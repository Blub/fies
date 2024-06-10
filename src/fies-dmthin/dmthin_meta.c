#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <linux/fs.h>
#include <sys/ioctl.h>

#include <libdevmapper.h>

#include "../../lib/util.h"
#include "../../lib/fies.h"
#include "../cli_common.h"
#include "../util.h"
#include "fies_dmthin.h"

static const void*
ThinMeta_getBlock(void *opaque, size_t block_number)
{
	ThinMeta *self = opaque;

	void *block = aligned_alloc(self->blocksize, self->blocksize);
	if (!block)
		return NULL;
	ssize_t got = pread(self->fd, block, self->blocksize,
	                    (off_t)(self->blocksize * block_number));

	if ((size_t)got != self->blocksize) {
		int err = got < 0 ? errno : EIO;
		free(block);
		errno = err;
		return NULL;
	}
	return block;
}

static void
ThinMeta_putBlock(void *opaque, const void *block)
{
	(void)opaque;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-qual"
	free((void*)block);
#pragma clang diagnostic pop
}

ThinMeta*
ThinMeta_new(const char *name,
             const char *poolname,
             FiesWriter *writer,
             bool raw)
{
	char *tmppath;
	const char *path;
	if (raw) {
		tmppath = NULL;
		path = name;
	} else {
		tmppath = make_path("/dev/mapper/", name, NULL);
		path = tmppath;
	}
	int fd = open(path, O_RDONLY | O_DIRECT);
	if (fd < 0) {
		int saved_errno = errno;
		fprintf(stderr, "fies: open(%s): %s\n",
		        path, strerror(saved_errno));
		free(tmppath);
		errno = saved_errno;
		return NULL;
	}
	free(tmppath);

	unsigned long size512s = 0;
	size_t blocksize = 0;
	if (ioctl(fd, BLKGETSIZE, &size512s) != 0 ||
	    ioctl(fd, BLKBSZGET, &blocksize) != 0)
	{
		fprintf(stderr, "fies: failed to get block size info: %s\n",
		        strerror(errno));
		close(fd);
		errno = EFAULT;
		return NULL;
	}

	ThinMeta *self = u_malloc0(sizeof(*self));
	self->name = strdup(name);
	self->poolname = strdup(poolname);
	self->size = size512s * 512;
	self->blocksize = blocksize;
	self->fd = fd;
	self->fid = FiesWriter_newDevice(writer);
	return self;
}

void
ThinMeta_release(ThinMeta *self)
{
	if (!self->release)
		return;
	if (!DMMessage(self->poolname, "release_metadata_snap")) {
		fprintf(stderr,
		        "WARNING: Failed to release metadata snapshot\n"
		        "  You must run the following command to clean up:\n"
		        "  > dmsetup message %s 0 release_metadata_snap\n",
		        self->poolname);
	} else {
		self->release = false;
	}
}

void
ThinMeta_delete(ThinMeta *self)
{
	if (!self)
		return;
	close(self->fd);
	ThinMeta_release(self);
	free(self->name);
	free(self->poolname);
	free(self);
}

static void
ThinMeta_delete_g(gpointer pself)
{
	ThinMeta_delete(pself);
}

bool
ThinMeta_loadRoot(ThinMeta *self, bool reserve)
{
	if (self->release) {
		// internal usage error, we snapshot once for each volume
		errno = EBUSY;
		return false;
	}

	if (reserve) {
		if (!DMMessage(self->poolname, "reserve_metadata_snap")) {
			errno = EFAULT;
			return false;
		}
		self->release = true;
	}
	self->dmthin = FiesDMThin_new(self, self->size, self->blocksize,
	                              ThinMeta_getBlock,
	                              ThinMeta_putBlock);
	if (!self->dmthin) {
		int saved_errno = errno;
		if (self->release)
			ThinMeta_release(self);
		errno = saved_errno;
		return false;
	}

	return true;
}

GHashTable*
ThinMetaTable_new()
{
	return g_hash_table_new_full(g_str_hash, g_str_equal,
	                             NULL, ThinMeta_delete_g);
}

void
ThinMetaTable_delete(GHashTable *table)
{
	if (table)
		g_hash_table_destroy(table);
}

static ThinMeta*
ThinMetaTable_addMeta(GHashTable *table,
                      const char *name,
                      const char *poolname,
                      FiesWriter *writer)
{
	ThinMeta *meta = g_hash_table_lookup(table, poolname);
	if (meta)
		return meta;

	ThinMeta *dev = ThinMeta_new(name, poolname, writer, false);
	if (!dev)
		return NULL;

	g_hash_table_insert(table, dev->poolname, dev);
	return dev;
}

ThinMeta*
ThinMetaTable_addPool(GHashTable *table,
                      const char *poolname,
                      FiesWriter *writer)
{

	// Query the metadata and data volume names from the pool now:
	dev_t metadev = 0;
	dev_t datadev = 0;
	unsigned int datablocksecs = 0;
	if (!DMThinPoolInfo(poolname, &metadev, &datadev, &datablocksecs))
		return NULL;
	char *metaname = GetDMName(metadev);
	ThinMeta *self = ThinMetaTable_addMeta(table, metaname, poolname,
	                                       writer);
	if (!self) {
		int saved_errno = errno;
		free(metaname);
		errno = saved_errno;
		return NULL;
	}
	free(metaname);
	return self;
}

ssize_t
ThinMeta_map(ThinMeta        *self,
             unsigned         dev,
             fies_pos         logical_start,
             FiesFile_Extent *output,
             size_t           count)
{
	return FiesDMThin_mapExtents(self->dmthin, dev, logical_start,
	                             (uint64_t)-1, // to end
	                             output, count);
}
