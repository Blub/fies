#ifndef FIES_LIBDMTHIN_INC
#define FIES_LIBDMTHIN_INC

#include <stdint.h>

#ifndef FIES_PACKED
#  error please include the fies header
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define FIES_DMTHIN_SUPER_MAGIC 0x019c52ba
#define FIES_DMTHIN_VERSION 2
#define FIES_DMTHIN_SPACE_MAP_SIZE 128
#define FIES_DMTHIN_SUPER_CSUM_XOR 160774

typedef const void* FiesDMThin_getBlock_t(void *opaque, size_t block_number);
typedef void        FiesDMThin_putBlock_t(void *opaque, const void *block);
struct FiesDMThin {
	/*! \brief Handle to data source. */
	void *opaque;

	/*! \brief Size of the metadata device. */
	size_t size;

	/*! \brief Size of a metadata device block. */
	size_t block_size;

	/*! \brief Metadata toot or snapshot block number. */
	size_t snap_root;

	/*! \brief Metadata block size as read from the superblock. */
	size_t metadata_block_size;

	/*! \brief Data block size as read from the superblock. */
	size_t data_block_size;

	/*! \brief Data mapping btree start block. */
	size_t data_mapping_root;

	/*! \brief Callback to get a pointer to a block of meta data. */
	FiesDMThin_getBlock_t *get_block_cb;

	/*! \brief Callback to return a block to the user. */
	FiesDMThin_putBlock_t *put_block_cb;
};

/*! \brief Create a new dmthin accessor instance. */
struct FiesDMThin* FiesDMThin_new(void *opaque,
                                  size_t size,
                                  size_t block_size,
                                  FiesDMThin_getBlock_t *get_block_cb,
                                  FiesDMThin_putBlock_t *put_block_cb);

/*! \brief Destroy a FiesDMThin accessor instance. */
void FiesDMThin_delete(struct FiesDMThin *self);

/*! \brief Map a logical address to a physical one. */
off_t FiesDMThin_mapAddress(struct FiesDMThin *self,
                            uint32_t device,
                            uint64_t logical);

/*! \brief Map a logical address range to a physical extent list for fies. */
ssize_t FiesDMThin_mapExtents(struct FiesDMThin      *self,
                              uint32_t                device,
                              uint64_t                logical,
                              uint64_t                length,
                              struct FiesFile_Extent *out_extents,
                              size_t                  out_count);

struct FiesDMThin_Extent {
	/*! \brief Logical offset of the extent within the file. */
	fies_pos logical;
	/*! \brief Physical offset of the extent on the underlying device. */
	fies_pos physical;
	/*! \brief Length of the extent in bytes. */
	fies_sz length;
	/*! \brief Flags describing the type of extent. */
	uint32_t flags;
	/*! \brief Reference count of the extent. */
	uint32_t refcount;
};

#define FIES_DMTHIN_QUERY_REFCOUNTS (1<<0)

/*! \brief Query information about a logical address range. */
ssize_t FiesDMThin_queryExtents(struct FiesDMThin        *self,
                                uint32_t                  device,
                                uint64_t                  logical,
                                uint64_t                  length,
                                uint32_t                  query_flags,
                                struct FiesDMThin_Extent *out_extents,
                                size_t                    out_count);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
