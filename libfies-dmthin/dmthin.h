#ifndef FIES_LIB_FIESMDTHIN_DMTHIN_H
#define FIES_LIB_FIESMDTHIN_DMTHIN_H

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

bool btree_node_verify(const btree_node*, size_t blocksize, size_t blocknr);
long btree_node_search(const btree_node*, uint64_t key, bool hi);

static inline const void*
btree_node_values(const btree_node *node)
{
	return node->keys + FIES_LE(node->max_entries);
}

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

bool thin_superblock_verify(const struct thin_superblock *super,
                            size_t blocksize,
                            size_t blocknr);

#endif
