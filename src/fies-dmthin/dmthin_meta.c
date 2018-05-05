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

#define SHOWTREE(x)

enum node_flags {
	INTERNAL_NODE = 0x01,
	LEAF_NODE     = 0x02
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wzero-length-array"
#pragma clang diagnostic ignored "-Wpacked"
#define BTREE_CSUM_XOR 121107
typedef struct btree_node {
	uint32_t csum;
	uint32_t flags;
	uint64_t blocknr;
	uint32_t nr_entries;
	uint32_t max_entries;
	uint32_t value_size;
	uint32_t padding;
	uint64_t keys[0];
} FIES_PACKED btree_node;

#define THIN_SUPER_MAGIC 0x019c52ba
#define THIN_VERSION 2
#define THIN_SPACE_MAP_SIZE 128
#define THIN_SUPER_CSUM_XOR 160774
struct thin_superblock {
	uint32_t csum;
	uint32_t flags;
	uint64_t blocknr;

	uint8_t  uuid[16];

	uint64_t magic;
	uint32_t version;
	uint32_t time;

	uint64_t transaction_id;
	uint64_t metasnap_root;

	uint8_t  data_spacemap_root[THIN_SPACE_MAP_SIZE];
	uint8_t  metadata_spacemap_root[THIN_SPACE_MAP_SIZE];
	uint64_t data_mapping_root;
	uint64_t dev_details_root;
	uint32_t data_block_size; // in 512 byte sectors
	uint32_t metadata_block_size; // in 512 byte sectors
	uint32_t compat_flags;
	uint32_t compat_ro_flags;
	uint32_t incompat_flags;
} FIES_PACKED;
#pragma clang diagnostic pop

static bool
VerifyTreeNode(const btree_node *node, size_t blocksize, size_t blocknr)
{
	if (FIES_LE(node->blocknr) != (uint64_t)blocknr ||
	    FIES_LE(node->value_size) != sizeof(uint64_t)) // we expect u64
	{
		return false;
	}
	uint32_t csum = crc32c(&node->flags,
	                       blocksize - sizeof(node->csum))
	                ^ BTREE_CSUM_XOR;
	if (FIES_LE(node->csum) != csum) {
		fprintf(stderr, "fies: dmthin btree checksum error\n");
		return false;
	}
	return true;
}

static bool
VerifyThinSuper(const struct thin_superblock *super, size_t bsz, size_t bnr)
{
	if (FIES_LE(super->blocknr) != bnr ||
	    FIES_LE(super->magic)   != THIN_SUPER_MAGIC ||
	    FIES_LE(super->version) != THIN_VERSION)
	{
		fprintf(stderr,
		        "fies: dmthin super block verification failed\n");
		return false;
	}
	uint32_t csum = crc32c(&super->flags, bsz - sizeof(super->csum))
	                ^ THIN_SUPER_CSUM_XOR;
	if (FIES_LE(super->csum) != csum) {
		fprintf(stderr, "fies: dmthin super block checksum error\n");
		return false;
	}
	return true;
}


ThinMeta*
ThinMeta_new(const char *name,
             const char *poolname,
             size_t root,
             size_t datablocksecs,
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

	ThinMeta *self = malloc(sizeof(*self));
	memset(self, 0, sizeof(*self));
	self->name = strdup(name);
	self->poolname = strdup(poolname);
	self->snaproot = root;
	self->fd = fd;
	self->fid = FiesWriter_newDevice(writer);
	self->size = size512s * 512;
	self->blocksize = blocksize;
	self->datablocksize = datablocksecs * 512;
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
		        "  > dmsetup message %s release_metadata_snap\n",
		        self->poolname);
	} else {
		self->release = false;
	}
	self->snaproot = 0;
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

static inline bool
Load(ThinMeta *self, void *buffer, size_t block)
{
	ssize_t got = pread(self->fd, buffer,
	                    self->blocksize,
	                    (off_t)(self->blocksize * block));
	return (size_t)got == self->blocksize;
}

static inline bool
LoadSuper(ThinMeta *self, void *buffer, size_t block)
{
	if (!Load(self, buffer, block))
		return false;
	if (!VerifyThinSuper(buffer, self->blocksize, block)) {
		errno = EBADF;
		return false;
	}
	return true;
}

static inline bool
LoadNode(ThinMeta *self, void *buffer, size_t block)
{
	if (!Load(self, buffer, block))
		return false;
	if (!VerifyTreeNode(buffer, self->blocksize, block)) {
		errno = EBADF;
		return false;
	}
	return true;
}

bool
ThinMeta_loadRoot(ThinMeta *self, bool reserve)
{
	if (self->release) {
		// internal usage error, we snapshot once for each volume
		errno = EBUSY;
		return false;
	}

	int err = EFAULT;
	void *buffer = aligned_alloc(self->blocksize, self->blocksize);
	struct thin_superblock *super = buffer;

	if (reserve) { //if (self->snaproot) {
		if (!DMMessage(self->poolname, "reserve_metadata_snap"))
			goto out;
		self->release = true;

		if (!LoadSuper(self, super, 0)) {
			err = errno;
			goto out;
		}
		self->snaproot = FIES_LE(super->metasnap_root);
	}

	if (!LoadSuper(self, super, self->snaproot)) {
		err = errno;
		goto out;
	}

	self->dataroot = FIES_LE(super->data_mapping_root);

	err = 0;
out:
	free(buffer);
	ThinMeta_release(self);
	return err == 0;
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
                      size_t      datablocksecs,
                      size_t      root,
                      FiesWriter *writer)
{
	ThinMeta *meta = g_hash_table_lookup(table, poolname);
	if (meta)
		return meta;

	ThinMeta *dev = ThinMeta_new(name, poolname, root, datablocksecs,
	                             writer, false);
	if (!dev)
		return NULL;

	g_hash_table_insert(table, dev->poolname, dev);
	return dev;
}

ThinMeta*
ThinMetaTable_addPool(GHashTable *table,
                      const char *poolname,
                      size_t      root,
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
	                                       datablocksecs, root, writer);
	if (!self) {
		int saved_errno = errno;
		free(metaname);
		errno = saved_errno;
		return NULL;
	}
	free(metaname);
	return self;
}

static long
BTree_search(btree_node *self, uint64_t key, bool hi)
{
	long a = -1;
	long b = (long)FIES_LE(self->nr_entries);
	while (b-a > 1) {
		long i = a + (b-a)/2; // (a+b)/2 might overflow
		uint64_t k = FIES_LE(self->keys[i]);
		if      (key < k) b = i;
		else if (key > k) a = i;
		else return (unsigned int)i;
	}
	return hi ? b : a;
}

static inline void*
BTree_values(btree_node *node)
{
	return node->keys + FIES_LE(node->max_entries);
}

static bool
ThinMeta_btree_search(ThinMeta   *self,
                      btree_node *node,
                      uint64_t    block,
                      uint64_t    key,
                      uint64_t   *value)
{
	long index;
	while (true) {
		if (!LoadNode(self, node, block))
			return false;
		index = BTree_search(node, key, false);
		if (index < 0 || (uint32_t)index == FIES_LE(node->nr_entries))
			return false;
		if (!(FIES_LE(node->flags) & INTERNAL_NODE))
			break;
		uint64_t *values = BTree_values(node);
		block = FIES_LE(values[index]);
	}
	if (!(FIES_LE(node->flags) & LEAF_NODE)) {
		fprintf(stderr, "fies: dmthin: bad btree node flags %08x\n",
		        FIES_LE(node->flags));
		return false;
	}
	if (FIES_LE(node->keys[index]) != key)
		return false;
	uint64_t *values = BTree_values(node);
	*value = FIES_LE(values[index]);
	return true;
}

static int
ThinMeta_mapTree(ThinMeta        *self,
                 btree_node      *pnode,
                 uint64_t         block,
                 uint64_t         start,
                 FiesFile_Extent *out_buf,
                 size_t           out_count,
                 size_t          *out_index)
{
	if (!LoadNode(self, pnode, block))
		return -EIO;

	int retval = -EINVAL;

	btree_node *node = g_memdup(pnode, (guint)self->blocksize);
	const uint64_t *values = BTree_values(node);

	const uint32_t flags = FIES_LE(node->flags);
	const uint32_t entries = FIES_LE(node->nr_entries);
	if (flags & INTERNAL_NODE) {
		// recurse, covering the 'start' point
		long lfrom = BTree_search(node, start, false);
		uint32_t from = lfrom < 0 ? 0 : (uint32_t)lfrom;
		SHOWTREE(fprintf(stderr, "(search %" PRIu64 " => %u/%u)\n",
		         start, from, entries));
		if (from == entries) {
			retval = 0;
			goto out;
		}
		while (*out_index != out_count && from != entries) {
			uint64_t next = FIES_LE(values[from]);
			SHOWTREE(fprintf(stderr, " vv DOWN vv\n"));
			SHOWTREE(fprintf(stderr,
			          "   branch %u of %u maps %" PRIu64
			          " to %" PRIu64 "\n",
			                 from, entries,
			                 FIES_LE(node->keys[from]), next));
			int step = ThinMeta_mapTree(self, pnode, next,
			                            start, out_buf,
			                            out_count, out_index);
			SHOWTREE(fprintf(stderr, " ^^  UP  ^^\n"));
			if (step < 0)
				return step;
			++from;
		}
		retval = 0;
	} else if (flags & LEAF_NODE) {
		long lfrom = BTree_search(node, start, true);
		uint32_t from = lfrom < 0 ? 0 : (uint32_t)lfrom;
		SHOWTREE(fprintf(stderr, "(search %" PRIu64 " => %u/%u)\n",
		                 start, from, entries));
		if (from == entries) {
			retval = 0;
			goto out;
		}
		FiesFile_Extent *ex = &out_buf[*out_index - 1];
		SHOWTREE(fprintf(stderr, " __ LAND __\n"));
		while (from != entries) {
			// First 24 bits are a time stamp.
			uint64_t logblock = FIES_LE(node->keys[from]);
			uint64_t logaddr = logblock * self->datablocksize;
			uint64_t physblock = FIES_LE(values[from]) >> 24;
			uint64_t physaddr = physblock * self->datablocksize;
			SHOWTREE(fprintf(stderr, "   -> %u maps %" PRIu64
			                         " to %" PRIu64 "\n",
			                 from, logblock, physblock));
			// Can we concatenate the extent?
			if (*out_index &&
			    ex->physical + ex->length == physaddr &&
			    ex->logical + ex->length == logaddr)
			{
				ex->length += self->datablocksize;
			} else if (*out_index < out_count) {
				ex = &out_buf[*out_index];
				ex->device = 0;
				ex->flags = FIES_FL_DATA | FIES_FL_SHARED;
				ex->physical = physaddr;
				ex->logical = logaddr;
				ex->length = self->datablocksize;
				++*out_index;
			} else {
				break;
			}
			++from;
		}
		SHOWTREE(fprintf(stderr, " ~^ LIFT ^~\n"));
		retval = 0;
	} else {
		fprintf(stderr, "fies: dmthin: bad btree flags: 0x%x\n",
		         flags);
	}

out:
	free(node);
	return retval;
}

ssize_t
ThinMeta_map(ThinMeta        *self,
             unsigned         dev,
             fies_pos         logical_start,
             FiesFile_Extent *output,
             size_t           count)
{
	ssize_t retval = -EINVAL;
	btree_node *node = aligned_alloc(self->blocksize, self->blocksize);

	uint64_t lvroot;
	if (!ThinMeta_btree_search(self, node, self->dataroot, dev, &lvroot)) {
		fprintf(stderr, "fies: dmthin: failed to find device %u\n",
		        dev);
		goto out;
	}

	SHOWTREE(fprintf(stderr,
	                 "Found volume %u's data btree at %" PRIu64 "\n",
	                 dev, lvroot));

	// Preload the first node and check for an empty device.
	if (!LoadNode(self, node, lvroot))
		goto out;
	if (!FIES_LE(node->nr_entries)) {
		retval = 0;
		goto out;
	}

	size_t exindex = 0;
	uint64_t start = logical_start;
	uint64_t startblock = start / self->datablocksize;
	int rc = ThinMeta_mapTree(self, node, lvroot, startblock,
	                          output, count, &exindex);
	if (rc < 0)
		goto out;

	if (exindex && output[0].logical < start) {
		uint64_t shift = start - output[0].logical;
		output[0].logical += shift;
		output[0].physical += shift;
		if (output[0].length <= shift) {
			output[0].length = 0;
			fprintf(stderr, "fies: dmthin: partial block\n");
		} else {
			output[0].length -= shift;
		}
	}
	//for (size_t i = 0; i != exindex; ++i) {
	//	fprintf(stderr,
	//	        "Extent %zu: %" PRIu64 ":%" PRIu64 " @ %" PRIu64 "\n",
	//	        i,
	//	        output[i].logical, output[i].length,
	//	        output[i].physical);
	//}

	retval = (ssize_t)exindex;

out:
	free(node);
	return retval;
}
