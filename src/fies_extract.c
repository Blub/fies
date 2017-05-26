#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <assert.h>

#include <sys/sendfile.h>
#include <sys/ioctl.h>

#include "../lib/fies_linux.h"
#include "../lib/fies.h"
#include "../lib/map.h"

#ifdef FIES_MAJOR_MACRO_HEADER
# include FIES_MAJOR_MACRO_HEADER
#endif

#include "cli_common.h"
#include "util.h"
#include "regex.h"
#include "fies_cli.h"

#define FIES_FATTR_MTIME 0x01

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	int fd;
	int dirfd;
	uint32_t mode;
	char *fullpath;
	char *target;
	// Things like the modification time need to be set after we're done
	// modifying the file ;-).
	uint32_t flags;
	struct timespec time[2];
	size_t blocksize;
} FileHandle;
#pragma clang diagnostic pop

static ssize_t
do_read(void *opaque, void *data, size_t count)
{
	int *pfd = opaque;
	ssize_t got = read(*pfd, data, count);
	return got < 0 ? -errno : got;
}

static FileHandle*
FileHandle_new(int fd, uint32_t mode, char *fullpath)
{
	FileHandle *self = u_malloc0(sizeof(*self));
	if (!self)
		return NULL;
	self->fd = fd;
	self->dirfd = AT_FDCWD;
	self->mode = mode;
	self->fullpath = fullpath;
	self->time[0].tv_nsec = UTIME_OMIT;
	return self;
}

static void
FileHandle_delete(FileHandle *self)
{
	if (self->fd != -1)
		close(self->fd);
	if (self->dirfd != -1 && self->dirfd != AT_FDCWD)
		close(self->dirfd);
	if (self->mode == FIES_M_FLNK) {
		if (unlink(self->fullpath) != 0) {
			showerr("fies: failed to remove temporary file: %s\n",
			        self->fullpath, strerror(errno));
		}
		if (symlink(self->target, self->fullpath) != 0) {
			showerr("fies: symlink failed: %s: %s\n",
			        self->fullpath, strerror(errno));
		}
	}
	free(self->fullpath);
	free(self->target);
	free(self);
}

static char*
opt_transform_filename(const char *in_filename)
{
	char *filename = strip_components(in_filename, opt_strip_components);
	if (!filename)
		return NULL;
	char *xform = opt_apply_xform(filename, &opt_xform);
	free(filename);
	return xform;
}

// create directories up to dirname(file) allowing us to create the file in it
static void
make_dir_tree(char *file)
{
	char *next = file;
	while ( (next = strchr(next+1, '/')) ) {
		*next = 0;
		int rc = mkdir(file, 0777);
		if (rc != 0 && errno != EEXIST) {
			warn(WARN_MKDIR, "fies: failed to create path: %s\n",
			     file);
			*next = '/';
			return;
		}
		*next = '/';
	}
}

static int
do_create(void *opaque,
          const char *in_filename,
          size_t size,
          uint32_t mode,
          void **out_handle)
{
	(void)opaque;

	int fd = -1;
	int retval;
	int openmode = O_CREAT | O_EXCL;
	
	if (opt_incremental && !(fies_flags & FIES_F_INCREMENTAL))
		openmode |= O_TRUNC;

	unsigned long filetype = mode & FIES_M_FMT;
	bool do_truncate = (filetype == FIES_M_FREG);

	if (filetype == FIES_M_FREF) {
		showerr("fies: created reference file through bad callback\n");
		return -EINVAL;
	}

	mode_t perms = 0666;
	if (!fies_mode_to_stat(mode, &perms)) {
		warn(0, "fies: bad fies file mode flags\n");
		return -EINVAL;

	}
	if (opt_is_path_excluded(in_filename, perms, false, true)) {
		verbose(VERBOSE_EXCLUSIONS, "fies: excluding: %s\n",
		        in_filename);
		return 0;
	}
	perms &= ~(unsigned)S_IFMT;

	char *filename = opt_transform_filename(in_filename);
	if (!filename)
		return -errno;

	if (filetype == FIES_M_FREG) {
		openmode |= O_RDWR;
	} else {
		openmode |= O_WRONLY;
		openmode &= ~O_TRUNC;
		size = 0;
	}

	if (unlink(filename) != 0 && errno != ENOENT &&
	    !(filetype == FIES_M_FDIR && errno == EISDIR))
	{
		warn(WARN_UNLINK, "fies: error unlinking %s: %s\n",
		     filename, strerror(errno));
	}

	if (filetype == FIES_M_FDIR) {
		verbose(VERBOSE_FILES, "%s/\n", filename);
		openmode = O_DIRECTORY;
		verbose(VERBOSE_ACTIONS, "mkdir: %s\n", filename);
		if (mkdir(filename, perms) != 0 && errno != EEXIST) {
			retval = -errno;
			showerr("fies: mkdir(%s): %s\n",
			        filename, strerror(errno));
			goto err_out;
		}
	} else {
		verbose(VERBOSE_FILES, "%s\n", filename);
		verbose(VERBOSE_ACTIONS, "create: %s\n", filename);
	}

	make_dir_tree(filename);
	fd = open(filename, openmode, perms);
	if (fd < 0) {
		retval = -errno;
		showerr("fies: open(%s): %s\n", filename, strerror(errno));
		goto err_out;
	}
	struct stat stbuf;
	if (fstat(fd, &stbuf) != 0) {
		retval = -errno;
		showerr("fies: stat(%s): %s\n", filename, strerror(errno));
		goto err_out;
	}
	if (stbuf.st_blksize < 0) {
		retval = -EINVAL;
		showerr("fies: %s has negative block size\n", filename);
		goto err_out;
	}
	if (opt_uid != -1 || opt_gid != -1) {
		uid_t uid = (opt_uid != -1) ? (uid_t)opt_uid : (uid_t)-1;
		gid_t gid = (opt_gid != -1) ? (gid_t)opt_gid : (gid_t)-1;
		if (chown(filename, uid, gid) != 0) {
			warn(WARN_CHOWN, "fies: chown(%s): %s\n",
			     filename, strerror(errno));
		}
	}
	if (do_truncate && ftruncate(fd, (off_t)size) != 0) {
		retval = -errno;
		showerr("fies: truncate: %s\n", strerror(errno));
		goto err_out;
	}

	if (filetype == FIES_M_FDIR) {
		close(fd);
		fd = -1;
	}

	FileHandle *handle = FileHandle_new(fd, mode, filename);
	if (!handle) {
		retval = -errno;
		showerr("fies: %s\n", strerror(errno));
		goto err_out;
	}
	handle->blocksize = (size_t)stbuf.st_blksize;

	*out_handle = handle;
	return 0;

err_out:
	free(filename);
	if (fd != -1)
		close(fd);
	return retval;
}

static int
do_reference(void *opaque,
             const char *in_filename,
             size_t size,
             uint32_t mode,
             void **out_handle)
{
	(void)opaque;
	(void)mode;
	(void)out_handle;
	(void)size;
	int retval;

	char *filename = opt_transform_filename(in_filename);
	if (!filename)
		return -errno;

	int fd = open(filename, O_RDONLY);
	if (fd < 0) {
		retval = -errno;
		showerr("fies: open(%s): %s\n", filename, strerror(errno));
		goto err_out;
	}
	struct stat stbuf;
	if (fstat(fd, &stbuf) != 0) {
		retval = -errno;
		showerr("fies: stat(%s): %s\n", filename, strerror(errno));
		goto err_out;
	}
	if (stbuf.st_size != (off_t)size) {
		warn(WARN_REF_SIZE_MISMATCH, "fies: "
		     "reference to existing file assumes size %zu, "
		     "local file has size %zu\n",
		     size, (size_t)stbuf.st_size);
	}

	verbose(VERBOSE_FILES, "ref %s\n",
	        filename);

	FileHandle *handle = FileHandle_new(fd, mode, filename);
	if (!handle) {
		retval = -errno;
		showerr("fies: %s\n", strerror(errno));
		goto err_out;
	}
	handle->blocksize = (size_t)stbuf.st_blksize;

	*out_handle = handle;
	return 0;

err_out:
	free(filename);
	if (fd != -1)
		close(fd);
	return retval;
}

static int
do_hardlink(void *opaque, void *psrc, const char *in_filename)
{
	(void)opaque;
	FileHandle *src = psrc;
	mode_t perms = 0666;
	if (!fies_mode_to_stat(src->mode, &perms)) {
		warn(0, "fies: bad fies file mode flags\n");
		return -EINVAL;
	}
	if (opt_is_path_excluded(in_filename, perms, false, true)) {
		verbose(VERBOSE_EXCLUSIONS, "fies: excluding: %s\n",
		        in_filename);
		return 0;
	}

	char *filename = opt_transform_filename(in_filename);
	if (!filename)
		return -errno;
	verbose(VERBOSE_FILES, "%s => %s\n", filename, src->fullpath);
	verbose(VERBOSE_ACTIONS, "link: %s => %s\n", filename, src->fullpath);
	int ret;
	if (unlink(filename) != 0 && errno != ENOENT) {
		ret = -errno;
		free(filename);
		showerr("fies: failed to unlink %s: %s\n",
		        filename, strerror(-ret));
		return ret;
	}
	make_dir_tree(filename);
	ret = link(src->fullpath, filename) == 0 ? 0 : -errno;
	free(filename);
	return ret;
}

static int
do_mkdir(void *opaque, const char *dirname, uint32_t mode, void **handle)
{
	return do_create(opaque, dirname, 0, mode, handle);
}

static int
do_mknod(void *opaque,
         const char *in_filename,
         uint32_t mode,
         uint32_t major_id,
         uint32_t minor_id,
         void **handle)
{
	(void)opaque;
	int retval = 0;
	int dirfd = -1;
	mode_t perms = 0666;
	if (!fies_mode_to_stat(mode, &perms)) {
		warn(0, "fies: bad fies file mode flags\n");
		return -EINVAL;
	}
	if (opt_is_path_excluded(in_filename, perms, false, true)) {
		verbose(VERBOSE_EXCLUSIONS, "fies: excluding: %s\n",
		        in_filename);
		return 0;
	}
	char *filename = opt_transform_filename(in_filename);
	if (!filename)
		return -errno;

	if (common.verbose >= VERBOSE_FILES) {
		unsigned long filetype = mode & FIES_M_FMT;
		char devtype = (filetype == FIES_M_FCHR) ? 'c' :
		               (filetype == FIES_M_FBLK) ? 'b' :
		               '?';
		verbose(VERBOSE_FILES, "%s (%c:%u:%u)\n",
		        filename, devtype, major_id, minor_id);
	}
	verbose(VERBOSE_ACTIONS, "mknod: %s %u %u\n",
	        filename, major_id, minor_id);

	// Since we don't want to open the device itself we open a directory
	// handle and use fchmodat() & friends.
	char *dir, *base;
	path_parts(filename, &dir, &base, PATH_PARTS_RELATIVE_DOT);
	if (!dir || !base) {
		showerr("fies: invalid path: %s\n", filename);
		retval = -EINVAL;
		goto err_out;
	}

	make_dir_tree(dir);
	mkdir(dir, 0777);
	dirfd = open(dir, O_DIRECTORY);
	if (dirfd < 0) {
		retval = -errno;
		showerr("fies: error opening directory %s: %s\n",
		        dir, strerror(errno));
		goto err_out;
	}
	free(dir);
	dir = NULL;

	if (unlinkat(dirfd, filename, 0) != 0 && errno != ENOENT) {
		retval = -errno;
		showerr("fies: failed to unlink %s: %s\n",
		        filename, strerror(errno));
		goto err_out;
	}

	if (mknodat(dirfd, base, mode, makedev(major_id, minor_id)) != 0) {
		retval = -errno;
		showerr("fies: mknod(%s): %s\n", filename, strerror(errno));
		goto err_out;
	}

	FileHandle *fh = FileHandle_new(-1, mode, filename);
	fh->dirfd = dirfd;
	fh->target = base;
	*handle = fh;
	return 0;
err_out:
	if (dirfd != -1)
		close(dirfd);
	free(dir);
	free(base);
	free(filename);
	return retval;
}

static int
do_symlink(void *opaque,
           const char *in_filename,
           const char *target,
           void **handle)
{
	(void)opaque;
	int retval = 0;
	if (opt_is_path_excluded(in_filename, S_IFLNK, false, true)) {
		verbose(VERBOSE_EXCLUSIONS, "fies: excluding: %s\n",
		        in_filename);
		return 0;
	}
	char *filename = opt_transform_filename(in_filename);
	if (!filename)
		return -errno;
	verbose(VERBOSE_FILES, "-> %s\n", filename, target);
	verbose(VERBOSE_ACTIONS, "symlink: %s -> %s\n", filename, target);

	if (unlink(filename) != 0 && errno != ENOENT) {
		retval = -errno;
		showerr("fies: failed to unlink %s: %s\n",
		        filename, strerror(errno));
		goto err_out;
	}

	make_dir_tree(filename);
	int fd = open(filename, O_CREAT | O_EXCL | O_WRONLY, 0000);
	if (fd < 0) {
		retval = -errno;
		showerr("fies: failed to create file for symlink: %s: %s\n",
		        filename, strerror(errno));
		goto err_out;
	}
	close(fd);

	FileHandle *fh = FileHandle_new(-1, FIES_M_FLNK, filename);
	fh->target = strdup(target);
	*handle = fh;
	return 0;
err_out:
	free(filename);
	return retval;
}

static ssize_t
do_send(void *opaque, void *out, fies_pos pos, size_t count)
{
	int fdin = *(int*)opaque;
	FileHandle *fhout = out;
	off_t sk = lseek(fhout->fd, (off_t)pos, SEEK_SET);
	if (sk < 0)
		return sk;
	verbose(VERBOSE_ACTIONS, "send: %zx : %zx => %s\n",
	        pos, count, fhout->fullpath);
	ssize_t put = sendfile(fhout->fd, fdin, NULL, count);
	return put < 0 ? -errno : put;
}

static int
do_punch_hole(void *opaque, void *out, fies_pos off, size_t length)
{
	(void)opaque;
	FileHandle *fhout = out;
	verbose(VERBOSE_ACTIONS, "punch hole: %zx : %zx => %s\n",
	        off, length, fhout->fullpath);
	if (fallocate(fhout->fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
	              (off_t)off, (off_t)length) == 0)
	{
		return 0;
	}
	return -errno;
}

static ssize_t
make_zero(void *opaque, void *out, fies_pos pos, size_t count)
{
	FileHandle *fhout = out;
	int mode = FALLOC_FL_ZERO_RANGE | FALLOC_FL_KEEP_SIZE;
	verbose(VERBOSE_ACTIONS, "zero-out: %zx : %zx => %s\n",
	        pos, count, fhout->fullpath);
	if (fallocate(fhout->fd, mode, (off_t)pos, (off_t)count) == 0)
		return (ssize_t)count;

	int rc;
	if (errno == EOPNOTSUPP) {
		verbose(VERBOSE_ACTIONS, "zero-out failed, punching hole\n");
		rc = do_punch_hole(opaque, out, pos, count);
	} else {
		return -errno;
	}
	if (rc < 0)
		return rc;
	return (ssize_t)count;
}

static ssize_t
do_pwrite(void *opaque, void *out, const void *buf, size_t count, fies_pos pos)
{
	FileHandle *fhout = out;
	if (!buf)
		return make_zero(opaque, out, pos, count);
	verbose(VERBOSE_ACTIONS, "write: %zx : %zx => %s\n",
	        pos, count, fhout->fullpath);
	ssize_t put = pwrite(fhout->fd, buf, count, (off_t)pos);
	return put < 0 ? -errno : put;
}

static int
do_full_copy(void *opaque,
             void *dst, fies_pos dstoff,
             void *src, fies_pos srcoff,
             fies_sz len)
{
	(void)opaque;
	(void)dstoff;
	(void)srcoff;

	FileHandle *srcfh = src;
	FileHandle *dstfh = dst;

	verbose(VERBOSE_ACTIONS, "  (copy): (%s)0x%zx => 0x%zx(%s) : 0x%zx\n",
	        srcfh->fullpath, srcoff,
	        dstoff, dstfh->fullpath,
	        len);

	off_t pos = lseek(dstfh->fd, (off_t)dstoff, SEEK_SET);
	if (pos < 0) {
		int rc = -errno;
		showerr("fies: seek failed in %s: %s\n",
		        dstfh->fullpath, strerror(errno));
		return rc;
	}
	if ((fies_pos)pos != dstoff) {
		showerr("fies: bad seek offset in %s\n", dstfh->fullpath);
		return -EIO;
	}

	off_t soff = (off_t)srcoff;
	ssize_t put = sendfile(dstfh->fd, srcfh->fd, &soff, (size_t)len);
	if (put < 0)
		return (int)put;
	if ((fies_sz)put != len) {
		showerr("fies: short write\n");
		return -EIO;
	}
	clone_info.unshared += len;
	return 0;
}

static int
do_clone(void *opaque,
         void *dst, fies_pos dstoff,
         void *src, fies_pos srcoff,
         size_t len)
{
	(void)opaque;

	clone_info.stream_shared += len;

	if (opt_clone == CLONE_NEVER)
		return do_full_copy(opaque, dst, dstoff, src, srcoff, len);

	FileHandle *srcfh = src;
	FileHandle *dstfh = dst;
	verbose(VERBOSE_ACTIONS, "clone: (%s)0x%zx => 0x%zx(%s) : 0x%zx\n",
	        srcfh->fullpath, srcoff,
	        dstoff, dstfh->fullpath,
	        len);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
	struct file_clone_range range;
	range.src_fd = srcfh->fd;
	range.src_offset = srcoff;
	range.src_length = len;
	range.dest_offset = dstoff;
#pragma clang diagnostic pop
	if (ioctl(dstfh->fd, FICLONERANGE, &range) == 0) {
		clone_info.shared += range.src_length;
		return 0;
	}

	int err = -errno;
	if (opt_clone == CLONE_FORCE)
		return err;

	if (err == -EOPNOTSUPP)
		return do_full_copy(opaque, dst, dstoff, src, srcoff, len);

	if (err != -EINVAL)
		return err;
	// EINVAL can mean we have unaligned offsets or sizes, note that this
	// typically only makes sense if both files use the same block size
	// since otherwise we'd most likely be on different devices and getting
	// EXDEV instead.
	if (srcfh->blocksize != dstfh->blocksize)
		return do_full_copy(opaque, dst, dstoff, src, srcoff, len);

	// Check alignment and shifts:
	fies_pos a_srcoff = FIES_ALIGN_UP(srcoff, srcfh->blocksize);
	fies_pos a_dstoff = FIES_ALIGN_UP(dstoff, dstfh->blocksize);
	// In order to be able to do partia clones both need to be
	// misaligned the same way since otherwise we'd be shifting
	// like below on the left (1), which is impossible.
	// If case is as shown on the right (2) we can do a full-copy
	// of the upper part of block 3 and then clone block 4
	// A: [  block 1 ][  block 2 ][  block 3 ][  block 4 ]
	//              /   /                |               |
	//          (1)/   /              (2)|               |
	//            /   /                  |               |
	// B: [  block 1 ][  block 2 ][  block 3 ][  block 4 ]
	fies_sz a_srcdiff = a_srcoff - srcoff;
	fies_sz a_dstdiff = a_dstoff - dstoff;
	if (a_srcdiff != a_dstdiff) // case 1 above
		return do_full_copy(opaque, dst, dstoff, src, srcoff, len);

	int rc;
	if (a_srcdiff) {
		rc = do_full_copy(opaque, dst, dstoff, src, srcoff, a_srcdiff);
		if (rc < 0)
			return rc;
		len -= a_srcdiff;
		srcoff = a_srcoff;
		dstoff = a_dstoff;
	}

	// Check if we're even copying a full block:
	size_t a_len = FIES_ALIGN_DOWN(len, dstfh->blocksize);
	if (!a_len)
		return do_full_copy(opaque, dst, dstoff, src, srcoff, len);

	range.src_offset = a_srcoff;
	range.dest_offset = a_dstoff;
	range.src_length = a_len;
	rc = ioctl(dstfh->fd, FICLONERANGE, &range);
	if (rc != 0)
		return do_full_copy(opaque, dst, dstoff, src, srcoff, len);
	clone_info.shared += range.src_length;

	// Is there a rest?
	len -= a_len;
	srcoff += a_len;
	dstoff += a_len;
	if (len)
		return do_full_copy(opaque, dst, dstoff, src, srcoff, len);

	return 0;
}

static int
do_chown(void *opaque, void *pfd, uid_t uid, gid_t gid)
{
	(void)opaque;

	FileHandle *fh = pfd;
	uint32_t filetype = fh->mode & FIES_M_FMT;
	if (filetype == FIES_M_FLNK || filetype == FIES_M_FHARD) {
		verbose(VERBOSE_ACTIONS, "not changing owner of link: %s\n",
		        fh->fullpath);
		return 0;
	}

	if (opt_uid != -1 && opt_gid != -1) {
		verbose(VERBOSE_ACTIONS, "skipping chown\n");
		return 0;
	}

	if (opt_uid != -1)
		uid = (uid_t)opt_uid;
	if (opt_gid != -1)
		gid = (gid_t)opt_gid;
	int rc;
	verbose(VERBOSE_ACTIONS, "chown: %s %u %u\n",
	        fh->fullpath ? fh->fullpath : "", uid, gid);
	if (fh->fd != -1)
		rc = fchown(fh->fd, uid, gid);
	else if (fh->target)
		rc = fchownat(fh->dirfd, fh->target, uid, gid,
		              AT_SYMLINK_NOFOLLOW);
	else
		rc = -(errno = EFAULT);

	if (rc != 0)
		warn(WARN_CHOWN, "fies: chown: %s (ignoring error)\n",
		     strerror(errno));
	return 0;
}

static int
do_set_mtime(void *opaque, void *pfd, struct fies_time time)
{
	(void)opaque;
	FileHandle *fh = pfd;

	uint32_t filetype = fh->mode & FIES_M_FMT;
	if (filetype == FIES_M_FLNK || filetype == FIES_M_FHARD) {
		verbose(VERBOSE_ACTIONS, "not changing time of link: %s\n",
		        fh->fullpath);
		return 0;
	}

	verbose(VERBOSE_ACTIONS, "setmtime: %s %zu %zu\n",
	        fh->fullpath, (size_t)time.secs, (size_t)time.nsecs);

	if (fh->flags & FIES_FATTR_MTIME)
		warn(WARN_STREAM, "fies: multiple modification times for %s\n",
		     fh->fullpath);
	fh->flags |= FIES_FATTR_MTIME;
	fh->time[1].tv_sec = (time_t)time.secs;
	fh->time[1].tv_nsec = (long)time.nsecs;
	return 0;
}

static int
do_set_xattr(void *opaque,
             void *pfd,
             const char *name,
             const char *value,
             size_t length)
{
	(void)opaque;
	FileHandle *fh = pfd;

	if (opt_is_xattr_excluded(name)) {
		verbose(VERBOSE_EXCLUSIONS, "fies: excluding xattr %s\n",
		        name);
		return 0;
	}

	verbose(VERBOSE_ACTIONS, "setxattr: %s: %s = data of size %zu\n",
	        fh->fullpath, name, length);

	int rc;
	if (fh->fd != -1) {
		rc = fsetxattr(fh->fd, name, value, length, 0)
		     == 0 ? 0 : -errno;
	}
	else if (fh->target) {
		int fd = openat(fh->dirfd, fh->target, O_WRONLY);
		if (fd < 0) {
			rc = -errno;
		} else {
			rc = fsetxattr(fd, name, value, length, 0)
			     == 0 ? 0 : -errno;
			close(fd);
		}
	} else {
		rc = setxattr(fh->fullpath, name, value, length, 0)
		     == 0 ? 0 : -errno;
	}
	if (rc < 0)
		warn(WARN_XATTR, "fies: setxattr: %s\n", strerror(-rc));
	return 0;
}

static int
do_meta_end(void *opaque, void *fd)
{
	(void)opaque;
	(void)fd;
	return 0;
}

static int
do_close(void *opaque, void *pfd)
{
	(void)opaque;
	FileHandle_delete(pfd);
	return 0;
}

static int
do_file_done(void *opaque, void *pfd)
{
	(void)opaque;
	FileHandle *fh = pfd;
	if (fh->flags & FIES_FATTR_MTIME) {
		int rc;
		if (fh->fd != -1)
			rc = futimens(fh->fd, fh->time);
		else if (fh->target)
			rc = utimensat(fh->dirfd, fh->target, fh->time,
			               AT_SYMLINK_NOFOLLOW);
		else
			rc = -(errno = EFAULT);
		if (rc != 0)
			warn(WARN_MTIME,
			     "fies: futimens: %s (ignoring error)\n",
			     strerror(errno));
	}
	return 0;

}

static void
do_finalize(void *opaque)
{
	(void)opaque;
}

const struct FiesReader_Funcs
extract_reader_funcs = {
	.read       = do_read,
	.create     = do_create,
	.reference  = do_reference,
	.hardlink   = do_hardlink,
	.mkdir      = do_mkdir,
	.symlink    = do_symlink,
	.mknod      = do_mknod,
	.chown      = do_chown,
	.set_mtime  = do_set_mtime,
	.set_xattr  = do_set_xattr,
	.meta_end   = do_meta_end,
	.send       = do_send,
	.pwrite     = do_pwrite,
	.punch_hole = do_punch_hole,
	.clone      = do_clone,
	.file_done  = do_file_done,
	.close      = do_close,
	.finalize   = do_finalize
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	char    *name;
	char    *target;
	size_t   size;
	uint32_t mode;
	uint32_t uid, gid;
	uint32_t major_id;
	uint32_t minor_id;
	time_t   mtime;
	bool     has_xattrs;
	bool     has_acls;
} ListFileHandle;
#pragma clang diagnostic pop

static ListFileHandle*
ListFileHandle_new(const char *name, uint32_t mode, size_t size)
{
	ListFileHandle *self = u_malloc0(sizeof(*self));
	mode_t perms = 0666;
	if (!fies_mode_to_stat(mode & FIES_M_PERMS, &perms)) {
		warn(0, "fies: bad fies file mode flags\n");
		perms = S_IFREG | 0666;
	}
	if (opt_is_path_excluded(name, perms, false, true)) {
		verbose(VERBOSE_EXCLUSIONS, "fies: excluding: %s\n", name);
		errno = 0;
		return NULL;
	}
	self->name = opt_transform_filename(name);
	if (!self->name) {
		free(self);
		errno = ENOMEM;
		return NULL;
	}
	self->size = size;
	self->mode = mode;
	return self;
}

static ListFileHandle*
ListFileHandle_new_from(const char *name, const ListFileHandle *src)
{
	ListFileHandle *self = u_memdup(src, sizeof(*src));
	self->name = strdup(name);
	self->target = NULL;
	return self;
}

static void
ListFileHandle_delete(void *opaque)
{
	ListFileHandle *self = opaque;
	free(self->name);
	free(self->target);
	free(self);
}

static void
ListFileHandle_show(const ListFileHandle *fh)
{
	if (!common.verbose) {
		printf("%s\n", fh->name);
		return;
	}
	bool device = false;
	char lsline[] = "----------";
	switch (fh->mode & FIES_M_FMT) {
	case FIES_M_FREG:  lsline[0] = '-'; break;
	case FIES_M_FHARD: lsline[0] = 'h'; break;
	case FIES_M_FSOCK: lsline[0] = 's'; break;
	case FIES_M_FLNK:  lsline[0] = 'l'; break;
	case FIES_M_FDIR:  lsline[0] = 'd'; break;
	case FIES_M_FIFO:  lsline[0] = 'p'; break;
	case FIES_M_FBLK:  lsline[0] = 'b'; device = true; break;
	case FIES_M_FCHR:  lsline[0] = 'c'; device = true; break;
	case FIES_M_FREF:
		memcpy(lsline, "ref       ", sizeof(lsline)-1);
		break;
	default:
		lsline[0] = '?';
		break;
	}

	if (fh->mode & FIES_M_PRUSR) lsline[1] = 'r';
	if (fh->mode & FIES_M_PWUSR) lsline[2] = 'w';
	if (fh->mode & FIES_M_PXUSR) lsline[3] = 'x';
	if (fh->mode & FIES_M_PSUID) lsline[3] = 's';

	if (fh->mode & FIES_M_PRGRP) lsline[4] = 'r';
	if (fh->mode & FIES_M_PWGRP) lsline[5] = 'w';
	if (fh->mode & FIES_M_PXGRP) lsline[6] = 'x';
	if (fh->mode & FIES_M_PSGID) lsline[6] = 's';

	if (fh->mode & FIES_M_PROTH) lsline[7] = 'r';
	if (fh->mode & FIES_M_PWOTH) lsline[8] = 'w';
	if (fh->mode & FIES_M_PXOTH) lsline[9] = 'x';
	if (fh->mode & FIES_M_PSSTICKY) lsline[9] = 't';

	printf("%s 0 %-6u %-6u", lsline, fh->uid, fh->gid);
	if (device)
		printf(" %4u, %4u", fh->major_id, fh->minor_id);
	else
		printf(" %10zu", fh->size);

	char buf[256];
	struct tm now;
	if (localtime_r(&fh->mtime, &now) &&
	    strftime(buf, sizeof(buf), "%c", &now))
	{
		printf(" %s %s", buf, fh->name);
	} else {
		printf(" ??? %s", fh->name);
	}
	if (lsline[0] == 'd')
		printf("/");
	if (fh->target)
		printf(" -> %s\n", fh->target);
	else
		printf("\n");
}

static int
list_create(void *opaque,
          const char *filename,
          size_t size,
          uint32_t mode,
          void **handle)
{
	(void)opaque;
	*handle = ListFileHandle_new(filename, mode, size);
	if (*handle && (mode & FIES_M_FMT) == FIES_M_FREF)
		ListFileHandle_show(*handle);
	return *handle ? 0 : -errno;
}

static int
list_hardlink(void *opaque, void *psrc, const char *filename)
{
	(void)opaque;
	ListFileHandle *src = psrc;
	ListFileHandle *file = ListFileHandle_new_from(filename, src);
	file->mode = (src->mode & (unsigned)~FIES_M_FMT) | FIES_M_FHARD;
	ListFileHandle_show(file);
	ListFileHandle_delete(file);
	return 0;
}

static int
list_mkdir(void *opaque, const char *dirname, uint32_t mode, void **handle)
{
	(void)opaque;
	*handle = ListFileHandle_new(dirname, mode, 0);
	return *handle ? 0 : -errno;
}

static int
list_mknod(void *opaque,
         const char *filename,
         uint32_t mode,
         uint32_t major_id,
         uint32_t minor_id,
         void **handle)
{
	(void)opaque;
	ListFileHandle *self = ListFileHandle_new(filename, mode, 0);
	if (!self)
		return -errno;
	self->major_id = major_id;
	self->minor_id = minor_id;
	*handle = self;
	return 0;
}

static int
list_symlink(void *opaque, const char *file, const char *target, void **handle)
{
	(void)opaque;
	ListFileHandle *self = ListFileHandle_new(file, FIES_M_FLNK|0777, 0);
	if (!self)
		return -errno;
	self->target = strdup(target);
	if (!self->target) {
		ListFileHandle_delete(self);
		return -ENOMEM;
	}
	*handle = self;
	return 0;
}

static int
list_chown(void *opaque, void *pfd, uid_t uid, gid_t gid)
{
	(void)opaque;
	ListFileHandle *self = pfd;
	self->uid = uid;
	self->gid = gid;
	return 0;
}

static int
list_set_mtime(void *opaque, void *pfd, struct fies_time time)
{
	(void)opaque;
	ListFileHandle *self = pfd;
	self->mtime = (time_t)time.secs;
	return 0;
}

static int
list_meta_end(void *opaque, void *fd)
{
	(void)opaque;
	ListFileHandle_show(fd);
	return 0;
}

static int
list_close(void *opaque, void *fd)
{
	(void)opaque;
	ListFileHandle_delete(fd);
	return 0;
}

//static ssize_t
//list_send(void *opaque, void *out, fies_pos pos, size_t count)
//{
//	(void)opaque;
//	(void)out;
//	(void)pos;
//	// do the actual reading (seeking) too!
//	return (ssize_t)count;
//}

static ssize_t
list_pwrite(void *opaque, void *out, const void *buf, size_t cnt, fies_pos pos)
{
	(void)opaque;
	(void)out;
	(void)buf;
	(void)pos;
	return (ssize_t)cnt;
}

static int
list_punch_hole(void *opaque, void *out, fies_pos off, size_t length)
{
	(void)opaque;
	(void)out;
	(void)off;
	(void)length;
	return 0;
}

static int
list_clone(void *opaque,
           void *dst, fies_pos dstoff,
           void *src, fies_pos srcoff,
           size_t len)
{
	(void)opaque;
	(void)dst;
	(void)dstoff;
	(void)src;
	(void)srcoff;
	(void)len;
	return 0;
}

const struct FiesReader_Funcs
list_reader_funcs = {
	.read       = do_read,
	.create     = list_create,
	.reference  = list_create,
	.hardlink   = list_hardlink,
	.mkdir      = list_mkdir,
	.symlink    = list_symlink,
	.mknod      = list_mknod,
	.chown      = list_chown,
	.set_mtime  = list_set_mtime,
	.meta_end   = list_meta_end,
	.send       = NULL,
	.pwrite     = list_pwrite,
	.punch_hole = list_punch_hole,
	.clone      = list_clone,
	.close      = list_close,
};
