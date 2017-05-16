#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "emap.h"

typedef struct {
	VectorOf(FiesEMapExtent) extents;
} ExtentList;

static void
ExtentList_init(ExtentList *self)
{
	Vector_init_type(&self->extents, FiesEMapExtent);
}

static void
ExtentList_destroy(ExtentList *self)
{
	Vector_destroy(&self->extents);
}

static int
fies_pos_cmp(const void *pa, const void *pb)
{
	const fies_pos *a = pa, *b = pb;
	return (*a < *b) ? -1 :
	       (*a > *b) ?  1 : 0;
}

static void
FiesEMap_init(FiesEMap *self)
{
	Map_init_type(&self->devices, fies_pos_cmp,
	              fies_pos, NULL,
	              VectorOf(ExtentList), (Vector_dtor*)ExtentList_destroy);
	//Vector_init_type(&self->extents, FiesEMapExtent);
}

FiesEMap*
FiesEMap_new()
{
	FiesEMap *self= malloc(sizeof(*self));
	if (!self)
		return NULL;
	FiesEMap_init(self);
	return self;
}

static void
FiesEMap_destroy(FiesEMap *self)
{
	Map_destroy(&self->devices);
}

void
FiesEMap_delete(FiesEMap *self)
{
	FiesEMap_destroy(self);
	free(self);
}

void
FiesEMap_clear(FiesEMap *self)
{
	//Vector_clear(&self->extents);
	Map_clear(&self->devices);
}

// simple binary search for an insertion point for 'pos'
static size_t
ExtentList_find(ExtentList *self, fies_pos pos)
{
	size_t a = 0;
	size_t b = Vector_length(&self->extents);
	if (b == 0)
		return 0;
	while (a != b) {
		size_t i = (a+b)/2;
		const FiesEMapExtent *elem = Vector_at(&self->extents, i);
		if (pos < elem->physical)
			b = i;
		else if (pos >= (elem->physical + elem->length))
			a = i+1;
		else
			return i;
	}
	return a;
}

static ExtentList*
FiesEMap_getDevice(FiesEMap *self, fies_pos device)
{
	ExtentList *dev = Map_get(&self->devices, &device);
	if (dev)
		return dev;
	ExtentList el;
	ExtentList_init(&el);
	Map_insert(&self->devices, &device, &el);
	return Map_get(&self->devices, &device);
}

int
FiesEMap_add(FiesEMap *self,
              fies_pos in_device,
              fies_pos in_physical,
              fies_pos in_logical,
              fies_pos in_length,
              fies_id in_file,
              FiesEMap_for_new *for_new,
              FiesEMap_for_avail *for_avail,
              void *opaque)
{
	if (!in_length)
		return 0;

	ExtentList *device = FiesEMap_getDevice(self, in_device);
	if (!device)
		return -errno;

	FiesEMapExtent ex = {
		in_physical,
		in_logical,
		in_length,
		in_file
	};

	size_t index = ExtentList_find(device, in_physical);
	if (index >= Vector_length(&device->extents)) {
		int rc = for_new(opaque, ex.logical, ex.length);
		if (rc < 0)
			return rc;
		Vector_push(&device->extents, &ex);
		return 0;
	}

	while (index != Vector_length(&device->extents)) {
		const FiesEMapExtent *it = Vector_at(&device->extents, index);
		if ((ex.physical+ex.length) <= it->physical) {
			// nearest extent doesn't overlap
			int rc = for_new(opaque, ex.logical, ex.length);
			if (rc < 0)
				return rc;
			Vector_insert(&device->extents, index, &ex);
			return 0;
		}

		if (ex.physical < it->physical) {
			fies_pos len = it->physical - ex.physical;
			int rc = for_new(opaque, ex.logical, len);
			if (rc < 0)
				return rc;
			FiesEMapExtent front = {
				ex.physical,
				ex.logical,
				len,
				ex.file
			};
			Vector_insert(&device->extents, index, &front);
			if (!FiesEMapExtent_shift(&ex, len))
				return 0;
			++index;
			it = Vector_at(&device->extents, index);
		}

		fies_pos phys_off = ex.physical - it->physical;
		fies_pos copy_logical = it->logical + phys_off;
		const fies_pos it_phy_end = it->physical + it->length;
		if (it_phy_end >= (ex.physical+ex.length)) {
			// 'it' covers the entire rest of 'ex'
			return for_avail(opaque, ex.logical, ex.length,
			                 it->file, copy_logical);
		}
		fies_pos len = it_phy_end - ex.physical;
		int rc = for_avail(opaque, ex.logical, len, it->file,
		                   copy_logical);
		if (rc < 0)
			return rc;
		if (!FiesEMapExtent_shift(&ex, len))
			return 0;
		++index;
	}
	assert(ex.length);
	int rc = for_new(opaque, ex.logical, ex.length);
	if (rc < 0)
		return rc;
	Vector_push(&device->extents, &ex);
	return 0;
}

#if 0
// Untested, not needed
int FiesEMap_replace(FiesEMap *self,
                     fies_pos in_physical,
                     fies_pos in_logical,
                     fies_pos in_length,
                     fies_id in_file)
{
	if (!in_length)
		return 0;

	FiesEMapExtent ex = {
		in_physical,
		in_logical,
		in_length,
		in_file
	};

	size_t index = ExtentList_find(self, in_physical);
	if (index >= Vector_length(&self->extents)) {
		Vector_push(&self->extents, &ex);
		return 0;
	}

	FiesEMapExtent *slot = Vector_at(&self->extents, index);
	if (slot->file != in_file) {
		if (slot->physical < in_physical) {
			slot->length = in_physical - slot->physical;
			++index;
			Vector_insert(&self->extents, index, &ex);
			slot = Vector_at(&self->extents, index);
		} else if (slot->physical == in_physical) {
			memcpy(slot, &ex, sizeof(*slot));
		} else {
			Vector_insert(&self->extents, index, &ex);
			slot = Vector_at(&self->extents, index);
		}
	}
	
	if (slot->physical > in_physical) {
		// extent slot to the left
		fies_pos end = slot->physical + slot->length;
		slot->physical = in_physical;
		slot->length = end - slot->physical;
		if (index > 0) {
			// merge with the left side
			FiesEMapExtent *left = Vector_at(&self->extents,
			                                 index-1);
			if (left->file == in_file &&
			    left->physical + left->length >= slot->physical)
			{
				left->length = end - left->physical;
				Vector_remove(&self->extents, index, 1);
				--index;
				slot = Vector_at(&self->extents, index);
			}
		}
	}

	// are we done?
	fies_pos in_end = in_physical + in_length;
	if (slot->physical + slot->length >= in_end)
		return 0;

	// extend slot to the right
	slot->length = in_end - slot->physical;

	// Was this the last entry?
	if (index+1 == Vector_length(&self->extents))
		return 0;

	// Now remove all entries we grew over
	size_t i = index + 1;
	size_t del_from = i;
	while (del_from != Vector_length(&self->extents)) {
		FiesEMapExtent *right = Vector_at(&self->extents, i);
		fies_pos end = right->physical + right->length;
		if (end > in_end)
			break;
		++i;
	}
	if (del_from != i) {
		Vector_remove(&self->extents, del_from, i-del_from);
		slot = Vector_at(&self->extents, index);
	}

	// Is it the last entry now?
	if (index+1 == Vector_length(&self->extents))
		return 0;

	// Does the next one overlap?
	FiesEMapExtent *right = Vector_at(&self->extents, index+1);
	if (right->physical >= in_end)
		return 0;
	fies_pos end = right->physical + right->length;
	// Can we merge it?
	if (right->file == in_file) {
		slot->length = end - slot->physical;
		Vector_remove(&self->extents, index+1, 1);
	} else { // if (right->physical < in_end) {
		right->physical = in_end;
		right->length = end - right->physical;
	}

	return 0;
}
#endif
