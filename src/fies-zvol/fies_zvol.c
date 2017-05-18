// libzfs' CFLAGS screw with the standard includes, so...:
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpedantic"
#pragma clang diagnostic ignored "-Wmissing-noreturn"
#pragma clang diagnostic ignored "-Wpadded"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmacro-redefined"
#pragma clang diagnostic ignored "-Wreserved-id-macro"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>

#include <libzfs.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_pool.h>
#include <sys/dbuf.h>

#include "../../lib/fies.h"
#include "../../lib/map.h"

#include "../cli_common.h"
#include "../util.h"
#include "../regex.h"
#pragma clang diagnostic pop

#define ERR_SKIPMSG 1
#define ITER_END 2

static const char           *opt_file      = NULL;
static long                  opt_uid       = -1;
static long                  opt_gid       = -1;
static VectorOf(RexReplace*) opt_xform;
static bool                  opt_snapshots = true;
static bool                  opt_set_ro    = false;
static bool                  opt_ignore_rw = false;
static const char           *opt_from      = NULL;
static const char           *opt_to        = NULL;

static bool option_error = false;

static MapOf(char*, fies_id) pool_devs;
static MapOf(char*, nothing) zvols_done;

static libzfs_handle_t *zfs = NULL;

static const char *usage_msg =
"usage: fies-zvol [options] [--] {zvol|zvol-snapshot}...\n\
Options:\n"
#include "fies_zvol.options.h"
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
#define OPT_SET_RO           (0x1100+'r')
#define OPT_NO_SET_RO        (0x1000+'r')
#define OPT_IGNORE_RW        (0x1100+'w')
#define OPT_NO_IGNORE_RW     (0x1000+'w')
#define OPT_FROM_SNAPSHOT    (0x1000+'F')
#define OPT_TO_SNAPSHOT      (0x1000+'T')

static struct option longopts[] = {
	{ "help",                    no_argument, NULL, 'h' },
	{ "file",              required_argument, NULL, 'f' },
	{ "verbose",                 no_argument, NULL, 'v' },
	{ "uid",               required_argument, NULL, OPT_UID },
	{ "gid",               required_argument, NULL, OPT_GID },
	{ "transform",         required_argument, NULL, 's' },
	{ "xform",             required_argument, NULL, 's' },

	{ "snapshots",               no_argument, NULL, OPT_SNAPSHOTS },
	{ "nosnapshots",             no_argument, NULL, OPT_NO_SNAPSHOTS },
	{ "no-snapshots",            no_argument, NULL, OPT_NO_SNAPSHOTS },

	{ "from-snapshot",     required_argument, NULL, OPT_FROM_SNAPSHOT },
	{ "to-snapshot",       required_argument, NULL, OPT_TO_SNAPSHOT },

	{ "setro",                   no_argument, NULL, OPT_SET_RO },
	{ "set-ro",                  no_argument, NULL, OPT_SET_RO },
	{ "no-setro",                no_argument, NULL, OPT_NO_SET_RO },
	{ "no-set-ro",               no_argument, NULL, OPT_NO_SET_RO },

	{ "rw",                      no_argument, NULL, OPT_IGNORE_RW },
	{ "norw",                    no_argument, NULL, OPT_NO_IGNORE_RW },
	{ "no-rw",                   no_argument, NULL, OPT_NO_IGNORE_RW },

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
			fprintf(stderr, "fies-zvol: %s\n", errstr);
			free(errstr);
			option_error = true;
		} else {
			Vector_push(&opt_xform, &xform);
		}
		break;
	}
	case OPT_UID:
		if (!arg_stol(oarg, &opt_uid, "--uid", "fies-zvol"))
			option_error = true;
		break;
	case OPT_GID:
		if (!arg_stol(oarg, &opt_gid, "--gid", "fies-zvol"))
			option_error = true;
		break;

	case OPT_SNAPSHOTS:    opt_snapshots = true;  break;
	case OPT_NO_SNAPSHOTS: opt_snapshots = false; break;
	case OPT_SET_RO:       opt_set_ro = true;     break;
	case OPT_NO_SET_RO:    opt_set_ro = false;    break;
	case OPT_IGNORE_RW:    opt_ignore_rw = true;  break;
	case OPT_NO_IGNORE_RW: opt_ignore_rw = false; break;

	case OPT_FROM_SNAPSHOT: opt_from = oarg; break;
	case OPT_TO_SNAPSHOT:   opt_to = oarg;   break;

	case '?':
		fprintf(stderr, "fies-zvol: unrecognized option: %c\n",
		        oopt);
		usage(stderr, EXIT_FAILURE);
	default:
		fprintf(stderr, "fies-zvol: option error\n");
		usage(stderr, EXIT_FAILURE);
	}
}

static void
main_cleanup()
{
	Vector_destroy(&opt_xform);
	Map_destroy(&pool_devs);
	Map_destroy(&zvols_done);
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	int fd;
	dnode_t *dn;
	zbookmark_phys_t zb;
	fies_pos uniq;
} ZVOL;
#pragma clang diagnostic pop

static fies_id
fies_dev_for_zpool(FiesWriter *fies, const char *pool)
{
	fies_id *existing = Map_get(&pool_devs, &pool);
	if (existing)
		return *existing;
	fies_id devid = FiesWriter_newDevice(fies);
	char *pooldup = strdup(pool);
	if (!pooldup) {
		fprintf(stderr, "fies-zvol: %s\n", strerror(errno));
		return devid;
	}
	Map_insert(&pool_devs, &pooldup, &devid);
	return devid;
}

static const struct FiesFile_Funcs zvol_file_funcs;
static FiesFile*
ZVOL_open(FiesWriter *writer,
          const char *pool,
          const char *volume,
          dnode_t *dn,
          uint64_t volsize)
{
	int err;
	ZVOL *self = NULL;
	char *filename = NULL;
	int fd = -1;

	filename = make_path("/dev/zvol", pool, volume, NULL);
	if (!filename)
		return NULL;
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		err = errno;
		fprintf(stderr, "fies-zvol: failed to open %s\n", filename);
		goto out;
	}

	free(filename);
	filename = make_path(pool, volume, NULL);
	if (!filename)
		goto out_errno;
	char *tmp = apply_xform_vec(filename, &opt_xform);
	if (!tmp)
		goto out_errno;
	free(filename);
	filename = tmp;

	self = malloc(sizeof(*self));
	if (!self)
		goto out_errno;
	self->uniq = 0;
	self->fd = fd;
	self->dn = dn;
	dnode_phys_t *dnphy = dn->dn_phys;
	SET_BOOKMARK(&self->zb, dmu_objset_id(dn->dn_objset),
	             dn->dn_object, dnphy->dn_nlevels - 1, 0);

	FiesFile *file = FiesFile_new(self, &zvol_file_funcs,
	                              filename, NULL, volsize,
	                              FIES_M_FREG | 0600,
	                              fies_dev_for_zpool(writer, pool));
	if (!file)
		goto out_errno;

	return file;
out_errno:
	err = errno;
out:
	free(filename);
	free(self);
	if (fd >= 0)
		close(fd);
	errno = err;
	return NULL;
}

static void
ZVOL_close(FiesFile *handle)
{
	ZVOL *self = handle->opaque;
	close(self->fd);
}

static ssize_t
ZVOL_pread(FiesFile *handle, void *buffer, size_t size, fies_pos off)
{
	ZVOL *self = handle->opaque;
	return pread(self->fd, buffer, size, (off_t)off);
}

static int
ZVOL_os_fd(FiesFile *handle)
{
	ZVOL *self = handle->opaque;
	return self->fd;
}

static uint64_t
blkid2offset(const dnode_phys_t *dnphy,
             const blkptr_t *bp,
             const zbookmark_phys_t *zb)
{
	if (dnphy == NULL) {
		if (!zb->zb_objset)
			return zb->zb_blkid;
		return zb->zb_blkid * BP_GET_LSIZE(bp);
	}

	uint64_t indblk = dnphy->dn_indblkshift - SPA_BLKPTRSHIFT;
	uint64_t lvlshift = (uint64_t)zb->zb_level * indblk;
	uint64_t zblk = zb->zb_blkid << lvlshift;

	return (zblk * dnphy->dn_datablkszsec) << SPA_MINBLOCKSHIFT;
}

static int
ZVOL_mapBP(ZVOL *self,
           FiesFile_Extent *buffer,
           size_t *bufat,
           size_t count,
           const dnode_phys_t *dnphy,
           const blkptr_t *bp,
           const zbookmark_phys_t *zb)
{
	fies_id vdev;
	fies_pos pos;
	fies_pos lsize = BP_GET_LSIZE(bp);
	fies_pos logical = blkid2offset(dnphy, bp, zb);

	bool sharable = !BP_COUNT_GANG(bp) && !BP_IS_EMBEDDED(bp);
	if (sharable) {
		int ndvas = BP_GET_NDVAS(bp);
		// We always use the lowest-numbered DVA for blocks found on
		// multiple vdevs:
		const dva_t *dva = bp->blk_dva;
		int low_i = 0;
		u_longlong_t low_vdev = DVA_GET_VDEV(&dva[0]);
		for (int i = 1; i < ndvas; ++i) {
			u_longlong_t zvdev = DVA_GET_VDEV(&dva[i]);
			if (zvdev < low_vdev) {
				low_vdev = zvdev;
				low_i = i;
			}
		}
		vdev = (fies_id)low_vdev;
		pos = (fies_pos)DVA_GET_OFFSET(&dva[low_i]);
	} else {
		vdev = (fies_id)-1;
		pos = self->uniq;
		self->uniq += lsize;
	}


	if (*bufat) {
		FiesFile_Extent *prev = &buffer[*bufat-1];
		fies_pos prev_phy_end = prev->physical + prev->length;
		fies_pos prev_log_end = prev->logical + prev->length;
		if (prev->device == vdev &&
		    prev_phy_end == pos &&
		    prev_log_end == logical)
		{
			prev->length += lsize;
			return 0;
		}
	}
	if (*bufat == count)
		return ENOBUFS;
	FiesFile_Extent *ex = &buffer[(*bufat)++];
	memset(ex, 0, sizeof(*ex));
	ex->device = vdev;
	ex->physical = pos;
	ex->logical = logical;
	ex->length = lsize;
	ex->flags = FIES_FL_DATA;
	if (sharable)
		ex->flags |= FIES_FL_SHARED;
	return 0;
}

static int
ZVOL_mapLevel(ZVOL *self,
              uint64_t logical_start,
              FiesFile_Extent *buffer,
              size_t *bufat,
              size_t count,
              spa_t *spa,
              const dnode_phys_t *dnphy,
              const blkptr_t *bp,
              const zbookmark_phys_t *zb)
{
	if (bp->blk_birth == 0 || BP_IS_HOLE(bp))
		return 0;
	if (BP_GET_LEVEL(bp) == 0)
		return ZVOL_mapBP(self, buffer, bufat, count, dnphy, bp, zb);

	arc_flags_t flags = ARC_FLAG_WAIT;
	arc_buf_t *buf;
	int err = arc_read(NULL, spa, bp, arc_getbuf_func, &buf,
	                   ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL,
	                   &flags, zb);
	if (err)
		return -err;
	if (!buf->b_data) {
		fprintf(stderr, "fies-zvol: arc buffer data is NULL\n");
		return -EFAULT;
	}

	blkptr_t *next_blkptrs = buf->b_data;
	unsigned long bpcount = BP_GET_LSIZE(bp) >> SPA_BLKPTRSHIFT;
	long a = 0, b = (long)bpcount;
	if (!b) {
		arc_buf_remove_ref(buf, &buf);
		return 0;
	}

	zbookmark_phys_t nextzb;
	SET_BOOKMARK(&nextzb, zb->zb_objset, zb->zb_object,
	             zb->zb_level - 1,
	             zb->zb_blkid * bpcount + 0);
	if (logical_start) {
		while (b-a > 1) {
			long i = (a+b)/2;
			nextzb.zb_blkid = zb->zb_blkid * bpcount
			                  + (unsigned long)i;
			blkptr_t *nbp = &next_blkptrs[i];
			uint64_t off = blkid2offset(dnphy, nbp, &nextzb);
			if (logical_start > off)
				a = i;
			else if (logical_start < off)
				b = i;
			else
				a = b = i;
		}
	}

	// Don't binary-search the level 0 blocks:
	if (BP_GET_LEVEL(bp) == 1)
		logical_start = 0;

	for (unsigned long i = (unsigned long)a; i < bpcount; ++i) {
		nextzb.zb_blkid = zb->zb_blkid * bpcount + i;
		blkptr_t *nbp = &next_blkptrs[i];
		err = ZVOL_mapLevel(self, logical_start, buffer, bufat, count,
		                    spa, dnphy, nbp, &nextzb);
		if (err)
			break;
	}
	arc_buf_remove_ref(buf, &buf);
	return err;
}

static ssize_t
ZVOL_nextExtents(FiesFile *handle,
                 FiesWriter *writer,
                 fies_pos logical_start,
                 FiesFile_Extent *buffer,
                 size_t count)
{
	(void)writer;

	ZVOL *self = handle->opaque;
	zbookmark_phys_t *zb = &self->zb;
	dnode_t *dn = self->dn;
	dnode_phys_t *dnphy = dn->dn_phys;
	long a = 0, b = (long)dnphy->dn_nblkptr;
	if (!b)
		return 0;

	// Binary search the outer block offsets for the closest below
	// logcial_start, then start mapping from there.
	while (b-a > 1) {
		long i = (a+b)/2;
		zb->zb_blkid = (unsigned long)i;
		blkptr_t *bp = &dnphy->dn_blkptr[i];
		uint64_t off = blkid2offset(dnphy, bp, zb);
		if (logical_start > off)
			a = i;
		else if (logical_start < off)
			b = i;
		else
			a = b = i;
	}
	if (a >= dnphy->dn_nblkptr) // no more blocks left
		return 0;
	size_t bufat = 0;
	for (unsigned long i = (unsigned long)a; i < dnphy->dn_nblkptr; ++i) {
		zb->zb_blkid = i;
		int rc = ZVOL_mapLevel(self, logical_start,
		                       buffer, &bufat, count,
		                       dmu_objset_spa(dn->dn_objset),
		                       dnphy, &dnphy->dn_blkptr[i], zb);
		if (rc < 0)
			return (ssize_t)rc;
		if (rc != 0)
			break;
	}
	return (ssize_t)bufat;
}

static const struct FiesFile_Funcs
zvol_file_funcs = {
	.close        = ZVOL_close,
	.pread        = ZVOL_pread,
	.next_extents = ZVOL_nextExtents,
	.get_os_fd    = ZVOL_os_fd
};

static int
zvol_write(FiesWriter *fies,
           bool as_ref,
           const char *pool,
           const char *volname,
           dnode_t *dn,
           uint64_t volsize)
{
	FiesFile *file = ZVOL_open(fies, pool, volname, dn, volsize);
	if (!file)
		return -errno;

	if (Vector_empty(&opt_xform))
		verbose(VERBOSE_FILES, "%s\n", file->filename);
	else
		verbose(VERBOSE_FILES, "%s/%s as %s\n",
		        pool, volname, file->filename);
	int rc;
	if (as_ref)
		rc = FiesWriter_readRefFile(fies, file);
	else
		rc = FiesWriter_writeFile(fies, file);
	FiesFile_close(file);
	return -rc;
}

static int
zvol_add_obj(FiesWriter *fies,
             bool as_ref,
             const char *pool,
             const char *volname,
             objset_t *os,
             uint64_t object)
{
	int rc;
	dnode_t *dn;
	dmu_buf_t *db = NULL;

	if (object == 0) {
		dn = DMU_META_DNODE(os);
	} else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
		rc = dmu_bonus_hold(os, object, FTAG, &db);
#pragma clang diagnostic pop
		if (rc) {
			fprintf(stderr,
			        "fies-zvol: dmu_bonus_hold(%" PRIu64
			        ") failed: %i\n",
			        object, rc);
			return ERR_SKIPMSG;
		}
		dn = DB_DNODE((dmu_buf_impl_t*)db);
	}

	dmu_object_info_t doi;
	dmu_object_info_from_dnode(dn, &doi);

	if (doi.doi_type == DMU_OT_ZVOL)
		rc = zvol_write(fies, as_ref, pool, volname,
		                dn, doi.doi_max_offset);
	else
		rc = ENOENT; // positivie as this is not an actual error yet

	if (db)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
		dmu_buf_rele(db, FTAG);
#pragma clang diagnostic pop
	return rc;
}

static int
do_zvol_add(FiesWriter *fies,
            bool as_ref,
            const char *pool,
            const char *volname,
            objset_t *os)
{
	dmu_objset_stats_t dds;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
	dmu_objset_fast_stat(os, &dds);
	dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
#pragma clang diagnostic pop

	if (dds.dds_type != DMU_OST_ZVOL) {
		fprintf(stderr, "fies-zvol: not a zvol: %s/%s (%u)\n",
		        pool, volname, dds.dds_type);
		return ERR_SKIPMSG;
	}

	int rc = zvol_add_obj(fies, as_ref, pool, volname, os, 0);
	if (rc <= 0)
		return rc;

	uint64_t object = 0;
	while ( (rc = dmu_object_next(os, &object, B_FALSE, 0)) == 0) {
		rc = zvol_add_obj(fies, as_ref, pool, volname, os, object);
		if (rc <= 0)
			return rc;
	}
	fprintf(stderr, "fies-zvol: no zvol meta info found for %s/%s\n",
	        pool, volname);
	if (rc != ESRCH) {
		fprintf(stderr, "fies-zvol: dmu_object_next() = %i\n", rc);
		return ERR_SKIPMSG;
	}
	return rc;
}

static bool
get_zvol_ro(const char *name, zfs_handle_t *zh, bool *value)
{
	char statbuf[8];
	zprop_source_t src;
	uint64_t bv;
	int rc = zfs_prop_get_numeric(zh, ZFS_PROP_READONLY, &bv,
	                              &src, statbuf, 0);
	if (rc) {
		fprintf(stderr,
		        "fies-zvol: cannot get readonly property of %s: %s\n",
		        name, strerror(errno));
		return false;
	}
	*value = (bv != 0);
	return true;
}

static bool
set_zvol_ro(const char *name, zfs_handle_t *zh, bool newvalue, bool *oldvalue)
{
	if (oldvalue) {
		if (!get_zvol_ro(name, zh, oldvalue))
			return false;
		if (*oldvalue == newvalue)
			return true;
	}
	if (zfs_prop_set(zh, zfs_prop_to_name(ZFS_PROP_READONLY),
	                 newvalue ? "on" : "off"))
	{
		fprintf(stderr, "fies-zvol: failed to set %s readonly: %s\n",
		        name, strerror(errno));
		return false;
	}
	return true;
}

static bool
zvol_done(const char *name)
{
	if (Map_get(&zvols_done, &name))
		return true;
	char *namedup = strdup(name);
	if (!namedup) {
		fprintf(stderr, "fies-zvol: %s\n", strerror(errno));
		return false;
	}
	int one = 1;
	Map_insert(&zvols_done, &namedup, &one);
	return false;
}

struct snapshot_iteration {
	FiesWriter *fies;
	const char *from;
	const char *to;
};

static int zvol_add(char *volume, bool as_ref, zfs_handle_t*, FiesWriter*);
static int
zvol_addsnap(zfs_handle_t *szh, void *opaque)
{
	struct snapshot_iteration *iter = opaque;

	const char *name = zfs_get_name(szh);
	if (!name) {
		// impossible... usually
		fprintf(stderr, "fies-zvol: failed to get zvol name: %s\n",
		        strerror(errno));
		return ERR_SKIPMSG;
	}

	const char *snap = strchr(name, '@');
	bool as_ref = false;
	bool last = false;
	if (snap) {
		++snap;
		if (iter->from) {
			if (!strcmp(snap, iter->from)) {
				iter->from = NULL;
				as_ref = true;
			} else {
				return 0;
			}
		}
		if (iter->to) {
			if (!strcmp(snap, iter->to)) {
				iter->from = iter->to; // magic
				iter->to = NULL;
				last = true;
			}
		}
	}

	char *namedup = strdup(name);
	if (!namedup) {
		fprintf(stderr, "fies-zvol: error: %s\n", strerror(errno));
		return ERR_SKIPMSG;
	}

	int rc = zvol_add(namedup, as_ref, szh, iter->fies);
	free(namedup);
	if (rc)
		return rc;
	if (last)
		return ITER_END;
	return rc;
}

static int
zvol_add(char *volume, bool as_ref, zfs_handle_t *zh, FiesWriter *fies)
{
	char *poolsep = strchr(volume, '/');
	if (!poolsep) {
		fprintf(stderr,
		        "fies-zvol: please use <pool>/<volume> instead of %s\n",
		        volume);
		return ERR_SKIPMSG;
	}

	if (zvol_done(volume))
		return 0;

	if (!zh) {
		zh = zfs_open(zfs, volume,
		              ZFS_TYPE_VOLUME | ZFS_TYPE_SNAPSHOT);
		if (!zh) {
			fprintf(stderr, "fies-zvol: cannot open zvol %s: %s\n",
			        volume, strerror(errno));
			return ERR_SKIPMSG;
		}
	}

	int retval;

	if (opt_snapshots) {
		opt_snapshots = false; // disable nesting
		struct snapshot_iteration iter = {
			fies, opt_from, opt_to
		};
		retval = zfs_iter_snapshots_sorted(zh, zvol_addsnap, &iter);
		opt_snapshots = true; // reenable
		if (retval) {
			if (retval == ITER_END)
				retval = 0;
			goto out_zh;
		}
	}

	retval = ERR_SKIPMSG;

	bool isvol = (zfs_get_type(zh) == ZFS_TYPE_VOLUME);
	bool was_ro = false;
	if (isvol) {
		if (opt_set_ro) {
			if (!set_zvol_ro(volume, zh, true, &was_ro))
				goto out_zh;
		} else if (!opt_ignore_rw) {
			if (!get_zvol_ro(volume, zh, &was_ro))
				goto out_zh;
			if (!was_ro) {
				fprintf(stderr, "fies-zvol: %s is writable\n",
				        volume);
				goto out_zh;
			}
		}
	}

	objset_t *os = NULL;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
	int rc = dmu_objset_own(volume, DMU_OST_ANY, B_TRUE, FTAG, &os);
#pragma clang diagnostic pop
	if (rc) {
		fprintf(stderr, "fies-zvol: failed to open %s: %s\n",
		        volume, strerror(rc));
		goto out_restore;
	}
	*poolsep = 0;
	retval = do_zvol_add(fies, as_ref, volume, poolsep+1, os);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
	dmu_objset_disown(os, FTAG);
#pragma clang diagnostic pop
	*poolsep = '/'; // Needed for set_zvol_ro below

out_restore:
	if (opt_set_ro && isvol && !was_ro)
		set_zvol_ro(volume, zh, false, NULL);

out_zh:
	zfs_close(zh);
	return retval;
}

int
main(int argc, char **argv)
{
	const char *errstr = NULL;

	Vector_init_type(&opt_xform, RexReplace*);
	Vector_set_destructor(&opt_xform, (Vector_dtor*)RexReplace_pdestroy);
	Map_init_type(&pool_devs, Map_strcmp,
	              char*, Map_pfree,
	              fies_id, NULL);

	Map_init_type(&zvols_done, Map_strcmp, char*, Map_pfree, int, NULL);

	atexit(main_cleanup);

	while (true) {
		int index = 0;
		int c = getopt_long(argc, argv, "hvf:s:", longopts, &index);
		if (c == -1)
			break;
		handle_option(c, optopt, optarg);
	}

	if (opt_from && !*opt_from)
		opt_from = NULL;
	if (opt_to && !*opt_to)
		opt_from = NULL;

	if ((opt_from || opt_to) && !opt_snapshots) {
		showerr("fies-zvol: --from and --to "
		        "require --snapshots to be set\n");
		option_error = true;
	}

	if (option_error)
		usage(stderr, EXIT_FAILURE);

	argc -= optind;
	argv += optind;
	if (!argc) {
		fprintf(stderr, "fies-zvol: missing volume names\n");
		usage(stderr, EXIT_FAILURE);
	}

	if ((opt_from || opt_to) && argc != 1) {
		showerr("fies-zvol: --from and --to "
		        "can only be used with a single volume\n");
		usage(stderr, EXIT_FAILURE);
	}

	int stream_fd = STDOUT_FILENO;
	if (opt_file && strcmp(opt_file, "-")) {
		stream_fd = open(opt_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (stream_fd < 0) {
			fprintf(stderr, "fies-zvol: open(%s): %s\n",
			        opt_file, strerror(errno));
			return 1;
		}
	}

	kernel_init(FREAD);

	int rc = -1;
	FiesWriter *fies = NULL;

	zfs = libzfs_init();
	if (!zfs) {
		fprintf(stderr, "fies-zvol: %s\n", libzfs_error_init(errno));
		goto out_skiperrmsg;
	}

	fies = FiesWriter_new(&writer_funcs, &stream_fd);
	if (!fies) {
		fprintf(stderr, "fies-zvol: failed to initialize: %s\n",
		        strerror(errno));
		rc = 1;
		goto out_skiperrmsg;
	}

	for (int i = 0; i != argc; ++i) {
		char *volume = argv[i];
		rc = zvol_add(volume, false, NULL, fies);
		if (rc == ERR_SKIPMSG)
			goto out_skiperrmsg;
		if (rc)
			break;
	}

	if (rc) {
		errstr = FiesWriter_getError(fies);
		if (!errstr)
			errstr = strerror(-rc);
		fprintf(stderr, "fies-zvol: %s\n", errstr);
	}
out_skiperrmsg:
	libzfs_fini(zfs);
	kernel_fini();
	FiesWriter_delete(fies);
	close(stream_fd);
	return rc == 0 ? 0 : 1;
}
