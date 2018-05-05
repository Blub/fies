#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <assert.h>

#include <sys/sendfile.h>

#include "../lib/fies.h"
#include "../lib/map.h"

#include "cli_common.h"
#include "util.h"
#include "fies_regex.h"
#include "fies_cli.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct FileLink {
	dev_t   device;
	ino_t   inode;
	fies_id fileid;
} FileLink;
#pragma clang diagnostic pop
static Map *file_links;

static struct FiesFile_Funcs file_funcs;

static ssize_t
my_file_get_xattr(struct FiesFile *handle,
                   const char *name,
                   const char **pbuffer)
{
	if (opt_is_xattr_excluded(name))
		return -ENOENT;
	return fies_os_file_funcs.get_xattr(handle, name, pbuffer);
}

void
create_init()
{
	memcpy(&file_funcs, &fies_os_file_funcs, sizeof(file_funcs));
	file_funcs.get_xattr = my_file_get_xattr;
}

static struct dirent*
DIR_read(DIR *dir, struct dirent *data)
{
#ifdef USE_READDIR_R
	struct dirent *entry = NULL;
	if (readdir_r(dir, data, &entry) != 0)
		return NULL;
	return entry;
#else
	(void)data;
	return readdir(dir);
#endif
}

static FileLink*
FileLink_new(dev_t device, ino_t inode, fies_id fileid)
{
	FileLink *self = malloc(sizeof(*self));
	if (!self)
		return NULL;
	self->device = device;
	self->inode = inode;
	self->fileid = fileid;
	return self;
}

static int
FileLink_cmp_p(const void *pa, const void *pb)
{
	FileLink *a = *(void *const *)pa;
	FileLink *b = *(void *const *)pb;
	return (a->device < b->device) ? -1 :
	       (a->device > b->device) ?  1 :
	       (a->inode < b->inode) ? -1 :
	       (a->inode > b->inode) ?  1 :
	       0;
}

static const FileLink*
get_existing_file(const struct stat *stbuf)
{
	if (!file_links)
		return NULL;
	FileLink key = {
		.device = stbuf->st_dev,
		.inode = stbuf->st_ino
	};
	return Map_getp(file_links, &key);
}

static int
register_existing_file(struct FiesFile *file, const struct stat *stbuf)
{
	if (!file_links) {
		file_links = Map_new_type(FileLink_cmp_p,
		                          FileLink*, NULL,
		                          FileLink, free);
		if (!file_links)
			return -errno;
	}
	FileLink *link = FileLink_new(stbuf->st_dev,
	                              stbuf->st_ino,
	                              file->fileid);
	if (!link)
		return -errno;
	Map_insert(file_links, &link, link);
	return 0;
}

int
do_create_add(struct FiesWriter *fies,
              int dirfd,
              const char *basepart,
              const char *fullpath,
              dev_t dev,
              const char *xformed)
{
	const bool is_recursion = (dirfd != AT_FDCWD);
	int retval = -EINVAL;
	// FIXME: opt_acls

	char *xform_path = NULL;
	if (!xformed) {
		xform_path = opt_apply_xform(fullpath, &opt_xform);
		if (!xform_path)
			return -errno;
		xformed = xform_path;
	}

	unsigned int flags = is_recursion ? 0 : FIES_FILE_CREATE_DEVICE;
	if (opt_dereference)
		flags |= FIES_FILE_FOLLOW_SYMLINKS;

	struct FiesFile *file = FiesFile_openat(dirfd, basepart, fies, flags);
	if (!file) {
		retval = -errno;
		showerr("fies: open(%s): %s\n", fullpath, strerror(errno));
		goto out;
	}

	// Replace the file functions so we can handle xattr getters
	file->funcs = &file_funcs;

	mode_t perms = 0666;
	if (!fies_mode_to_stat(file->mode, &perms)) {
		showerr("fies: bad file mode\n");
		retval = -EFAULT;
		goto out;
	}
	if (opt_is_path_excluded(fullpath, perms, is_recursion, false)) {
		warn(WARN_EXCLUDED, "fies: excluding: %s\n", fullpath);
		retval = 0;
		goto out;
	}

	free(file->filename);
	file->filename = strdup(xformed);
	if (!file->filename) {
		retval = -errno;
		goto out;
	}
	unsigned long filetype = (file->mode & FIES_M_FMT);

	struct stat stbuf;
	int fd = FiesFile_get_os_fd(file);
	bool register_file = false;
	if (fd < 0) {
		// FiesFile doesn't keep file descriptors for symlinks.
		// But we cannot recurse into them so they do not matter for
		// our opt_no_xdev check anyway.
		if (filetype != FIES_M_FLNK) {
			showerr("fies: no file descriptor for %s\n", fullpath);
			retval = -ENOENT;
			goto out;
		}
	} else {
		if (fstat(fd, &stbuf) != 0) {
			retval = -errno;
			showerr("fies: fstat(%s): %s\n",
			        fullpath, strerror(errno));
			goto out;
		}

		if (opt_noxdev && is_recursion) {
			if (stbuf.st_dev != dev) {
				retval = 0;
				goto out;
			}
			dev = stbuf.st_dev;
		}

		const FileLink *old = opt_hardlinks ? get_existing_file(&stbuf)
		                                    : NULL;
		if (old) {
			file->mode &= (unsigned)~FIES_M_FMT;
			file->mode |= FIES_M_FHARD;
			free(file->linkdest);
			file->linkdest = NULL;
			file->fileid = old->fileid;
		} else if (opt_hardlinks) {
			register_file = true;
		}
	}

	verbose(VERBOSE_FILES, "%s\n", xformed);
	retval = FiesWriter_writeFile(fies, file);
	if (retval < 0) {
		const char *err = FiesWriter_getError(fies);
		showerr("fies: writing file %s: %s\n",
		        file->filename, err ? err : strerror(-retval));
		goto out;
	}
	if (register_file) {
		retval = register_existing_file(file, &stbuf);
		if (retval < 0) {
			showerr("fies: indexing file %s: %s\n",
			        file->filename, strerror(-retval));
			goto out;
		}
	}
	if (fd >= 0)
		fd = dup(fd);
	FiesFile_close(file);
	file = NULL;

	retval = 0;
	if (opt_recurse && filetype == FIES_M_FDIR) {
		assert(fd >= 0);
		DIR *dir = fdopendir(fd);
		struct dirent data;
		struct dirent *entry;
		while ((entry = DIR_read(dir, &data))) {
			if (!strcmp(entry->d_name, ".") ||
			    !strcmp(entry->d_name, ".."))
			{
				continue;
			}
			char *inpath = make_path(xformed, entry->d_name,
			                         NULL);
			if (!inpath) {
				retval = -errno;
				goto out;
			}
			retval = do_create_add(fies, fd, entry->d_name,
			                         inpath, dev, NULL);
			free(inpath);
			if (retval < 0)
				break;
		}
		closedir(dir);
	}

out:
	free(xform_path);
	FiesFile_close(file);
	return retval;
}

int
create_add(FiesWriter *fies, const char *arg)
{
	return do_create_add(fies, AT_FDCWD, arg, arg, 0, NULL);
}

static ssize_t
wr_writev(void *opaque, const struct iovec *iov, size_t count)
{
	int stream_fd = *(int*)opaque;
	ssize_t put = writev(stream_fd, iov, (int)count);
	return put < 0 ? -errno : put;
}

static fies_ssz
wr_sendfile(void *opaque, struct FiesFile *infile, fies_pos pos, fies_sz count)
{
	int stream_fd = *(int*)opaque;
	int infd = FiesFile_get_os_fd(infile);
	if (infd < 0)
		return (ssize_t)infd;
	off_t off = (off_t)pos;
	ssize_t put = sendfile(stream_fd, infd, &off, count);
	return put < 0 ? (fies_ssz)-errno : (fies_ssz)put;
}

const struct FiesWriter_Funcs
create_writer_funcs = {
	.writev   = wr_writev,
	.sendfile = wr_sendfile
};
