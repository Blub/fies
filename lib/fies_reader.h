#ifndef FIES_SRC_FIES_READER_H
#define FIES_SRC_FIES_READER_H

#include <setjmp.h>

#include "map.h"

typedef enum {
	FR_State_Header,
	FR_State_Begin,
	FR_State_NewFile_Get,
	FR_State_NewFile_Create,
	FR_State_FileMeta_Get,
	FR_State_FileMeta_Do,
	FR_State_FileEnd,
	FR_State_SnapshotList,
#if 0
	FR_State_FileClose,
#endif
	FR_State_Extent_Get,
	FR_State_Extent_Start,
	FR_State_Extent_GetCopyInfo,
	FR_State_Extent_Read,
	FR_State_Extent_ZeroOut,
	FR_State_Extent_PunchHole,
	FR_State_SnapshotList_Read,
	FR_State_Botched
} FiesReader_State;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	fies_id id;
	char *filename;
	char *linkdest;
	fies_sz size;
	uint32_t mode;
	void *opaque;
	struct FiesReader *reader;
} FiesReader_File;

typedef struct FiesReader {
	const struct FiesReader_Funcs *funcs;
	void *opaque;

	Map files; // { fies_id => FiesReader_File }

	bool eof;
	int errc; // negative errno code
	const char *errstr;
	jmp_buf jmpbuf;
	size_t warnings;

	struct {
		uint8_t *data;
		size_t capacity;
		size_t filled;
		size_t at;
	} buffer;

	uint32_t hdr_flags;
	uint32_t hdr_flags_required;
	uint32_t hdr_flags_rejected;

	uint16_t pkt_type;
	size_t pkt_size;

	struct fies_extent extent;
	fies_sz extent_at;
	FiesReader_File *snapshot_file;
	VectorOf(char*) snapshots;

	FiesReader_State state;
	FiesReader_File *newfile;
} FiesReader;
#pragma clang diagnostic pop

// internal:

static inline size_t
FiesReader_filled(const FiesReader *self) {
	return self->buffer.filled - self->buffer.at;
}

static inline const void*
FiesReader_data(const FiesReader *self) {
	return &self->buffer.data[self->buffer.at];
}

static inline bool
FiesReader_bufferFull(const FiesReader *self) {
	return self->buffer.at == 0 &&
	       self->buffer.filled == self->buffer.capacity;
}

#endif
