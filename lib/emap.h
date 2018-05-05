#ifndef FIES_SRC_EMAP_H
#define FIES_SRC_EMAP_H

#include "../include/fies.h"

#include <assert.h>
#include "vector.h"
#include "map.h"

// FIXME: Use a tree structure for this?
// Then again our data is too small (the keys consist of both phys+length and
// then the data would end up being only logical+file... Adding child pointers
// would be a 50% overhead...

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	fies_pos physical;
	fies_pos logical;
	fies_pos length;
	fies_id file;
} FiesEMapExtent;
#pragma clang diagnostic pop

static inline bool
FiesEMapExtent_shift(FiesEMapExtent *self, fies_pos by) {
	assert(by < self->length);
	self->physical += by;
	self->logical += by;
	self->length -= by;
	return self->length > 0;
}

typedef struct {
	MapOf(fies_pos, VectorOf(ExtentList)) devices;
} FiesEMap;

FiesEMap* FiesEMap_new(void);
void FiesEMap_delete(FiesEMap*);
void FiesEMap_clear(FiesEMap*);
typedef int FiesEMap_for_new(void *opaque, fies_pos pos, fies_sz len,
                             fies_pos physical);
typedef int FiesEMap_for_avail(void *opaque, fies_pos pos, fies_sz len,
                               fies_id file, fies_pos logical);
int FiesEMap_add(FiesEMap*,
                 fies_pos dev,
                 fies_pos phy,
                 fies_pos log,
                 fies_pos len,
                 fies_id file,
                 FiesEMap_for_new *for_new,
                 FiesEMap_for_avail *for_avail,
                 void *opaque);
#if 0
int FiesEMap_replace(FiesEMap*, fies_pos phy, fies_pos log, fies_pos len,
                     fies_id file);
#endif

static inline bool
FiesEMap_empty(const FiesEMap *self) {
	//return Vector_empty(&self->extents);
	return Map_empty(&self->devices);
}

#endif
