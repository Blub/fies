#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/ioctl.h>

#include <linux/fs.h>
#include <libdevmapper.h>

#include "../../lib/fies.h"
#include "../../lib/vector.h"
#include "../cli_common.h"
#include "../util.h"
#include "../regex.h"
#include "fies_dmthin.h"

static const char           *opt_file    = NULL;
static long                  opt_uid     = -1;
static long                  opt_gid     = -1;
static VectorOf(RexReplace*) opt_xform;

static bool option_error = false;

static const char *usage_msg =
"usage: fies-dmthin [options] volumes...\n\
Options:\n"
#include "fies_dmthin.options.h"
;

static _Noreturn void
usage(FILE *out, int exit_code)
{
	fprintf(out, "%s", usage_msg);
	exit(exit_code);
}

#define OPT_UID              (0x1100+'u')
#define OPT_GID              (0x1000+'g')

static struct option longopts[] = {
	{ "help",                    no_argument, NULL, 'h' },
	{ "file",              required_argument, NULL, 'f' },
	{ "verbose",                 no_argument, NULL, 'v' },
	{ "uid",               required_argument, NULL, OPT_UID },
	{ "gid",               required_argument, NULL, OPT_GID },
	{ "transform",         required_argument, NULL, 's' },
	{ "xform",             required_argument, NULL, 's' },
	{ NULL, 0, NULL, 0 }
};

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
			fprintf(stderr, "fies-dmthin: %s\n", errstr);
			free(errstr);
			option_error = true;
		} else {
			Vector_push(&opt_xform, &xform);
		}
		break;
	}
	case OPT_UID:
		if (!arg_stol(oarg, &opt_uid, "--uid", "fies-dmthin"))
			option_error = true;
		break;
	case OPT_GID:
		if (!arg_stol(oarg, &opt_gid, "--gid", "fies-dmthin"))
			option_error = true;
		break;
	case '?':
		fprintf(stderr, "fies-dmthin: unrecognized option: %c\n",
		        oopt);
		usage(stderr, EXIT_FAILURE);
	default:
		fprintf(stderr, "fies-dmthin: option error\n");
		usage(stderr, EXIT_FAILURE);
	}
}

char*
GetDMName(dev_t dev)
{
	char buf[1024];
	int rc = snprintf(buf, sizeof(buf), "/sys/dev/block/%d:%d/dm/name",
	                  major(dev), minor(dev));
	if (rc <= 0) {
		errno = EOVERFLOW;
		return NULL;
	}
	int fd = open(buf, O_RDONLY);
	if (fd < 0)
		return NULL;
	ssize_t got = read(fd, buf, sizeof(buf));
	int saved_errno = errno;
	close(fd);
	if (got <= 0) {
		errno = saved_errno;
		return NULL;
	} else {
		while (got > 0 && buf[got-1] == '\n')
			--got;
		if (!got) {
			errno = EBADF;
			return NULL;
		}
	}
	return strndup(buf, (size_t)got);
}

static char*
DMNameFromDevice(const char *name_or_path)
{
	struct stat stbuf;
	if (stat(name_or_path, &stbuf) != 0) {
		if (errno == ENOENT && !strchr(name_or_path, '/'))
			return strdup(name_or_path);
		return NULL;
	}
	if (!S_ISBLK(stbuf.st_mode)) {
		errno = ENODEV;
		return NULL;
	}
	return GetDMName(stbuf.st_rdev);
}

bool
DMMessage(const char *dmname, const char *message)
{
	struct dm_task *task = dm_task_create(DM_DEVICE_TARGET_MSG);
	//uint32_t cookie = 0;
	if (!task) {
		errno = ENOMEM;
		return false;
	}
	(void)dm_task_secure_data(task);
	if (!dm_task_set_name(task, dmname) ||
	    !dm_task_set_message(task, message) ||
	    !dm_task_set_sector(task, 0) ||
	    !dm_task_no_open_count(task))
	{
		dm_task_destroy(task);
		errno = EINVAL;
		return false;
	}

	//if (dm_cookie_supported() &&
	//    !dm_task_set_cookie(task, &cookie, DM_COOKIE_AUTO_CREATE))
	//{
	//	dm_task_destroy(task);
	//	errno = EINVAL;
	//	return false;
	//}
	if (!dm_task_run(task)) {
		dm_task_destroy(task);
		errno = ENODEV;
		return false;
	}
	return true;
}

#if 0
static bool
DMSuspend(const char *dmname, bool on)
{
	int err = EFAULT;
	bool rv = false;
	uint32_t cookie = 0;
	struct dm_task *task =
		dm_task_create(on ? DM_DEVICE_SUSPEND : DM_DEVICE_RESUME);
	if (!task) {
		errno = ENOMEM;
		return false;
	}
	(void)dm_task_secure_data(task);
	if (!dm_task_set_name(task, dmname) ||
	    !dm_task_no_open_count(task) ||
	    !dm_task_skip_lockfs(task))
	{
		goto out;
	}

	// otherwise we race against udev's removing and adding of
	// /dev/mapper/* entries
	if (dm_cookie_supported() &&
	    !dm_task_set_cookie(task, &cookie, DM_COOKIE_AUTO_CREATE))
	{
		goto out;
	}

	rv = true;
	if (!dm_task_run(task))
		rv = false;
	else
		err = 0;

out:
	dm_task_destroy(task);
	errno = err;
	return rv;
}
#endif

static inline struct dm_task*
DMTableTask(const char *dmname, char **type, char **params)
{
	struct dm_task *task = dm_task_create(DM_DEVICE_TABLE);
	if (!task) {
		errno = ENOMEM;
		return NULL;
	}
	(void)dm_task_secure_data(task);
	if (!dm_task_set_name(task, dmname)) {
		dm_task_destroy(task);
		errno = EINVAL;
		return NULL;
	}
	if (!dm_task_run(task)) {
		dm_task_destroy(task);
		errno = ENODEV;
		return NULL;
	}

	uint64_t start = 0, length = 0;
	(void)dm_get_next_target(task, NULL, &start, &length, type, params);
	return task;
}

bool
DMThinPoolInfo(const char *dmname,
               dev_t *metadev,
               dev_t *datadev,
               unsigned int *datablocksectors)
{
	int err;
	char *target_type = NULL;
	char *params = NULL;
	struct dm_task *task = DMTableTask(dmname, &target_type, &params);
	if (!task)
		return false;
	if (!target_type || strcmp(target_type, "thin-pool")) {
		err = -EINVAL;
		goto out;
	}

	unsigned int metamajor = 0, metaminor = 0;
	unsigned int datamajor = 0, dataminor = 0;
	unsigned int dbs = 0, lwm = 0;
	// thin-pool: <meta dev> <data dev> <data block size in sectors>
	// <low water mark>
	if (sscanf(params, "%d:%d %d:%d %d %d",
	           &metamajor, &metaminor, &datamajor, &dataminor,
	           &dbs, &lwm) != 6)
	{
		err = -EBADF;
		goto out;
	}
	if (metadev)
		*metadev = makedev(metamajor, metaminor);
	if (datadev)
		*datadev = makedev(datamajor, dataminor);
	if (datablocksectors)
		*datablocksectors = dbs;
	(void)lwm;

	err = 0;
out:
	dm_task_destroy(task);
	errno = err;
	return err == 0;
}

static bool
DMThinInfo(const char *dmname, dev_t *pooldev, unsigned int *devid)
{
	int err;
	char *target_type = NULL;
	char *params = NULL;
	struct dm_task *task = DMTableTask(dmname, &target_type, &params);
	if (!task)
		return false;
	if (!target_type || strcmp(target_type, "thin")) {
		err = -EINVAL;
		goto out;
	}

	unsigned int poolmajor = 0, poolminor = 0;
	unsigned int dummy = 0;
	if (!devid)
		devid = &dummy;
	// thin: <pool dev> <dev id> [<external origin dev>]
	// we don't care about the last value
	if (sscanf(params, "%d:%d %d", &poolmajor, &poolminor, devid) != 3) {
		err = -EBADF;
		goto out;
	}

	if (pooldev)
		*pooldev = makedev(poolmajor, poolminor);

	err = 0;

out:
	dm_task_destroy(task);
	errno = err;
	return err == 0;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct DMThinVolume {
	char *volname;
	ThinMeta *meta;
	unsigned int devid;
	int fd;
	bool suspended;
} DMTV;
#pragma clang diagnostic pop

static void
DMTV_destroy(DMTV *self)
{
	if (self->suspended) {
		ThinMeta_release(self->meta);
		//if (!DMSuspend(self->volname, false))
		//	fprintf(stderr, "failed to resume volume %s\n",
		//	        self->volname);
	}
	free(self->volname);
	if (self->fd >= 0)
		close(self->fd);
	free(self);
}

static void
DMTV_close(FiesFile *handle)
{
	DMTV_destroy(handle->opaque);
}

static ssize_t
DMTV_pread(FiesFile *handle, void *buffer, size_t size, fies_pos off)
{
	DMTV *self = handle->opaque;
	return pread(self->fd, buffer, size, (off_t)off);
}

static int
DMTV_os_fd(FiesFile *handle)
{
	DMTV *self = handle->opaque;
	return self->fd;
}

//static inline bool
//DMTV_loadMeta(DMTV *self, size_t block)
//{
//	ssize_t got = pread(self->meta->fd,
//	                    self->metablock, self->meta->blocksize,
//	                    DMTVBLK(self, block));
//	return (size_t)got == self->meta->blocksize;
//}

static ssize_t
DMTV_nextExtents(FiesFile *handle,
                 FiesWriter *writer,
                 fies_pos logical_start,
                 FiesFile_Extent *buffer,
                 size_t count)
{
	(void)writer;
	DMTV *self = handle->opaque;
	return ThinMeta_map(self->meta, self->devid,
	                    logical_start, buffer, count);
}

static const struct FiesFile_Funcs
dmthin_file_funcs = {
	.close         = DMTV_close,
	.pread         = DMTV_pread,
	.next_extents  = DMTV_nextExtents,
	//.verify_extent = DMTV_verifyExtent,
	.get_os_fd     = DMTV_os_fd,
};

static FiesFile*
DMThinVolume_open(const char *volume_or_device,
                  FiesWriter *writer,
                  GHashTable *thin_metadevs)
{
	int saved_errno, err;
	const char *errstr = NULL;

	DMTV *self = u_malloc0(sizeof(*self));
	if (!self)
		return NULL;

	self->fd = -1;

	// Get the volume name, input could already be the name, but could also
	// be a device node. We need a name to query the device mapper.

	self->volname = DMNameFromDevice(volume_or_device);
	if (!self->volname) {
		errstr = "failed to get volume name";
		goto out;
	}

	// Get information about the thin device: we need to know the pool
	// name in order to send messages to it, and to find the metadata
	// and data volumes it is composed of.
	dev_t pooldev = 0;
	if (!DMThinInfo(self->volname, &pooldev, &self->devid)) {
		errstr = "failed to get thin volume info";
		goto out;
	}

	char *tmppoolname = GetDMName(pooldev);
	if (!tmppoolname) {
		errstr = "failed to get thin pool name";
		goto out;
	}

	// Now that we have a pool we can associate ourselves with a FiesWriter
	// device for that pool to make sure volumes get fies_clone packets
	// iff they're on the same thin pool.
	self->meta = ThinMetaTable_addPool(thin_metadevs,
	                                   tmppoolname,
	                                   0, writer);
	saved_errno = errno;
	free(tmppoolname);
	errno = saved_errno;
	if (!self->meta) {
		errstr = "failed to register metadata pool";
		goto out;
	}

	// Open the volume before suspending it
	char *path = make_path("/dev/mapper", self->volname, NULL);
	self->fd = open(path, O_RDONLY);
	saved_errno = errno;
	free(path);
	errno = saved_errno;
	if (self->fd < 0) {
		errstr = "failed to open volume device";
		goto out;
	}

	unsigned long bdev512secs = 0;
	if (ioctl(self->fd, BLKGETSIZE, &bdev512secs) != 0) {
		errstr = "failed to get size of thin data volume";
		goto out;
	}

	//if (!DMSuspend(self->volname, true)) {
	//	errstr = "failed to suspend volume";
	//	goto out;
	//}
	self->suspended = true;
	if (!ThinMeta_reserve(self->meta)) {
		errstr = "failed to reserve metadata snapshot";
		goto out;
	}

	char *xformed_name = apply_xform_vec(self->volname, &opt_xform);
	if (!xformed_name) {
		errstr = "failed to apply `--xform` to volume name";
		goto out;
	}
	FiesFile *file = FiesFile_new(self, &dmthin_file_funcs,
	                              xformed_name, NULL, bdev512secs*512,
	                              FIES_M_FREG | 0600,
	                              self->meta->fid);
	saved_errno = errno;
	free(xformed_name);
	if (!file) {
		errno = saved_errno;
		goto out;
	}
	return file;

out:
	err = errno;
	FiesWriter_setError(writer, err, errstr);
	DMTV_destroy(self);
	errno = err;
	return NULL;
}

static void
main_cleanup()
{
	Vector_destroy(&opt_xform);
}

static ssize_t
writer_writev(void *opaque, const struct iovec *iov, size_t count)
{
	int stream_fd = *(int*)opaque;
	ssize_t put = writev(stream_fd, iov, (int)count);
	return put < 0 ? -errno : put;
}

static const struct FiesWriter_Funcs writer_funcs = {
	.writev = writer_writev
};

int
main(int argc, char **argv)
{
	int err = 0;
	const char *errstr = NULL;
	const char *errnostr = NULL;

	Vector_init_type(&opt_xform, RexReplace*);
	Vector_set_destructor(&opt_xform, (Vector_dtor*)RexReplace_pdestroy);

	atexit(main_cleanup);

	while (true) {
		int index = 0;
		int c = getopt_long(argc, argv, "hvf:s:", longopts, &index);
		if (c == -1)
			break;
		handle_option(c, optopt, optarg);
	}
	if (option_error)
		usage(stderr, EXIT_FAILURE);

	if (optind >= argc) {
		fprintf(stderr, "fies-dmthin: missing volume names\n");
		usage(stderr, EXIT_FAILURE);
	}

	int stream_fd = STDOUT_FILENO;
	if (opt_file && strcmp(opt_file, "-")) {
		stream_fd = open(opt_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (stream_fd < 0) {
			fprintf(stderr, "fies-dmthin: open(%s): %s\n",
			        opt_file, strerror(errno));
			return 1;
		}
	}

	FiesWriter *fies = FiesWriter_new(&writer_funcs, &stream_fd);
	if (!fies) {
		fprintf(stderr, "fies-dmthin: failed to initialize: %s\n",
		        strerror(errno));
		close(stream_fd);
		return 1;
	}

	GHashTable *dmthin_metadevs = ThinMetaTable_new();

	for (int i = optind; i != argc; ++i) {
		const char *arg = argv[i];
		FiesFile *file = DMThinVolume_open(arg, fies, dmthin_metadevs);
		if (!file)
			goto out_errno;
		verbose(VERBOSE_FILES, "%s\n", arg);
		err = -FiesWriter_writeFile(fies, file);
		FiesFile_close(file);
		if (err > 0)
			goto out_err;
	}

	err = 0;
	goto out;

out_errno:
	err = errno;
	errnostr = strerror(err);
out_err:
	errstr = FiesWriter_getError(fies);
	if (errstr)
		fprintf(stderr, "fies-dmthin: %s", errstr);
	if (errnostr)
		fprintf(stderr, ": %s\n", errnostr);
	else
		fprintf(stderr, "\n");
out:
	ThinMetaTable_delete(dmthin_metadevs);
	FiesWriter_delete(fies);
	close(stream_fd);
	return err ? 1 : 0;
}
