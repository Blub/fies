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
#include <assert.h>

#include <linux/fs.h>
#include <libdevmapper.h>

#include "../../lib/fies.h"
#include "../../lib/vector.h"
#include "../../lib/util.h"

#ifdef FIES_MAJOR_MACRO_HEADER
# include FIES_MAJOR_MACRO_HEADER
#endif

#include "../cli_common.h"
#include "../util.h"
#include "../fies_regex.h"
#include "fies_dmthin.h"

static const char           *opt_file            = NULL;
static long                  opt_uid             = -1;
static long                  opt_gid             = -1;
static VectorOf(RexReplace*) opt_xform;
static bool                  opt_incremental     = false;
static const char           *opt_snapshot_list   = NULL;
static const char           *opt_data_device     = NULL;
static const char           *opt_metadata_device = NULL;

static bool option_error = false;

static const char *usage_msg =
"usage: fies-dmthin [options] volumes...\n\
Options:\n"
#include "fies-dmthin.options.h"
;

static _Noreturn void
usage(FILE *out, int exit_code)
{
	fprintf(out, "%s", usage_msg);
	exit(exit_code);
}

#define OPT_UID              (0x1100+'u')
#define OPT_GID              (0x1000+'g')
#define OPT_INCREMENTAL      (0x1100+'i')
#define OPT_NO_INCREMENTAL   (0x1000+'i')
#define OPT_SNAPSHOT_LIST    (0x1000+'L')
#define OPT_DATA_DEVICE      (0x1000+'d')
#define OPT_METADATA_DEVICE  (0x1000+'m')

static struct option longopts[] = {
	{ "help",                    no_argument, NULL, 'h' },
	{ "file",              required_argument, NULL, 'f' },
	{ "verbose",                 no_argument, NULL, 'v' },
	{ "uid",               required_argument, NULL, OPT_UID },
	{ "gid",               required_argument, NULL, OPT_GID },
	{ "transform",         required_argument, NULL, 's' },
	{ "xform",             required_argument, NULL, 's' },
	{ "incremental",             no_argument, NULL, OPT_INCREMENTAL },
	{ "noincremental",           no_argument, NULL, OPT_NO_INCREMENTAL },
	{ "no-incremental",          no_argument, NULL, OPT_NO_INCREMENTAL },
	{ "snapshot-list",     required_argument, NULL, OPT_SNAPSHOT_LIST },
	{ "data-device",       required_argument, NULL, OPT_DATA_DEVICE },
	{ "metadata-device",   required_argument, NULL, OPT_METADATA_DEVICE },
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
	case OPT_INCREMENTAL:     opt_incremental = true; break;
	case OPT_NO_INCREMENTAL:  opt_incremental = false; break;
	case OPT_SNAPSHOT_LIST:   opt_snapshot_list = oarg; break;
	case OPT_DATA_DEVICE:     opt_data_device = oarg; break;
	case OPT_METADATA_DEVICE: opt_metadata_device = oarg; break;
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
	                                   writer);
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
	if (!ThinMeta_loadRoot(self->meta, true)) {
		errstr = "failed to reserve or load metadata snapshot";
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
	file->uid = (uint32_t)opt_uid;
	file->gid = (uint32_t)opt_gid;
	return file;

out:
	err = errno;
	FiesWriter_setError(writer, err, errstr);
	DMTV_destroy(self);
	errno = err;
	return NULL;
}

typedef struct RawDMThinVolume {
	ThinMeta *meta;
	int data_fd;
	unsigned int devid;
	fies_sz size;
} RawDMTV;

static void
RawDMTV_destroy(RawDMTV *self)
{
	// Don't close the data fd...
	free(self);
}

static void
RawDMTV_close(FiesFile *handle)
{
	RawDMTV_destroy(handle->opaque);
}

static ssize_t
RawDMTV_preadp(FiesFile *handle, void *buffer, size_t size, fies_pos off,
               fies_pos physical)
{
	(void)off;
	RawDMTV *self = handle->opaque;
	// Since extents have been mapped prior to calling this method, it
	// should only actually be called for *existing* extents, meaning
	// we don't need to map them and we may assume that they exist.
	return pread(self->data_fd, buffer, size, (off_t)physical);
}

static ssize_t
RawDMTV_nextExtents(FiesFile *handle,
                    FiesWriter *writer,
                    fies_pos logical_start,
                    FiesFile_Extent *buffer,
                    size_t count)
{
	(void)writer;
	RawDMTV *self = handle->opaque;
	return ThinMeta_map(self->meta, self->devid,
	                    logical_start, buffer, count);
}

static const struct FiesFile_Funcs
raw_dmthin_file_funcs = {
	.close         = RawDMTV_close,
	.preadp        = RawDMTV_preadp,
	.next_extents  = RawDMTV_nextExtents,
	//.verify_extent = DMTV_verifyExtent,
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
struct RawDeviceOpt {
	const char *name;
	unsigned int devid;
	fies_sz size;
};
#pragma clang diagnostic pop
static VectorOf(struct RawDeviceOpt) raw_device_entries;

static FiesFile*
OpenThinVolume(const struct RawDeviceOpt *entry,
               FiesWriter *writer,
               ThinMeta *meta,
               int data_fd)
{
	char *xformed_name = apply_xform_vec(entry->name, &opt_xform);
	if (!xformed_name) {
		int saved_errno = errno;
		FiesWriter_setError(writer, saved_errno,
		                    "failed to apply `--xform` to device id");
		errno = saved_errno;
		return NULL;
	}

	RawDMTV *self = u_malloc0(sizeof(*self));
	if (!self) {
		int saved_errno = errno;
		free(xformed_name);
		FiesWriter_setError(writer, saved_errno,
		                    "failed to allocate memory");
		errno = saved_errno;
		return NULL;
	}
	self->devid = entry->devid;
	self->meta = meta;
	self->data_fd = data_fd;

	FiesFile *file = FiesFile_new(self, &raw_dmthin_file_funcs,
	                              xformed_name, NULL, entry->size,
	                              FIES_M_FREG | 0600,
	                              self->meta->fid);
	int saved_errno = errno;
	free(xformed_name);
	if (!file) {
		FiesWriter_setError(writer, saved_errno, NULL);
		RawDMTV_destroy(self);
		errno = saved_errno;
		return NULL;
	}
	file->uid = (uint32_t)opt_uid;
	file->gid = (uint32_t)opt_gid;

	return file;
}

static GHashTable *gThinMetaDevices = NULL;
static ThinMeta *gRawMetaDevice = NULL;

static void
cleanupDevices()
{
	// After a terminating signal atexit() should still be called and then
	// we can use this to cleanup after reserved metadata snapshots.
	if (gThinMetaDevices) {
		ThinMetaTable_delete(gThinMetaDevices);
		gThinMetaDevices = NULL;
	}
	if (gRawMetaDevice) {
		ThinMeta_delete(gRawMetaDevice);
		gRawMetaDevice = NULL;
	}
}

static void
handleSignal()
{
	fprintf(stderr, "fies-dmthin: caught signal\n");
	cleanupDevices();
	exit(-1);
}

static void
cleanupMain()
{
	cleanupDevices();
	Vector_destroy(&opt_xform);
	Vector_destroy(&raw_device_entries);
}

static ssize_t
writer_writev(void *opaque, const struct iovec *iov, size_t count)
{
	int stream_fd = *(int*)opaque;
	ssize_t put = writev(stream_fd, iov, (int)count);
	return put < 0 ? -errno : put;
}

static const struct FiesWriter_Funcs
writer_funcs = {
	.writev = writer_writev
};

static bool
is_wsp(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
	       c == '\f' || c == '\v';
}

static char*
ParseSnapshotLine(const char *line)
{
	// Trim front
	while (is_wsp(*line))
		++line;

	// skip comments and empty lines
	if (!*line || *line == '#') {
		errno = 0;
		return NULL;
	}

	// Trim back
	size_t len = strlen(line);
	if (len && is_wsp(line[len-1]))
		--len;

	// decode mtree escaping:
	size_t size = fies_mtree_decode(NULL, 0, line, len);
	if (!size) {
		errno = EINVAL;
		return NULL;
	}
	char *out = malloc(size+1);
	if (!out) {
		errno = ENOMEM;
		return NULL;
	}
	fies_mtree_decode(out, size+1, line, len);
	return out;
}

static int
SendSnapshotList(FiesWriter *fies, FiesFile *filehandle, const char *listfile)
{
	FILE *fh = fopen(listfile, "rb");
	if (!fh) {
		fprintf(stderr, "fies-dmthin: failed to open %s: %s",
		        listfile, strerror(errno));
		return -1;
	}

	Vector snapshots;
	Vector_init_type(&snapshots, char*);
	Vector_set_destructor(&snapshots, (Vector_dtor*)&u_strptrfree);

	char *line = NULL;
	size_t line_alloc = 0;

	int rc = 0;

	size_t lno = 0;
	while (getline(&line, &line_alloc, fh) != -1) {
		++lno;
		char *name = ParseSnapshotLine(line);
		if (!name) {
			if (errno)
				break;
			continue;
		}
		Vector_push(&snapshots, &name);
		errno = 0;
	}
	int saved_errno = errno;
	free(line);
	line = NULL;
	if (saved_errno) {
		fprintf(stderr, "fies-dmthin: error reading from %s: %s\n",
		        listfile, strerror(saved_errno));
		rc = -1;
		goto out;
	}

	FiesWriter_snapshots(fies, filehandle,
	                     Vector_data(&snapshots),
	                     Vector_length(&snapshots));

out:
	free(line);
	fclose(fh);
	Vector_destroy(&snapshots);
	return rc;
}

static bool
parseRawThinEntry(struct RawDeviceOpt *dev, const char *entry)
{
	errno = 0;
	unsigned long long tmp;
	char *endp = NULL;

	errno = 0;
	tmp = strtoull(entry, &endp, 0);
	if (errno || !endp) {
		fprintf(stderr, "fies-dmthin: expected 'id:size', not: %s\n",
		        entry);
		return false;
	}
	if (*endp != ':' || !endp[1]) {
		fprintf(stderr, "fies-dmthin: expected 'id:size', not: %s\n",
		        entry);
		return false;
	}
	if (tmp > UINT_MAX) {
		fprintf(stderr, "fies-dmthin: id too large: %llu\n", tmp);
		return false;
	}
	entry = endp+1;
	dev->devid = (unsigned int)tmp;

	errno = 0;
	endp = NULL;
	tmp = strtoull(entry, &endp, 0);
	if (errno || !endp) {
		fprintf(stderr, "fies-dmthin: bad size: %s\n", entry);
		return false;
	}
	int skip = multiply_size(&tmp, endp);
	if (skip < 0 || endp[skip]) {
		fprintf(stderr, "fies-dmthin: bad size in: %s\n", entry);
		return false;
	}
	dev->size = (fies_sz)tmp;
	return true;
}

static int
handleAndCloseFile(FiesWriter *fies, FiesFile *file)
{
	int err = 0;
	if (opt_incremental) {
		opt_incremental = false;
		err = FiesWriter_readRefFile(fies, file);
	} else {
		err = FiesWriter_writeFile(fies, file);
	}
	if (!err && opt_snapshot_list) {
		err = SendSnapshotList(fies, file, opt_snapshot_list);
		opt_snapshot_list = NULL;
	}
	FiesFile_close(file);
	return err;
}

int
main(int argc, char **argv)
{
	int err = 0;
	const char *errstr = NULL;
	const char *errnostr = NULL;

	Vector_init_type(&opt_xform, RexReplace*);
	Vector_set_destructor(&opt_xform, (Vector_dtor*)RexReplace_pdestroy);

	Vector_init_type(&raw_device_entries, struct RawDeviceOpt);

	atexit(cleanupMain);
	signal(SIGINT, handleSignal);
	signal(SIGTERM, handleSignal);
	signal(SIGQUIT, handleSignal);

	while (true) {
		int index = 0;
		int c = getopt_long(argc, argv, "hvf:s:", longopts, &index);
		if (c == -1)
			break;
		handle_option(c, optopt, optarg);
	}
	if (option_error)
		usage(stderr, EXIT_FAILURE);

	if (opt_uid == -1)
		opt_uid = getuid();
	if (opt_gid == -1)
		opt_gid = getgid();

	if (!opt_data_device != !opt_metadata_device) {
		fprintf(stderr,
		        "fies-dmthin: must specify both --data-device and "
		        "--metadata-device\n");
		return 1;
	}

	if (optind >= argc) {
		fprintf(stderr, "fies-dmthin: missing volume names\n");
		usage(stderr, EXIT_FAILURE);
	}

	// Verify this early:

	int data_fd = -1;
	if (opt_data_device) {
		data_fd = open(opt_data_device, O_RDONLY);
		if (data_fd < 0) {
			fprintf(stderr,
			        "fies-dmthin: cannot open: %s: %s\n",
			        opt_data_device, strerror(errno));
			return 1;
		}

		bool has_error = false;
		for (int i = optind; i != argc; ++i) {
			struct RawDeviceOpt entry;
			if (parseRawThinEntry(&entry, argv[i])) {
				entry.name = argv[i];
				Vector_push(&raw_device_entries, &entry);
			} else {
				has_error = true;
			}
		}
		if (has_error) {
			close(data_fd);
			return 1;
		}
	}

	int stream_fd = STDOUT_FILENO;
	if (opt_file && strcmp(opt_file, "-")) {
		stream_fd = open(opt_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (stream_fd < 0) {
			fprintf(stderr, "fies-dmthin: open(%s): %s\n",
			        opt_file, strerror(errno));
			if (data_fd >= 0)
				close(data_fd);
			return 1;
		}
	}

	FiesWriter *fies = FiesWriter_new(&writer_funcs, &stream_fd);
	if (!fies) {
		fprintf(stderr, "fies-dmthin: failed to initialize: %s\n",
		        strerror(errno));
		close(stream_fd);
		if (data_fd >= 0)
			close(data_fd);
		return 1;
	}

	if (opt_metadata_device) {
		assert(opt_data_device);
		gRawMetaDevice = ThinMeta_new(opt_metadata_device, "<pool>",
		                 fies, true);
		if (!gRawMetaDevice) {
			fprintf(stderr,
			    "fies-dmthin: failed to open metadata device\n");
			goto out_err;
		}
		if (!ThinMeta_loadRoot(gRawMetaDevice, false)) {
			fprintf(stderr, "fies-dmthin: "
			        "failed to load metadata superblock\n");
			goto out_err;
		}
		struct RawDeviceOpt *entry;
		Vector_foreach(&raw_device_entries, entry) {
			FiesFile *file =
			    OpenThinVolume(entry, fies, gRawMetaDevice,
			                   data_fd);
			if (!file)
				goto out_errno;
			verbose(VERBOSE_FILES, "%s\n", entry->name);
			err = handleAndCloseFile(fies, file);
			if (err != 0)
				goto out_errno;
		}
	} else {
		gThinMetaDevices = ThinMetaTable_new();

		for (int i = optind; i != argc; ++i) {
			const char *arg = argv[i];
			FiesFile *file =
			    DMThinVolume_open(arg, fies, gThinMetaDevices);
			if (!file)
				goto out_errno;
			verbose(VERBOSE_FILES, "%s\n", arg);
			err = handleAndCloseFile(fies, file);
			if (err != 0)
				goto out_errno;
		}
	}

	err = 0;
	goto out;

out_errno:
	err = errno;
	errnostr = strerror(err);
out_err:
	errstr = FiesWriter_getError(fies);
	if (errstr) {
		fprintf(stderr, "fies-dmthin: %s", errstr);
		if (errnostr)
			fprintf(stderr, ": %s\n", errnostr);
		else
			fprintf(stderr, "\n");
	} else if (errnostr) {
		fprintf(stderr, "fies-dmthin: %s\n", errnostr);
	}
out:
	ThinMetaTable_delete(gThinMetaDevices);
	gThinMetaDevices = NULL;
	ThinMeta_delete(gRawMetaDevice);
	gRawMetaDevice = NULL;
	FiesWriter_delete(fies);
	close(stream_fd);
	if (data_fd >= 0)
		close(data_fd);
	return err ? 1 : 0;
}
