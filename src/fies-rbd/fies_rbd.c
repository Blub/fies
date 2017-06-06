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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wreserved-id-macro"
#pragma GCC diagnostic ignored "-Wc++98-compat-pedantic"
#pragma GCC diagnostic ignored "-Wdocumentation"
#pragma GCC diagnostic ignored "-Wdocumentation-unknown-command"
#include <glib.h>
#pragma GCC diagnostic pop

#include <rados/librados.h>
#include <rbd/librbd.h>

#include "../../lib/fies.h"
#include "../../lib/vector.h"
#include "../cli_common.h"
#include "../util.h"
#include "../regex.h"

static const char           *opt_file     = NULL;
static long                  opt_uid      = -1;
static long                  opt_gid      = -1;
static VectorOf(RexReplace*) opt_xform;
static const char           *opt_cephconf = NULL;
static bool                  opt_snapshots = true;

static bool option_error = false;

static rados_t gRados;
#pragma clang diagnostic ignored "-Wunused-function"

static GHashTable *gPools = NULL;

static const char *usage_msg =
"usage: fies-rbd [options] [-- ceph options [--]] {image | snapshot}...\n\
Options:\n"
#include "fies_rbd.options.h"
;

static _Noreturn void
usage(FILE *out, int exit_code)
{
	fprintf(out, "%s", usage_msg);
	exit(exit_code);
}

#define OPT_UID              (0x1100+'u')
#define OPT_GID              (0x1000+'g')
#define OPT_SNAPSHOTS        (0x1100+'s')
#define OPT_NO_SNAPSHOTS     (0x1000+'s')

static struct option longopts[] = {
	{ "help",                    no_argument, NULL, 'h' },
	{ "file",              required_argument, NULL, 'f' },
	{ "verbose",                 no_argument, NULL, 'v' },
	{ "uid",               required_argument, NULL, OPT_UID },
	{ "gid",               required_argument, NULL, OPT_GID },
	{ "transform",         required_argument, NULL, 's' },
	{ "xform",             required_argument, NULL, 's' },

	{ "config",            required_argument, NULL, 'c' },
	{ "snapshots",               no_argument, NULL, OPT_SNAPSHOTS },
	{ "nosnapshots",             no_argument, NULL, OPT_NO_SNAPSHOTS },
	{ "no-snapshots",            no_argument, NULL, OPT_NO_SNAPSHOTS },

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
			fprintf(stderr, "fies-rbd: %s\n", errstr);
			free(errstr);
			option_error = true;
		} else {
			Vector_push(&opt_xform, &xform);
		}
		break;
	}
	case OPT_UID:
		if (!arg_stol(oarg, &opt_uid, "--uid", "fies-rbd"))
			option_error = true;
		break;
	case OPT_GID:
		if (!arg_stol(oarg, &opt_gid, "--gid", "fies-rbd"))
			option_error = true;
		break;

	case 'c': opt_cephconf = oarg; break;

	case OPT_SNAPSHOTS:    opt_snapshots = true; break;
	case OPT_NO_SNAPSHOTS: opt_snapshots = false; break;

	case '?':
		fprintf(stderr, "fies-rbd: unrecognized option: %c\n",
		        oopt);
		usage(stderr, EXIT_FAILURE);
	default:
		fprintf(stderr, "fies-rbd: option error\n");
		usage(stderr, EXIT_FAILURE);
	}
}

// For each image we store a list of snapshots explicitly asked for via the
// positional parameters.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	char *name;
	bool include_self;
	GHashTable *snapshots;
	// device and "virtual" physical offset tracker
	fies_id device;
	fies_id last_file_id;
	size_t lastsize;
} Image;
#pragma clang diagnostic pop

static Image*
Image_new(const char *name, bool include_self, fies_id device)
{
	int err;
	Image *self = malloc(sizeof(*self));
	if (!self)
		return NULL;
	memset(self, 0, sizeof(*self));
	if (!(self->name = strdup(name)))
		goto out;
	self->snapshots =
		g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free);
	self->include_self = include_self;
	self->device = device;
	self->last_file_id = (fies_id)-1;
	self->lastsize = 0;
	return self;

out:
	err = errno;
	free(self->name);
	free(self);
	errno = err;
	return NULL;
}

static void
Image_delete(void *pself)
{
	Image *self = pself;
	g_hash_table_destroy(self->snapshots);
	free(self->name);
	free(self);
}

static int
Image_addSnapshot(Image *self, const char *snapname)
{
	char *name = strdup(snapname);
	if (!name)
		return -errno;
	g_hash_table_insert(self->snapshots, name, name);
	return 0;
}

static inline bool
Image_hasSnapshot(Image *self, const char *snapname)
{
	return g_hash_table_contains(self->snapshots, snapname);
}

static inline bool
Image_removeSnapshot(Image *self, const char *snapname)
{
	return g_hash_table_remove(self->snapshots, snapname);
}

// For each pool we need a rados_ioctx and images we want to sture
typedef struct {
	char *name;
	rados_ioctx_t ioctx;
	GHashTable *images;
} Pool;

static Pool*
Pool_new(const char *pool_name)
{
	int err;
	Pool *self = malloc(sizeof(*self));
	if (!self)
		return NULL;
	memset(self, 0, sizeof(*self));
	self->name = strdup(pool_name);
	if (!self->name)
		goto out_errno;
	// In my librados using rados_ioctx_create can crash in some
	// circumstances (particularly when not calling rados_connect first)
	// while rados_pool_lookup and rados_ioctx_create2 seem to work fine,
	// so I'll use this as it seems to be the safer choice...
	int64_t pid = rados_pool_lookup(gRados, pool_name);
	if (pid < 0) {
		err = (int)-pid;
		goto out;
	}
	err = -rados_ioctx_create2(gRados, pid, &self->ioctx);
	if (err > 0)
		goto out;
	self->images = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
	                                     Image_delete);
	return self;

out_errno:
	err = errno;
out:
	if (self->images)
		g_hash_table_destroy(self->images);
	free(self->name);
	free(self);
	errno = err;
	return NULL;
}

static void
Pool_delete(void *pself)
{
	Pool *self = pself;
	g_hash_table_destroy(self->images);
	rados_ioctx_destroy(self->ioctx);
	free(self);
}

static Image*
Pool_addImage(Pool *self, const char *name, bool is_self, FiesWriter *fies)
{
	Image *img = g_hash_table_lookup(self->images, name);
	if (img) {
		if (self)
			img->include_self = true;
		return img;
	}

	img = Image_new(name, is_self, FiesWriter_newDevice(fies));
	if (!img)
		return NULL;

	g_hash_table_insert(self->images, img->name, img);

	return img;
}

static int
cephrbd_add(FiesWriter *fies, const char *imgspec)
{
	char *poolname = NULL, *name = NULL, *snap = NULL;
	int err = EINVAL;

	char *slash = strchr(imgspec, '/');
	if (slash) {
		if (slash == imgspec) // starts with a slash
			goto out;

		size_t poollen = (size_t)(slash - imgspec);
		poolname = malloc(poollen + 1);
		if (!poolname)
			goto out_errno;
		memcpy(poolname, imgspec, poollen);
		poolname[poollen] = 0;

		// parse only the rest
		imgspec = slash+1;
	}

	char *at = strchr(imgspec, '@');
	if (at) {
		if (at == imgspec) // had no name
			goto out;

		size_t namelen = (size_t)(at-imgspec);
		name = malloc(namelen + 1);
		if (!name)
			goto out_errno;
		memcpy(name, imgspec, namelen);
		name[namelen] = 0;

		snap = strdup(at+1);
		if (!snap)
			goto out_errno;
	} else {
		name = strdup(imgspec);
		if (!name)
			goto out_errno;
	}

	const char *pn = poolname ? poolname : "rbd";

	Pool *pool = g_hash_table_lookup(gPools, pn);
	if (!pool) {
		pool = Pool_new(pn);
		if (!pool)
			goto out_errno;
		g_hash_table_insert(gPools, pool->name, pool);
	}

	Image *image = Pool_addImage(pool, name, snap == NULL, fies);
	if (!image)
		goto out_errno;

	if (snap) {
		err = Image_addSnapshot(image, snap);
		if (err < 0)
			goto out;
	}

	return 0;

out_errno:
	err = -errno;
out:
	free(poolname);
	free(name);
	free(snap);
	return err;
}

typedef struct {
	rbd_image_t rbdimg;
	Image *image;
	const char *previous_snapshot;
	FiesFile_Extent *buffer;
	size_t bufsize;
	size_t bufat;
	size_t lastend;
} RBDFile;

static int
RBDFile_extent_cb(const uint64_t off,
                  const size_t len,
                  int has_data,
                  void *opaque)
{
	FiesFile *handle = opaque;
	RBDFile *self = handle->opaque;

	// can we merge with the previous extent?
	FiesFile_Extent *ex;
	if (self->bufat > 0) {
		ex = &self->buffer[self->bufat-1];
		if (!(ex->flags & FIES_FL_COPY) &&
		    self->lastend == off &&
		    (!has_data == !(ex->flags & FIES_FL_DATA)))
		{
			ex->length += len;
			goto done;
		}
	}
	// If we cannot merge and we're at the end, cancel the
	// diff-iterate operation:
	if (self->bufat == self->bufsize)
		return -EAGAIN;

	ex = &self->buffer[self->bufat++];
	memset(ex, 0, sizeof(*ex));

	// Parts rbd-diff-iterate skips are inherited from the parent.
	if (self->lastend != off) {
		ex->logical = self->lastend;
		if (self->image->last_file_id == (fies_id)-1 ||
		    self->lastend >= self->image->lastsize)
		{
			// If we have no parent (or the parent was too small)
			// we punch holes.
			ex->flags = FIES_FL_HOLE;
			ex->length = off - self->lastend;
		} else {
			// If we have a parent we clone.
			ex->flags = FIES_FL_DATA | FIES_FL_COPY;
			ex->source.file = self->image->last_file_id;
			ex->source.offset = ex->logical;
			// But the parent might not fit
			if (self->image->lastsize < handle->filesize) {
				ex->length = handle->filesize - self->lastend;
				self->lastend = handle->filesize;
				if (self->bufat == self->bufsize)
					return -EAGAIN;
				ex = &self->buffer[self->bufat++];
				memset(ex, 0, sizeof(*ex));
				ex->logical = self->lastend;
				ex->flags = FIES_FL_HOLE;
			}
			ex->length = off - self->lastend;
		}
		self->lastend = off;
		if (self->bufat == self->bufsize)
			return -EAGAIN;
		ex = &self->buffer[self->bufat++];
		memset(ex, 0, sizeof(*ex));
	}

	ex->flags = has_data ? FIES_FL_DATA : FIES_FL_HOLE;
	ex->logical = off;
	ex->length = len;

done:
	self->lastend = off + len;
	return 0;
}

static ssize_t
RBDFile_nextExtents(FiesFile *handle,
                    FiesWriter *writer,
                    fies_pos logical_start,
                    FiesFile_Extent *buffer,
                    size_t count)
{
	(void)writer;
	RBDFile *self = handle->opaque;
	self->buffer = buffer;
	self->bufsize = count;
	self->bufat = 0;
	self->lastend = (size_t)logical_start;
	int rc = rbd_diff_iterate(self->rbdimg, self->previous_snapshot,
	                          logical_start,
	                          handle->filesize - logical_start,
	                          RBDFile_extent_cb, handle);
	if (rc < 0 && !(rc == -EAGAIN && self->bufat == self->bufsize)) {
		showerr("fies-rbd: rbd_diff_iterate: %s\n", strerror(-rc));
		return (ssize_t)rc;
	}

	// Fill it to the end:
	if (self->bufat != self->bufsize && self->lastend != handle->filesize)
	{
		FiesFile_Extent *ex = &self->buffer[self->bufat++];
		memset(ex, 0, sizeof(*ex));
		ex->logical = self->lastend;
		ex->length = handle->filesize - self->lastend;
		if (self->image->last_file_id != (fies_id)-1) {
			// by copying from the parent
			ex->flags = FIES_FL_DATA | FIES_FL_COPY;
			ex->source.file = self->image->last_file_id;
			ex->source.offset = ex->logical;
		} else {
			// or by creating an explicit hole
			ex->flags = FIES_FL_HOLE;
		}
	}
	return (ssize_t)self->bufat;
}

static ssize_t
RBDFile_pread(FiesFile *handle, void *buffer, size_t size, fies_pos off)
{
	RBDFile *self = handle->opaque;
	// XXX: once we can take clones into account we can send
	// LIBRADOS_OP_FLAG_FADVICE_NOCACHE via rbd_read2()
	return rbd_read(self->rbdimg, off, size, buffer);
}

static const struct FiesFile_Funcs
cephrbd_file_funcs = {
	.pread         = RBDFile_pread,
	.close         = NULL, // we don't allocate anything for this
	.next_extents  = RBDFile_nextExtents,
	//.get_mtime     = RBDFile_mtime, TODO: <-
};

static int
do_cephrbd_add(FiesWriter *fies,
               rbd_image_t rbdimg,
               Pool *pool,
               Image *image,
               const char *snapshot,
               size_t size,
               const char *previous_snapshot)
{
	if (snapshot) {
		verbose(VERBOSE_FILES, "%s/%s@%s\n",
		        pool->name, image->name, snapshot);
	} else {
		verbose(VERBOSE_FILES, "%s/%s\n",
		        pool->name, image->name);
	}

	int rc = rbd_snap_set(rbdimg, snapshot);
	if (rc < 0)
		return rc;

	size_t namelen = strlen(image->name);
	size_t snaplen = snapshot ? 1+strlen(snapshot) : 0;
	char *fullname = alloca(namelen + snaplen + 1);
	memcpy(fullname, image->name, namelen);
	if (snapshot) {
		fullname[namelen] = '@';
		memcpy(fullname+namelen+1, snapshot, snaplen);
		fullname[namelen+1+snaplen] = 0;
	} else {
		fullname[namelen] = 0;
	}

	char *xformed_name = apply_xform_vec(fullname, &opt_xform);
	if (!xformed_name)
		return -errno;

	RBDFile opaque = {
		.rbdimg = rbdimg,
		.image = image,
		.previous_snapshot = previous_snapshot
		// rest is filled later
	};
	FiesFile *file = FiesFile_new(&opaque, &cephrbd_file_funcs,
	                              xformed_name, NULL, size,
	                              FIES_M_FREG | 0644,
	                              image->device);
	int saved_errno = errno;
	free(xformed_name);
	errno = saved_errno;
	if (!file)
		return -errno;

	rc = FiesWriter_writeFile(fies, file);
	image->last_file_id = file->fileid;
	image->lastsize = size;
	FiesFile_close(file);

	return rc;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
static struct {
	int retval;
	Pool *pool;
} gStatus;
#pragma clang diagnostic pop

static void
report_missing(gpointer pname, gpointer pname2, gpointer userdata)
{
	(void)pname2;
	const char *snap = pname;
	const char *image = userdata;
	showerr("fies-rbd: no such snapshot: %s@%s\n", image, snap);
	gStatus.retval = -ENOENT;
}

static void
add_image(gpointer pname, gpointer pimage, gpointer userdata)
{
	(void)pname;
	if (gStatus.retval != 0)
		return;

	Pool *pool = gStatus.pool;
	Image *image = pimage;
	FiesWriter *fies = userdata;

	rbd_snap_info_t *snaps = NULL, *stemp;

	rbd_image_t rbdimg;
	int rc = rbd_open(pool->ioctx, image->name, &rbdimg, NULL);
	if (rc < 0) {
		gStatus.retval = rc;
		return;
	}

	size_t imgsize;
	rc = rbd_get_size(rbdimg, &imgsize);
	if (rc < 0) {
		gStatus.retval = rc;
		goto out;
	}

	int max_snaps = 64;
	int snap_count = -1;
	do {
		stemp = realloc(snaps, (size_t)max_snaps * sizeof(*snaps));
		if (!stemp) {
			gStatus.retval = -errno;
			free(snaps);
			goto out;
		}
		snaps = stemp;
		// rbd_snap_list updates max_snap
		snap_count = rbd_snap_list(rbdimg, snaps, &max_snaps);
	} while (snap_count == -ERANGE);
	if (snap_count < 0) {
		free(snaps);
		snaps = NULL;
		goto out;
	}

	int desired_count = 0;
	if (image->include_self) {
		desired_count = snap_count;
	} else {
		// if we don't need the image we need to limit the snapshots
		// we want to pass to the latest snapshot in the command line,
		// so find the latest one:
		for (int i = 0; i != snap_count; ++i) {
			if (Image_hasSnapshot(image, snaps[i].name))
				desired_count = i+1;
		}
	}

	const char *previous_snapshot = NULL;

	// Now go through as many snapshots as we may possibly need, if it
	// was either requested explicitly or -bsnapshots=yes was set, add it.
	for (int i = 0; i != desired_count; ++i) {
		if (Image_removeSnapshot(image, snaps[i].name) ||
		    opt_snapshots)
		{
			rc = do_cephrbd_add(fies, rbdimg, pool, image,
			                    snaps[i].name, snaps[i].size,
			                    previous_snapshot);
			if (rc < 0) {
				gStatus.retval = rc;
				goto out;
			}
			previous_snapshot = snaps[i].name;
		}
	}

	// If the image was also listed without a snapshot, add the last state
	// as well.
	if (image->include_self) {
		rc = do_cephrbd_add(fies, rbdimg, pool, image, NULL, imgsize,
		                    previous_snapshot);
		if (rc < 0) {
			gStatus.retval = rc;
			goto out;
		}
	}

	g_hash_table_foreach(image->snapshots, report_missing, image->name);

out:
	if (snaps) {
		rbd_snap_list_end(snaps);
		free(snaps);
	}
	rbd_close(rbdimg);
}

static void
add_pool(gpointer pname, gpointer ppool, gpointer userdata)
{
	(void)pname;
	if (gStatus.retval != 0)
		return;
	gStatus.pool = ppool;
	FiesWriter *fies = userdata;
	g_hash_table_foreach(gStatus.pool->images, add_image, fies);
}

static int
add_all(FiesWriter *fies)
{
	g_hash_table_foreach(gPools, add_pool, fies);
	return gStatus.retval;
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

static const struct FiesWriter_Funcs
writer_funcs = {
	.writev = writer_writev
};

int
main(int argc, char **argv)
{
	const char *errstr = NULL;

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

	argc -= optind;
	argv += optind;
	if (!argc) {
		fprintf(stderr, "fies-rbd: missing volume names\n");
		usage(stderr, EXIT_FAILURE);
	}

	int rc = rados_create(&gRados, NULL);
	if (rc < 0) {
		fprintf(stderr, "fies-rbd: rados_create: %s\n",
		        strerror(-rc));
		return 1;
	}

	rc = rados_conf_read_file(gRados, opt_cephconf);
	if (rc < 0) {
		fprintf(stderr, "fies-rbd: rados_conf_read_file: %s\n",
		        strerror(-rc));
		rados_shutdown(gRados);
		return 1;
	}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
	rc = rados_conf_parse_argv_remainder(gRados, argc,
	                                     (const char**)argv,
	                                     (const char**)argv);
#pragma clang diagnostic pop
	if (rc < 0) {
		fprintf(stderr, "fies-rbd: ceph parameters: %s\n",
		        strerror(-rc));
		rados_shutdown(gRados);
		return 1;
	}
	bool check = true;
	for (argc = 0; argv[argc]; ++argc) {
		if (!strcmp(argv[argc], "--"))
			check = false;
		else if (check && argv[argc][0] == '-') {
			fprintf(stderr,
			        "fies-rbd: unknown ceph parameter: %s\n",
			        argv[argc]);
			rados_shutdown(gRados);
			return 1;
		}
	}
	if (!argc) {
		fprintf(stderr, "fies-rbd: missing volume names\n");
		return 1;
	}

	rc = rados_connect(gRados);
	if (rc < 0) {
		fprintf(stderr, "fies-rbd: rados_connect: %s\n",
		        strerror(-rc));
		rados_shutdown(gRados);
		return 1;
	}

	int stream_fd = STDOUT_FILENO;
	if (opt_file && strcmp(opt_file, "-")) {
		stream_fd = open(opt_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (stream_fd < 0) {
			fprintf(stderr, "fies-rbd: open(%s): %s\n",
			        opt_file, strerror(errno));
			return 1;
		}
	}

	FiesWriter *fies = FiesWriter_new(&writer_funcs, &stream_fd);
	if (!fies) {
		fprintf(stderr, "fies-rbd: failed to initialize: %s\n",
		        strerror(errno));
		close(stream_fd);
		return 1;
	}

	gPools = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
	                               Pool_delete);

	for (int i = 0; i != argc; ++i) {
		rc = cephrbd_add(fies, argv[i]);
		if (rc < 0)
			goto out_err;
	}
	rc = add_all(fies);
	if (rc < 0)
		goto out_err;

	goto out;

out_err:
	errstr = FiesWriter_getError(fies);
	if (!errstr)
		errstr = strerror(-rc);
	fprintf(stderr, "fies-rbd: %s\n", errstr);
out:
	FiesWriter_delete(fies);
	g_hash_table_destroy(gPools);
	rados_shutdown(gRados);
	close(stream_fd);
	return rc == 0 ? 1 : 0;
}
