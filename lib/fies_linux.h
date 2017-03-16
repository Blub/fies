#ifndef FIES_SRC_FIES_LINUX_H
#define FIES_SRC_FIES_LINUX_H

#include <linux/fs.h>
#include <linux/fiemap.h>

#ifndef FICLONE
#  define FICLONE		_IOW(0x94, 9, int)
#endif
#ifndef FICLONERANGE
struct file_clone_range {
	__s64 src_fd;
	__u64 src_offset;
	__u64 src_length;
	__u64 dest_offset;
};
#  define FICLONERANGE	_IOW(0x94, 13, struct file_clone_range)
#endif

#ifndef FALLOC_FL_ZERO_RANGE
# define FALLOC_FL_ZERO_RANGE 0x10
#endif

#ifndef ENOATTR
# define ENOATTR ENODATA
#endif
typedef int (*xattr_cb)(const char *name, const char *value, void*);
int fd_forxattrs(int fd, xattr_cb cb, void *userdata);
char* fd_getxattr(int fd, const char *name, char **pbuffer, size_t *size);

#endif
