#include <errno.h>

#include "main.h"

static inline const void*
fdmt_getBlock(FiesDMThin *self, size_t blocknr)
{
	const void *block = self->get_block_cb(self->opaque, blocknr);
	if (!block && !errno)
		errno = EIO;
	return block;
}

static inline void
fdmt_putBlock(FiesDMThin *self, const void *block)
{
	self->put_block_cb(self->opaque, block);
}

static int
fdmt_init(FiesDMThin *self)
{
	const struct thin_superblock *super = fdmt_getBlock(self, 0);
	if (!super)
		return -errno;
	if (!thin_superblock_verify(super, self->block_size, 0)) {
		fdmt_putBlock(self, super);
		return -EBADF;
	}
	self->snap_root = FIES_LE(super->metasnap_root);
	if (self->snap_root) {
		fdmt_putBlock(self, super);
		super = fdmt_getBlock(self, self->snap_root);
		if (!super)
			return -errno;
		if (!thin_superblock_verify(super, self->block_size,
		                            self->snap_root))
		{
			fdmt_putBlock(self, super);
			return -EIO;
		}
	}

	self->metadata_block_size = FIES_LE(super->metadata_block_size) * 512;
	self->data_block_size = FIES_LE(super->data_block_size) * 512;
	self->data_mapping_root = FIES_LE(super->data_mapping_root);

	fdmt_putBlock(self, super);
	return 0;
}

extern
FiesDMThin*
FiesDMThin_new(void *opaque,
               size_t size,
               size_t block_size,
               FiesDMThin_getBlock_t *get_block_cb,
               FiesDMThin_putBlock_t *put_block_cb)
{
	FiesDMThin *self = u_malloc0(sizeof(*self));
	if (!self)
		return NULL;
	self->opaque = opaque;
	self->size = size;
	self->block_size = block_size;
	self->get_block_cb = get_block_cb;
	self->put_block_cb = put_block_cb;

	int rc = fdmt_init(self);
	if (rc < 0) {
		free(self);
		errno = -rc;
		return NULL;
	}

	return self;
}

extern void
FiesDMThin_delete(FiesDMThin *self)
{
	free(self);
}

static int
fdmt_search(FiesDMThin *self, uint64_t block, uint64_t key, uint64_t *value)
{
	int rc;
	long index;
	const btree_node *node = NULL;
	while (true) {
		if ( !(node = fdmt_getBlock(self, block)) )
			return -errno;
		index = btree_node_search(node, key, false);
		if (index < 0 || (uint32_t)index == FIES_LE(node->nr_entries))
		{
			rc = -ERANGE;
			goto out;
		}
		if ( !(FIES_LE(node->flags) & INTERNAL_NODE) )
			break;
		const uint64_t *values = btree_node_values(node);
		block = FIES_LE(values[index]);
		fdmt_putBlock(self, node);
	}
	if ( !(FIES_LE(node->flags) & LEAF_NODE) ) {
		rc = -EINVAL;
		goto out;
	}
	if (FIES_LE(node->keys[index]) != key) {
		rc = -ENOENT;
		goto out;
	}
	rc = 0;
	const uint64_t *values = btree_node_values(node);
	*value = FIES_LE(values[index]);
out:
	return rc;
}

static int
fdmt_searchRoot(FiesDMThin *self, uint64_t key, uint64_t *value)
{
	return fdmt_search(self, self->data_mapping_root, key, value);
}

extern off_t
FiesDMThin_mapAddress(FiesDMThin *self, uint32_t device, uint64_t logical)
{
	uint64_t start;
	int rc = fdmt_searchRoot(self, device, &start);
	if (rc < 0)
		return (off_t)rc;

	uint64_t address;
	if (!fdmt_search(self, start, logical, &address))
		return -errno;

	return (off_t)address;
}

static int
fdmt_mapAddressRange(FiesDMThin       *self,
                     uint64_t          pos,   // btree position
                     uint64_t          begin, // block to start mapping from
                     uint64_t          end,   // last+1 block to map to
                     FiesFile_Extent  *out_buf,
                     size_t            out_count,
                     size_t           *index)
{
	const btree_node *node = fdmt_getBlock(self, pos);
	if (!node)
		return -errno;

	const uint64_t *values = btree_node_values(node);
	const uint32_t flags   = FIES_LE(node->flags);
	const uint32_t entries = FIES_LE(node->nr_entries);

	int rc = 0;
	if (!(flags & (INTERNAL_NODE | LEAF_NODE))) {
		rc = -EBADF;
		goto out;
	}

	// Search for the current address
	long lfrom = btree_node_search(node, begin, (flags & LEAF_NODE));
	uint32_t from = lfrom < 0 ? 0 : (uint32_t)lfrom;
	if (from == entries)
		goto out; // higher than the last mapped chunk

	if (flags & INTERNAL_NODE) {
		// recurse into each node that may contain the address:
		while (*index != out_count && from != entries) {
			uint64_t next = FIES_LE(values[from]);
			if (next >= end)
				break;

			rc = fdmt_mapAddressRange(self, next, begin, end,
			                          out_buf, out_count, index);
			if (rc < 0)
				goto out;
			++from;
		}
	} else if (flags & LEAF_NODE) {
		FiesFile_Extent *ex = &out_buf[*index - 1];
		while (from != entries) {
			// First 24 bits are a time stamp.
			uint64_t logblock = FIES_LE(node->keys[from]);
			uint64_t logaddr = logblock * self->data_block_size;
			uint64_t physblock = FIES_LE(values[from]) >> 24;
			uint64_t physaddr = physblock * self->data_block_size;

			// Can we concatenate the extent?
			if (*index &&
			    ex->physical + ex->length == physaddr &&
			    ex->logical + ex->length == logaddr)
			{
				ex->length += self->data_block_size;
			} else if (*index < out_count) {
				ex = &out_buf[*index];
				ex->device = 0;
				ex->flags = FIES_FL_DATA | FIES_FL_SHARED;
				ex->physical = physaddr;
				ex->logical = logaddr;
				ex->length = self->data_block_size;
				++*index;
			} else {
				break;
			}
			++from;
		}
	} else {
		rc = -EBADF;
	}

out:
	fdmt_putBlock(self, node);
	return rc;
}

extern ssize_t
FiesDMThin_mapExtents(FiesDMThin       *self,
                      uint32_t          device,
                      uint64_t          logical_start,
                      uint64_t          length,
                      FiesFile_Extent  *out_buf,
                      size_t            out_count)
{
	uint64_t at;
	int rc = fdmt_searchRoot(self, device, &at);
	if (rc < 0)
		return (ssize_t)rc;

	size_t index = 0;
	uint64_t begin = logical_start / self->data_block_size;
	uint64_t blocklen = length / self->data_block_size;
	uint64_t end = begin + blocklen;
	if (end < begin) // overflow
		end = (uint64_t)-1;
	rc = fdmt_mapAddressRange(self, at, begin, end,
	                          out_buf, out_count, &index);
	if (rc < 0)
		return (ssize_t)rc;

	// cut the first block to the actual address
	if (index && out_buf[0].logical < logical_start) {
		uint64_t shift = logical_start - out_buf[0].logical;
		out_buf[0].logical += shift;
		out_buf[0].physical += shift;
		if (out_buf[0].length <= shift) {
			// shouldn't be possible
			out_buf[0].length = 0;
		} else {
			out_buf[0].length -= shift;
		}
	}

	return (ssize_t)index;
}
