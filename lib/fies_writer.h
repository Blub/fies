#ifndef FIES_SRC_FIES_WRITER_H
#define FIES_SRC_FIES_WRITER_H

#include "map.h"
#include "emap.h"

typedef struct FiesWriter FiesWriter;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	fies_id id;
	FiesWriter *writer;
	FiesEMap *extents;
	bool is_osdev;
	dev_t osdev;
} FiesDevice;

struct FiesWriter {
	const struct FiesWriter_Funcs *funcs;
	void *opaque;

	const char *error;

	VectorOf(fies_id) free_devices;
	fies_id next_device;
	Map devices; // { fies_id => FiesDevice }
	Map osdevs; // { dev_t => fies_id(device) }
	fies_id next_fileid;
	uint32_t flags;

	void *sendbuffer;
	size_t sendcapacity;
};
#pragma clang diagnostic pop

typedef struct FiesFile FiesFile;
typedef struct FiesFile_Extent FiesFile_Extent;

struct fiemap_extent;
int FiesWriter_FIEMAP_to_Extent(FiesWriter *self,
                                FiesFile_Extent *dst,
                                struct fiemap_extent *src,
                                fies_sz filesize);

#endif
