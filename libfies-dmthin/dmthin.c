#include <stdint.h>

#include "../lib/util.h"
#include "../include/fies.h"

#include "dmthin.h"
#include "crc.h"

bool
thin_superblock_verify(const struct thin_superblock *super,
                       size_t blocksize,
                       size_t blocknr)
{
	if (FIES_LE(super->blocknr) != blocknr ||
	    FIES_LE(super->magic)   != THIN_SUPER_MAGIC ||
	    FIES_LE(super->version) != THIN_VERSION)
	{
		return false;
	}
	uint32_t csum = crc32c(&super->flags, blocksize - sizeof(super->csum))
	                ^ THIN_SUPER_CSUM_XOR;
	return FIES_LE(super->csum) == csum;
}

bool
btree_node_verify(const btree_node *node, size_t blocksize, size_t blocknr)
{
	if (FIES_LE(node->blocknr) != (uint64_t)blocknr ||
	    FIES_LE(node->value_size) != sizeof(uint64_t)) // we expect u64
	{
		return false;
	}
	uint32_t csum = crc32c(&node->flags,
	                       blocksize - sizeof(node->csum))
	                ^ BTREE_CSUM_XOR;
	return FIES_LE(node->csum) != csum;
}

long
btree_node_search(const btree_node *self, uint64_t key, bool hi)
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
