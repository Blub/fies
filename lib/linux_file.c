#ifndef NO_DEBUG
# include <stdio.h>
#endif
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/ioctl.h>

#include "../config.h"

#if defined(MAJOR_IN_MKDEV)
# include <sys/mkdev.h>
#elif defined(MAJOR_IN_SYSMACROS)
# include <sys/sysmacros.h>
#endif

#include "fies.h"
#include "fies_writer.h"
#include "fies_linux.h"

#ifndef S_ISVTX
# define S_ISVTX 01000
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	int fd;
	uid_t uid;
	gid_t gid;
	dev_t dev;
	struct fies_time mtime;
	struct fiemap fm; // must be the last member
} FiesOSFile;
#pragma clang diagnostic pop

static void
FiesOSFile_close(FiesFile *handle)
{
	FiesOSFile *self = handle->opaque;
	if (self->fd != -1)
		close(self->fd);
	free(self);
}

static fies_ssz
FiesOSFile_pread(FiesFile *handle, void *buffer, fies_sz size, fies_sz off)
{
	FiesOSFile *self = handle->opaque;
	return pread(self->fd, buffer, size, (off_t)off);
}

extern bool
fies_mode_to_stat(uint32_t mode, mode_t *conv)
{
	switch (mode & FIES_M_FMT) {
	case FIES_M_FSOCK: *conv = S_IFSOCK; break;
	case FIES_M_FBLK:  *conv = S_IFBLK;  break;
	case FIES_M_FCHR:  *conv = S_IFCHR;  break;
	case FIES_M_FDIR:  *conv = S_IFDIR;  break;
	case FIES_M_FIFO:  *conv = S_IFIFO;  break;
	case FIES_M_FLNK:  *conv = S_IFLNK;  break;
	case FIES_M_FREG:  *conv = S_IFREG;  break;
	default:
		*conv = 0;
		break;
	}
	mode &= (unsigned)~(FIES_M_FMT);

	if (mode & FIES_M_PSUID) *conv |= S_ISUID;
	if (mode & FIES_M_PSGID) *conv |= S_ISGID;
	if (mode & FIES_M_PSSTICKY) *conv |= S_ISVTX;
	mode &= (unsigned)~(FIES_M_PSUID | FIES_M_PSGID | FIES_M_PSSTICKY);

	if (mode & FIES_M_PRUSR) *conv |= S_IRUSR;
	if (mode & FIES_M_PWUSR) *conv |= S_IWUSR;
	if (mode & FIES_M_PXUSR) *conv |= S_IXUSR;

	if (mode & FIES_M_PRGRP) *conv |= S_IRGRP;
	if (mode & FIES_M_PWGRP) *conv |= S_IWGRP;
	if (mode & FIES_M_PXGRP) *conv |= S_IXGRP;

	if (mode & FIES_M_PROTH) *conv |= S_IROTH;
	if (mode & FIES_M_PWOTH) *conv |= S_IWOTH;
	if (mode & FIES_M_PXOTH) *conv |= S_IXOTH;
	mode &= (unsigned)~(FIES_M_PRWXU | FIES_M_PRWXG | FIES_M_PRWXO);

	return mode == 0;
}

extern bool
fies_mode_from_stat(mode_t mode, uint32_t *conv)
{
	switch (mode & S_IFMT) {
	case S_IFSOCK: *conv = FIES_M_FSOCK; break;
	case S_IFBLK:  *conv = FIES_M_FBLK;  break;
	case S_IFCHR:  *conv = FIES_M_FCHR;  break;
	case S_IFDIR:  *conv = FIES_M_FDIR;  break;
	case S_IFIFO:  *conv = FIES_M_FIFO;  break;
	case S_IFLNK:  *conv = FIES_M_FLNK;  break;
	case S_IFREG:  *conv = FIES_M_FREG;  break;
	default:
		*conv = 0;
		break;
	}
	mode &= (unsigned)~(S_IFMT);

	if (mode & S_ISUID) *conv |= FIES_M_PSUID;
	if (mode & S_ISGID) *conv |= FIES_M_PSGID;
	if (mode & S_ISVTX) *conv |= FIES_M_PSSTICKY;
	mode &= (unsigned)~(S_ISUID | S_ISGID | S_ISVTX);

	if (mode & S_IRUSR) *conv |= FIES_M_PRUSR;
	if (mode & S_IWUSR) *conv |= FIES_M_PWUSR;
	if (mode & S_IXUSR) *conv |= FIES_M_PXUSR;

	if (mode & S_IRGRP) *conv |= FIES_M_PRGRP;
	if (mode & S_IWGRP) *conv |= FIES_M_PWGRP;
	if (mode & S_IXGRP) *conv |= FIES_M_PXGRP;

	if (mode & S_IROTH) *conv |= FIES_M_PROTH;
	if (mode & S_IWOTH) *conv |= FIES_M_PWOTH;
	if (mode & S_IXOTH) *conv |= FIES_M_PXOTH;
	mode &= (unsigned)~(S_IRWXU | S_IRWXG | S_IRWXO);

	return mode == 0;
}

static inline void
dump_extent(const struct fiemap_extent *ex)
{
#ifndef NO_DEBUG
	fprintf(stderr, "\
{\n\
	.fe_logical  = %llu\n\
	.fe_physical = %llu\n\
	.fe_length   = %llu\n\
	.fe_flags    = %x\n\
}\n",
	(unsigned long long)ex->fe_logical,
	(unsigned long long)ex->fe_physical,
	(unsigned long long)ex->fe_length,
	(unsigned int)ex->fe_flags
	);
#endif
}

extern int
FiesWriter_FIEMAP_to_Extent(FiesWriter *self,
                            FiesFile_Extent *dst,
                            struct fiemap_extent *src,
                            fies_sz filesize)
{
	const unsigned long known_flags = FIEMAP_EXTENT_LAST |
	                                  FIEMAP_EXTENT_UNWRITTEN |
	                                  FIEMAP_EXTENT_SHARED;
	unsigned long flags = src->fe_flags;

	if (flags & FIEMAP_EXTENT_MERGED)
		return FiesWriter_setError(self, ENOTSUP,
		       "file does not support extents\n");

	if (flags & FIEMAP_EXTENT_DATA_INLINE) {
		// In this case we know EXTENT_NOT_ALIGNED doesn't matter as
		// we never attempt to clone this one.
		flags &= (unsigned long)~(FIEMAP_EXTENT_DATA_INLINE |
		                          FIEMAP_EXTENT_NOT_ALIGNED);
	} else if (flags & FIEMAP_EXTENT_NOT_ALIGNED) {
		dump_extent(src);
		return FiesWriter_setError(self, ENOTSUP,
		       "encountered an unaligned extent");
	}

	if (flags & ~known_flags) {
#ifndef NO_DEBUG
		fprintf(stderr, "unhandled extent flags: %x\n",
		        src->fe_flags);
#endif
		return FiesWriter_setError(self, ENOTSUP,
		       "unhandled extent flags");
	}
	if (src->fe_logical >= filesize)
		return FiesWriter_setError(self, EINVAL,
		       "bad extent at or past EOF");

	dst->device = 0;
	dst->flags = 0;
	if (flags & FIEMAP_EXTENT_UNWRITTEN)
		dst->flags = FIES_FL_ZERO;
	else
		dst->flags = FIES_FL_DATA;
	if ((flags & FIEMAP_EXTENT_SHARED) &&
	    !(flags & FIEMAP_EXTENT_DATA_INLINE))
	{
		dst->flags |= FIES_FL_SHARED;
	}

	dst->logical = src->fe_logical;
	dst->physical = src->fe_physical;
	dst->length = src->fe_length;
	return 0;
}

static fies_ssz
FiesOSFile_nextExtents(FiesFile *handle,
                       FiesWriter *writer,
                       fies_pos logical_start,
                       FiesFile_Extent *buffer,
                       size_t count)
{
	FiesOSFile *self = handle->opaque;

	if (!count)
		return 0;

	self->fm.fm_start = (size_t)logical_start;
	self->fm.fm_length = handle->filesize - (size_t)logical_start;
	self->fm.fm_mapped_extents = 0;

	if (ioctl(self->fd, FS_IOC_FIEMAP, &self->fm) != 0) {
		// With -ldevmapper the FIEMAP ioctl ends up with an ENOENT
		// past the last data section...
		if (errno == ENOENT)
			return 0;
		return -errno;
	}

	if (count > self->fm.fm_mapped_extents)
		count = self->fm.fm_mapped_extents;

	if (!count)
		return 0;

	struct fiemap_extent *fex = self->fm.fm_extents;
	for (size_t i = 0; i != count; ++i) {
		int rc = FiesWriter_FIEMAP_to_Extent(writer,
		                                     &buffer[i],
		                                     &fex[i],
		                                     handle->filesize);
		if (rc < 0)
			return rc;
	}

	return (ssize_t)count;
}

static int
FiesOSFile_verifyExtent(FiesFile *handle,
                        FiesWriter *writer,
                        const FiesFile_Extent *extent)
{
	(void)writer;
	FiesOSFile *self = handle->opaque;
	(void)self;
	(void)extent;
	return 0;
}

static int
FiesOSFile_os_fd(FiesFile *handle)
{
	FiesOSFile *self = handle->opaque;
	return self->fd;
}

static int
FiesOSFile_owner(FiesFile *handle, uid_t *uid, gid_t *gid)
{
	FiesOSFile *self = handle->opaque;
	*uid = self->uid;
	*gid = self->gid;
	return 0;
}

static int
FiesOSFile_mtime(FiesFile *handle, struct fies_time *time)
{
	FiesOSFile *self = handle->opaque;
	*time = self->mtime;
	return 0;
}

static int
FiesOSFile_device(FiesFile *handle, uint32_t *out_major, uint32_t *out_minor)
{
	FiesOSFile *self = handle->opaque;
	*out_major = major(self->dev);
	*out_minor = minor(self->dev);
	return 0;
}

extern void
FiesFile_setDevice(struct FiesFile *self, fies_id id)
{
	self->device = id;
}

extern int
FiesFile_get_os_fd(FiesFile *self)
{
	if (self->funcs->get_os_fd)
		return self->funcs->get_os_fd(self);
	return -ENOTSUP;
}

static ssize_t
FiesOSFile_list_xattrs(struct FiesFile *handle, const char **names)
{
	FiesOSFile *self = handle->opaque;

	if (self->fd == -1 &&
	    ((handle->mode & FIES_M_FMT) == FIES_M_FLNK ||
	     (handle->mode & FIES_M_FMT) == FIES_M_FHARD))
	{
		return -ENOTSUP;
	}

	size_t sz;
	ssize_t ssz = 0;
	char *buffer = NULL;
	int err;

	ssz = flistxattr(self->fd, buffer, 0);
	if (ssz < 0)
		return -errno;
	do {
		sz = (size_t)ssz;
		char *tmp = realloc(buffer, sz);
		if (!tmp)
			goto out_errno;
		buffer = tmp;
		ssz = flistxattr(self->fd, buffer, sz);
		if (ssz < 0)
			goto out_errno;
	} while ((size_t)ssz > sz);
	sz = (size_t)ssz;

	if (!sz) {
		free(buffer);
		return 0;
	}

	*names = buffer;
	return ssz;
out_errno:
	err = -errno;
	free(buffer);
	return (ssize_t)err;
}

static void
FiesOSFile_free_xattr_list(struct FiesFile *handle, const char *names)
{
	(void)handle;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
	free((void*)names);
#pragma clang diagnostic pop
}

static ssize_t
FiesOSFile_get_xattr(struct FiesFile *handle,
                     const char *name,
                     const char **pbuffer)
{
	FiesOSFile *self = handle->opaque;

	if (self->fd == -1 &&
	    ((handle->mode & FIES_M_FMT) == FIES_M_FLNK ||
	     (handle->mode & FIES_M_FMT) == FIES_M_FHARD))
	{
		return -ENOTSUP;
	}

	char *buffer = NULL;
	size_t size = 0;
	ssize_t ssz = 0;

	ssz = fgetxattr(self->fd, name, NULL, 0);
	if (ssz < 0)
		return (ssize_t)-errno;
	if (!ssz)
		return 0;
	while ((size_t)ssz >= size) {
		char *tmp = realloc(buffer, (size_t)ssz+1);
		if (!tmp) {
			int rc = -errno;
			free(buffer);
			return (ssize_t)rc;
		}
		buffer = tmp;
		size = (size_t)ssz+1;
		ssz = fgetxattr(self->fd, name, buffer, size);
		if (ssz < 0) {
			int rc = -errno;
			free(buffer);
			return (ssize_t)rc;
		}
	}
	buffer[ssz] = 0;
	*pbuffer = buffer;
	return ssz;
}

static void
FiesOSFile_free_xattr(struct FiesFile *handle, const char *value)
{
	(void)handle;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
	free((void*)value);
#pragma clang diagnostic pop
}

const struct FiesFile_Funcs
fies_os_file_funcs = {
	.pread           = FiesOSFile_pread,
	.close           = FiesOSFile_close,
	.next_extents    = FiesOSFile_nextExtents,
	.verify_extent   = FiesOSFile_verifyExtent,
	.get_os_fd       = FiesOSFile_os_fd,
	.get_owner       = FiesOSFile_owner,
	.get_mtime       = FiesOSFile_mtime,
	.get_device      = FiesOSFile_device,
	.list_xattrs     = FiesOSFile_list_xattrs,
	.free_xattr_list = FiesOSFile_free_xattr_list,
	.get_xattr       = FiesOSFile_get_xattr,
	.free_xattr      = FiesOSFile_free_xattr,
};

static FiesFile*
FiesFile_fdopenfull(int fd,
                    const char *filename,
                    size_t filenamelen,
                    FiesWriter *writer,
                    unsigned int flags,
                    struct stat *stbuf,
                    const char *linkdest,
                    size_t linkdestlen)
{

	fies_id devid;
	int rc = FiesWriter_getOSDevice(writer, stbuf->st_dev,
	                                &devid,
	                                (flags & FIES_F_CREATE_DEVICE));
	if (rc < 0) {
		if (fd >= 0)
			close(fd);
		errno = -rc;
		return NULL;
	}

	uint32_t fiesmode;
	if (!fies_mode_from_stat(stbuf->st_mode, &fiesmode))
	{
#ifndef NO_DEBUG
		fprintf(stderr, "fies: unhandled stat() mode flags\n");
#endif
	}

	// The only thing without extents but a size is a symlink:
	size_t filesize = (size_t)stbuf->st_size;

	if (!linkdest != !S_ISLNK(stbuf->st_mode)) {
		errno = EBADF;
		return NULL;
	}
	else if (S_ISLNK(stbuf->st_mode))
		filesize = linkdestlen;
	else if (!S_ISREG(stbuf->st_mode))
		filesize = 0;

	//FiesFile *handle = malloc(sizeof(*handle));
	//memset(handle, 0, sizeof(*handle));
	FiesOSFile *self;

	const size_t bufsize = 1024*1024;
	const size_t datasize = bufsize - sizeof(*self);

	self = malloc(bufsize);
#ifndef NO_DEBUG
	// valgrind doesn't know the FIEMAP ioctl is filling the buffer
	memset(self, 0, bufsize);
#endif
	self->fd = fd;
	self->uid = stbuf->st_uid;
	self->gid = stbuf->st_gid;
	self->dev = stbuf->st_dev;
	self->mtime.secs = (fies_secs)stbuf->st_mtim.tv_sec;
	self->mtime.nsecs = (fies_nsecs)stbuf->st_mtim.tv_nsec;
	memset(&self->fm, 0, sizeof(self->fm));
	self->fm.fm_start = 0;
	self->fm.fm_length = (size_t)stbuf->st_size;
	self->fm.fm_flags = FIEMAP_FLAG_SYNC;
	self->fm.fm_mapped_extents = 0;
	self->fm.fm_extent_count = datasize / sizeof *self;

	FiesFile *file = FiesFile_new2(self, &fies_os_file_funcs,
	                               filename, filenamelen,
	                               linkdest, linkdestlen,
	                               filesize, fiesmode, devid);
	if (!file) {
		int err = errno;
		free(self);
		errno = err;
		return NULL;
	}
	return file;
}

static FiesFile*
FiesFile_openlinkat(int dirfd,
                    const char *filename,
                    FiesWriter *writer,
                    unsigned int flags)
{
	char linkbuf[PATH_MAX+1];
	size_t linkbuflen = 0;
	ssize_t got = readlinkat(dirfd, filename, linkbuf, sizeof(linkbuf));
	if (got < 0)
		return NULL;
	if ((size_t)got >= sizeof(linkbuf)) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	linkbuf[got] = 0;
	linkbuflen = (size_t)got;

	struct stat stbuf;
	if (fstatat(dirfd, filename, &stbuf, AT_SYMLINK_NOFOLLOW) != 0)
		return NULL;
	if (!S_ISLNK(stbuf.st_mode)) {
		errno = EBADF;
		return NULL;
	}
	return FiesFile_fdopenfull(-1, filename, strlen(filename),
	                           writer, flags, &stbuf,
	                           linkbuf, linkbuflen);
}

extern FiesFile*
FiesFile_fdopen(int fd,
                const char *filename,
                FiesWriter *writer,
                unsigned int flags)
{
#ifdef READLINKAT_EMPTY_PATH
	char linkbuf[PATH_MAX+1];
	size_t linkbuflen = 0;
#endif
	struct stat stbuf;
	if (fstat(fd, &stbuf) != 0) {
		int saved_errno = errno;
		close(fd);
		errno = saved_errno;
		return NULL;
	}
	// FIXME: An fd pointing to a symlink should only possible on linux
	// via O_PATH where we can also use readlinkat() with an empty string.
	// For other platforms this is an error:
	if (S_ISLNK(stbuf.st_mode) && !(flags & FIES_F_FOLLOW_SYMLINKS)) {
#ifndef READLINKAT_EMPTY_PATH
		errno = ELOOP;
		return NULL;
#else
		ssize_t got = readlinkat(fd, "", linkbuf, sizeof(linkbuf));
		if (got < 0)
			return NULL;
		if ((size_t)got >= sizeof(linkbuf)) {
			errno = ENAMETOOLONG;
			return NULL;
		}
		linkbuf[got] = 0;
		linkbuflen = (size_t)got;
#endif
	}
	return FiesFile_fdopenfull(fd, filename, strlen(filename), writer,
	                           flags, &stbuf,
# ifdef READLINKAT_EMPTY_PATH
	                           linkbuflen ? linkbuf : NULL, linkbuflen
# else
	                           NULL, 0
# endif
	                          );
}

extern FiesFile*
FiesFile_openat(int dirfd, 
                const char *filename,
                FiesWriter *writer,
                unsigned int flags)
{
	int mode = O_RDONLY;
	if ((!(flags & FIES_F_FOLLOW_SYMLINKS)))
		mode |= O_NOFOLLOW;
	int fd = openat(dirfd, filename, mode);
	if (fd < 0 && errno == ELOOP) {
		if ((flags & FIES_F_FOLLOW_SYMLINKS))
			return NULL;
		return FiesFile_openlinkat(dirfd, filename, writer, flags);
	}
	if (fd < 0)
		return NULL;
	return FiesFile_fdopen(fd, filename, writer, flags);
}

extern FiesFile*
FiesFile_open(const char *filename, FiesWriter *writer, unsigned int flags)
{
	return FiesFile_openat(AT_FDCWD, filename, writer, flags);
}
