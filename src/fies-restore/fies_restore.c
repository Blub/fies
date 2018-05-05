#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/wait.h>
#include <assert.h>

#include <sys/ioctl.h>
#include <linux/fs.h>

#include "../../lib/fies.h"
#include "../../lib/fies_linux.h"
#include "../../lib/vector.h"
#include "../cli_common.h"
#include "../util.h"
#include "../fies_regex.h"
#include "../filematch.h"

static const char           *opt_file    = NULL;
static VectorOf(RexReplace*) opt_xform;
static VectorOf(FileMatch)   opt_exclude;
static VectorOf(FileMatch)   opt_include;
static char  **opt_cmd_open        = NULL;
static char  **opt_cmd_snapshot    = NULL;
static char  **opt_cmd_close       = NULL;
static char  **opt_cmd_resize      = NULL;
static char  **opt_cmd_create      = NULL;
static char   *opt_devname         = NULL;
static char   *opt_filename        = NULL;
static bool    opt_final_snapshot  = true;
static bool    opt_wildcards_slash = false;

static bool option_error = false;

static const char *usage_msg =
"usage: fies-restore [options] volumes...\n\
Options:\n"
#include "fies_restore.options.h"
;

static _Noreturn void
usage(FILE *out, int exit_code)
{
	fprintf(out, "%s", usage_msg);
	exit(exit_code);
}

#define OPT_FINAL_SNAP         (0x1100+'s')
#define OPT_NO_FINAL_SNAP      (0x1000+'s')
#define OPT_EXCLUDE            (0x2000+'x')
#define OPT_INCLUDE            (0x2000+'i')
#define OPT_REXCLUDE           (0x2100+'x')
#define OPT_RINCLUDE           (0x2100+'i')
#define OPT_WILD_SLASH         (0x1200+'w')
#define OPT_NO_WILD_SLASH      (0x2200+'w')

static struct option longopts[] = {
	{ "help",                     no_argument, NULL, 'h' },
	{ "file",               required_argument, NULL, 'f' },
	{ "verbose",                  no_argument, NULL, 'v' },
	{ "transform",          required_argument, NULL, 's' },
	{ "xform",              required_argument, NULL, 's' },

	{ "create",             required_argument, NULL, 'C' },
	{ "open",               required_argument, NULL, 'o' },
	{ "close",              required_argument, NULL, 'c' },
	{ "snapshot",           required_argument, NULL, 'S' },
	{ "resize",             required_argument, NULL, 'R' },

	{ "final-snapshot",           no_argument, NULL, OPT_FINAL_SNAP },
	{ "nofinal-snapshot",         no_argument, NULL, OPT_NO_FINAL_SNAP },
	{ "no-final-snapshot",        no_argument, NULL, OPT_NO_FINAL_SNAP },

	{ "exclude",            required_argument, NULL, OPT_EXCLUDE },
	{ "rexclude",           required_argument, NULL, OPT_REXCLUDE },
	{ "include",            required_argument, NULL, OPT_INCLUDE },
	{ "rinclude",           required_argument, NULL, OPT_RINCLUDE },
	{ "wildcards-match-slash",    no_argument, NULL, OPT_WILD_SLASH },
	{ "no-wildcards-match-slash", no_argument, NULL, OPT_NO_WILD_SLASH},

	{ NULL, 0, NULL, 0 }
};

static void
command_opt(char ***dst, const char *arg)
{
	if (!*arg) {
		// empty means no command
		u_strfreev(*dst);
		*dst = NULL;
	}
	*dst = split_command(arg);
}

static void
handle_option(int c, int oopt, const char *oarg)
{
	switch (c) {
	case 'h': usage(stdout, EXIT_SUCCESS);
	case 'v': ++common.verbose; break;
	case 'f': opt_file = oarg; break;
	case 's': {
		char *errstr = NULL;
		RexReplace *xform = RexReplace_new(oarg, &errstr);
		if (!xform) {
			fprintf(stderr, "fies-restore: %s\n", errstr);
			free(errstr);
			option_error = true;
		} else {
			Vector_push(&opt_xform, &xform);
		}
		break;
	}
	case 'C': command_opt(&opt_cmd_create,   oarg); break;
	case 'o': command_opt(&opt_cmd_open,     oarg); break;
	case 'c': command_opt(&opt_cmd_close,    oarg); break;
	case 'S': command_opt(&opt_cmd_snapshot, oarg); break;
	case 'R': command_opt(&opt_cmd_resize,   oarg); break;

	case OPT_FINAL_SNAP:     opt_final_snapshot = true; break;
	case OPT_NO_FINAL_SNAP:  opt_final_snapshot = false; break;
	case OPT_WILD_SLASH:     opt_wildcards_slash = true; break;
	case OPT_NO_WILD_SLASH:  opt_wildcards_slash = false; break;

	case OPT_EXCLUDE: {
		FileMatch entry = {
			.flags = 0,
			.glob = oarg
		};
		Vector_push(&opt_exclude, &entry);
		break;
	}
	case OPT_REXCLUDE:
		if (!handle_file_re_opt(oarg, &opt_exclude, "fies-restore"))
			option_error = true;
		break;
	case OPT_INCLUDE: {
		int flags = opt_wildcards_slash ? FMATCH_WILDCARD_SLASH : 0;
		FileMatch entry = {
			.flags = flags,
			.glob = oarg
		};
		Vector_push(&opt_include, &entry);
		break;
	}
	case OPT_RINCLUDE:
		if (!handle_file_re_opt(oarg, &opt_include, "fies-restore"))
			option_error = true;
		break;

	case '?':
		fprintf(stderr, "fies-restore: unrecognized option: %c\n",
		        oopt);
		usage(stderr, EXIT_FAILURE);
	default:
		fprintf(stderr, "fies-restore: option error\n");
		usage(stderr, EXIT_FAILURE);
	}
}

static bool
path_matches(const char *path, VectorOf(FileMatch) *vec)
{
	FileMatch *it;
	Vector_foreach(vec, it) {
		if (FileMatch_matches(it, path, 0))
			return true;
	}
	return false;
}

static bool
opt_is_snapshot_excluded(const char *name)
{
	if (Vector_empty(&opt_exclude) && Vector_empty(&opt_include))
		return false;
	if (!Vector_empty(&opt_include) && !path_matches(name, &opt_include))
		return true;
	return path_matches(name, &opt_exclude);
}

static int     stream_fd         = STDIN_FILENO;
static char   *snap_lastname     = NULL;
static size_t  snap_size         = 0;
static int     snap_outfd        = -1;
static bool    snap_cur_done     = false;
static char   *snap_cur_handle   = NULL;

static unsigned int snap_discard_zeroes = (unsigned int)-1;
static int snap_can_punch_hole = -1;
static int snap_can_zero_range = -1;

static bool
snap_run_command(char **template_args, const char *size_arg)
{
	if (!template_args)
		return true;

	char **argv = strvec_replace((const char*const*)template_args,
	                             "%s", snap_lastname,
	                             "%n", opt_devname,
	                             "%f", opt_filename,
	                             "%z", size_arg,
	                             NULL);

	if (common.verbose) {
		fprintf(stderr, "Running command" );
		for (size_t i = 0; argv[i]; ++i)
			fprintf(stderr, " '%s'", argv[i]);
		fprintf(stderr, "\n");
	}
	pid_t cld = fork();
	if (cld == -1) {
		showerr("fies-restore: fork failed: %s\n", strerror(errno));
		return false;
	}

	if (!cld) {
		// don't leak fds to eg. lvm calls
		if (stream_fd != STDIN_FILENO)
			close(stream_fd);
		close(snap_outfd);
		execvp(argv[0], argv);
		showerr("fies-restore: exec(%s): %s\n",
		        argv[0], strerror(errno));
		exit(EXIT_FAILURE);
	}
	u_strfreev(argv);

	int ret = 0;
	while (waitpid(cld, &ret, 0) != cld) {
		showerr("fies-restore: wait: %s\n", strerror(errno));
		kill(cld, SIGKILL);
		return false;
	}
	if (WEXITSTATUS(ret) != 0) {
		showerr("fies-restore: command failed\n");
		return false;
	}
	return true;
}

static int
snap_close_output()
{
	close(snap_outfd);
	snap_outfd = -1;
	if (!snap_run_command(opt_cmd_close, NULL))
		return -EFAULT;
	return 0;
}

static int
snap_open_output(int extra_mode)
{
	if (!snap_run_command(opt_cmd_open, NULL))
		return -EFAULT;
	snap_outfd = open(opt_filename, O_WRONLY | extra_mode, 0666);
	if (snap_outfd < 0) {
		int rc = -errno;
		showerr("fies-restore: open(%s): %s\n",
		        opt_filename, strerror(errno));
		return rc;
	}
	return 0;
}

static int
snap_take_snapshot(bool final)
{
	bool reopen = (opt_cmd_open || opt_cmd_close);

	fsync(snap_outfd);
	if (final || reopen) {
		int rc = snap_close_output();
		if (rc < 0)
			return rc;
	}

	if (final && !opt_final_snapshot)
		return 0;

	assert(opt_cmd_snapshot != NULL);
	if (!snap_lastname || !snap_lastname[0])
		warn(WARN_GENERIC, "fies-restore: empty snapshot name\n");
	else if (opt_is_snapshot_excluded(snap_lastname))
		verbose(VERBOSE_ACTIONS, "not creating snapshot %s\n",
		        snap_lastname);
	else if (!snap_run_command(opt_cmd_snapshot, NULL))
		return -EFAULT;

	if (!final && reopen)
		return snap_open_output(0);
	return 0;
}

static ssize_t
snap_read(void *opaque, void *data, size_t count)
{
	int *pfd = opaque;
	ssize_t got = read(*pfd, data, count);
	return got < 0 ? -errno : got;
}

static int
snap_create(void *opaque,
          const char *in_filename,
          size_t size,
          uint32_t mode,
          void **handle)
{
	(void)opaque;
	(void)mode;

	int retval = -EFAULT;
	char *filename = opt_apply_xform(in_filename, &opt_xform);
	if (!filename)
		return -errno;

	verbose(VERBOSE_FILES, "%s\n", filename);

	if (snap_lastname) {
		if (!snap_cur_done) {
			showerr("fies-restore: previous snapshot not done yet,"
			        " not a snapshot stream\n");
			goto out;
		}
		if (size != snap_size) {
			// FIXME: just add a `resize` option to allow this
			// instead of erroring.
			showerr("fies-restore: volume size changed\n");
			goto out;
		}
		retval = snap_take_snapshot(false);
		if (retval < 0)
			goto out;
	} else {
		snap_size = size;
		if (opt_cmd_create) {
			char szbuf[128];
			snprintf(szbuf, sizeof(szbuf), "%zd", snap_size);
			if (!snap_run_command(opt_cmd_create, szbuf))
				goto out;
		}
		retval = snap_open_output(O_CREAT);
		if (retval < 0)
			goto out;
	}
	retval = -EFAULT;
	if (opt_cmd_resize) {
		char szbuf[128];
		snprintf(szbuf, sizeof(szbuf), "%zd", snap_size);
		if (!snap_run_command(opt_cmd_resize, szbuf))
			goto out;
	} else if (ftruncate(snap_outfd, (off_t)snap_size) != 0) {
		retval = -errno;
		showerr("fies-restore: truncate(%s): %s\n",
			opt_filename, strerror(errno));
		goto out;
	}

	snap_cur_done = false;
	snap_lastname = filename;
	*handle = ++snap_cur_handle;
	return 0;
out:
	free(filename);
	return retval;
}

static int
snap_reference(void *opaque,
               const char *in_filename,
               size_t size,
               uint32_t mode,
               void **handle)
{
	(void)opaque;
	(void)size;
	(void)mode;
	(void)handle;

	char *filename = opt_apply_xform(in_filename, &opt_xform);
	if (!filename)
		return -errno;
	verbose(VERBOSE_FILES, "%s\n", filename);
	free(filename);

	// FIXME: should/could verify the size here...
	return 0;
}

static int
snap_mkdir(void *opaque, const char *dirname, uint32_t mode, void **handle)
{
	(void)opaque;
	(void)dirname;
	(void)mode;
	(void)handle;
	showerr("fies-restore: found a directory, not a snapshot stream");
	return -EBADF;
}

static int
snap_mknod(void *opaque,
         const char *filename,
         uint32_t mode,
         uint32_t major_id,
         uint32_t minor_id,
         void **handle)
{
	(void)opaque;
	(void)filename;
	(void)mode;
	(void)major_id;
	(void)minor_id;
	(void)handle;
	showerr("fies-restore: found a device node, not a snapshot stream");
	return -EBADF;
}

static int
snap_symlink(void *opaque, const char *file, const char *target, void **handle)
{
	(void)opaque;
	(void)file;
	(void)target;
	(void)handle;
	showerr("fies-restore: found a symlink, not a snapshot stream");
	return -EBADF;
}

static int
snap_chown(void *opaque, void *pfd, uid_t uid, gid_t gid)
{
	(void)opaque;
	(void)pfd;
	(void)uid;
	(void)gid;
	return 0;
}

static int
snap_set_mtime(void *opaque, void *pfd, struct fies_time time)
{
	(void)opaque;
	(void)pfd;
	(void)time;
	return 0;
}

static int
snap_discard(void *opaque, void *out, fies_pos off, fies_sz length, bool zero)
{
	(void)opaque;
	(void)out;

	uint64_t range[2] = { off, (uint64_t)length };
	if (snap_discard_zeroes == (unsigned int)-1) {
		if (ioctl(snap_outfd, BLKDISCARDZEROES, &snap_discard_zeroes)
		    != 0)
		{
			snap_discard_zeroes = 0;
		}
		if (snap_discard_zeroes)
			verbose(VERBOSE_ACTIONS, "=> discard zeroes\n");
		else
			verbose(VERBOSE_ACTIONS, "=> discard does not zero\n");
	}

	unsigned long request;
	if (zero && !snap_discard_zeroes) {
		verbose(VERBOSE_ACTIONS, "using BLKZEROOUT\n");
		request = BLKZEROOUT;
	} else {
		verbose(VERBOSE_ACTIONS, "using BLKDISCARD\n");
		request = BLKDISCARD;
	}

	int rc = ioctl(snap_outfd, request, range);
	if (rc != 0)
		return -errno;
	return 0;
}

static int
snap_punch_hole(void *opaque, void *out, fies_pos off, fies_sz length)
{
	(void)opaque;
	(void)out;

	int rc, mode;
	if (snap_can_punch_hole == 0)
		goto try_discard;

	mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;
	verbose(VERBOSE_ACTIONS, "zero-out: %zx : %zx\n", off, length);
	if (fallocate(snap_outfd, mode, (off_t)off, (off_t)length) == 0) {
		snap_can_punch_hole = 1;
		return 0;
	}

	if (snap_can_punch_hole < 0 &&
	    (errno == ENODEV || errno == ENOTSUP || errno == EOPNOTSUPP))
	{
		snap_can_punch_hole = 0;
		verbose(VERBOSE_ACTIONS, "not a regular file => DISCARD\n");
	 try_discard:
		rc = snap_discard(opaque, out, off, length, false);
	} else {
		return -errno;
	}
	if (rc < 0)
		return rc;
	return 0;
}

static ssize_t
snap_make_zero(void *opaque, void *out, fies_pos pos, fies_sz count)
{
	int rc, mode;
	if (snap_can_zero_range == 0) {
		if (snap_can_punch_hole == 0)
			goto try_discard;
		goto try_punch_hole;
	}

	mode = FALLOC_FL_ZERO_RANGE | FALLOC_FL_KEEP_SIZE;
	verbose(VERBOSE_ACTIONS, "zero-out: %zx : %zx\n", pos, count);
	if (fallocate(snap_outfd, mode, (off_t)pos, (off_t)count) == 0) {
		snap_can_zero_range = 1;
		return (ssize_t)count;
	}

	if (errno == ENODEV) {
		snap_can_zero_range = 0;
		verbose(VERBOSE_ACTIONS, "not a regular file => BLKZEROOUT\n");
	 try_discard:
		rc = snap_discard(opaque, out, pos, count, true);
	} else if (errno == ENOTSUP || errno == EOPNOTSUPP) {
		snap_can_zero_range = 0;
		verbose(VERBOSE_ACTIONS, "zero-out failed, punching hole\n");
	 try_punch_hole:
		rc = snap_punch_hole(opaque, out, pos, count);
	} else {
		return -errno;
	}
	if (rc < 0)
		return rc;
	return (ssize_t)count;
}

static fies_ssz
snap_pwrite(void *opaque,
            void *out,
            const void *buf,
            fies_sz count,
            fies_pos pos)
{
	(void)opaque;
	if (out != snap_cur_handle) {
		showerr("fies-restore: encountered write to old target,"
		        " not a snapshot stream\n");
		return -EBADF;
	}
	if (!buf)
		return snap_make_zero(opaque, out, pos, count);
	return (fies_ssz)pwrite(snap_outfd, buf, (size_t)count, (off_t)pos);
}

static int
snap_clone(void *opaque,
           void *dst, fies_pos dstoff,
           void *src, fies_pos srcoff,
           fies_sz len)
{
	(void)opaque;
	(void)dst;
	(void)dstoff;
	(void)srcoff;
	(void)len;
	if (dst != snap_cur_handle) {
		showerr("fies-restore: encountered an out-of-order clone,"
		        " not a snapshot stream\n");
		return -EBADF;
	}
	if ((char*)src >= snap_cur_handle) {
		showerr("fies-restore: clone from bad file handle, "
		        "not a snapshot stream\n");
		return -EBADF;
	}
	return 0;
}

static int
snap_file_done(void *opaque, void *fh)
{
	(void)opaque;
	(void)fh;
	snap_cur_done = true;
	return 0;
}

static const struct FiesReader_Funcs
reader_funcs = {
	.read       = snap_read,
	.create     = snap_create,
	.reference  = snap_reference,
	.mkdir      = snap_mkdir,
	.symlink    = snap_symlink,
	.mknod      = snap_mknod,
	.chown      = snap_chown,
	.set_mtime  = snap_set_mtime,
	.pwrite     = snap_pwrite,
	.punch_hole = snap_punch_hole,
	.clone      = snap_clone,
	.file_done  = snap_file_done,
};

static void
main_cleanup()
{
	Vector_destroy(&opt_exclude);
	Vector_destroy(&opt_include);
	Vector_destroy(&opt_xform);
	u_strfreev(opt_cmd_create);
	u_strfreev(opt_cmd_resize);
	u_strfreev(opt_cmd_open);
	u_strfreev(opt_cmd_snapshot);
	u_strfreev(opt_cmd_close);
}

int
main(int argc, char **argv)
{
	Vector_init_type(&opt_exclude, FileMatch);
	Vector_set_destructor(&opt_exclude,
	                      (Vector_dtor*)FileMatch_destroy);

	Vector_init_type(&opt_include, FileMatch);
	Vector_set_destructor(&opt_include,
	                      (Vector_dtor*)FileMatch_destroy);

	Vector_init_type(&opt_xform, RexReplace*);
	Vector_set_destructor(&opt_xform, (Vector_dtor*)RexReplace_pdestroy);

	atexit(main_cleanup);

	while (true) {
		int index = 0;
		int c = getopt_long(argc, argv, "hvf:s:C:o:c:S:R:", longopts,
		                    &index);
		if (c == -1)
			break;
		handle_option(c, optopt, optarg);
	}
	if (option_error)
		usage(stderr, EXIT_FAILURE);

	if (!opt_cmd_snapshot) {
		fprintf(stderr,
		"fies-restore: --snapshot option is mandatory\n");
		return 1;
	}

	argc -= optind;
	argv += optind;
	if (!argc) {
		fprintf(stderr, "fies-restore: missing file name\n");
		return 1;
	}
	opt_filename = opt_devname = argv[0];
	if (argc == 2) {
		opt_devname = argv[1];
	} else if (argc > 2) {
		fprintf(stderr, "fies-restore: too many parameters\n");
		return 1;
	}

	if (opt_file && strcmp(opt_file, "-")) {
		stream_fd = open(opt_file, O_RDONLY);
		if (stream_fd < 0) {
			fprintf(stderr, "fies-restore: open(%s): %s\n",
			        opt_file, strerror(errno));
			return 1;
		}
	}

	FiesReader *fies = FiesReader_new(&reader_funcs, &stream_fd);
	if (!fies) {
		fprintf(stderr, "fies-restore: failed to initialize: %s\n",
		        strerror(errno));
		close(stream_fd);
		return 1;
	}
	
	int rc;
	do {
		rc = FiesReader_iterate(fies);
	} while (rc > 0);
	FiesReader_delete(fies);
	if (rc < 0) {
		warn(WARN_GENERIC, "fies-restore: %s\n", strerror(-rc));
		showerr("fies-restore: %s\n", FiesReader_getError(fies));
	}
	int fin = snap_take_snapshot(true);
	if (fin < 0) {
		showerr("fies-restore: %s\n", strerror(-fin));
		rc = fin;
	}

	close(stream_fd);
	return rc == 0 ? 0 : 1;
}
