#include <stdio.h> // for the alignment warning because everybody's a moron
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "vector.h"
#include "util.h"

void
Vector_init(Vector *self, size_t entry_size, size_t align)
{
	self->data = NULL;
	self->count = 0;
	self->capacity = 0;
	self->entry_size = entry_size;
	self->entry_align = align;
	self->slot_size = FIES_ALIGN_UP(entry_size, align);
	self->dtor = NULL;
}

void
Vector_clear(Vector *self)
{
	if (self->dtor) {
		void *ptr;
		Vector_foreach(self, ptr)
			self->dtor(ptr);
	}
	free(self->data);
	self->data = NULL;
	self->count = 0;
	self->capacity = 0;
}

void
Vector_destroy(Vector *self)
{
	Vector_clear(self);
}

// FIXME: expose this to the rest of the code base as aligned_realloc()?
static void*
realign(size_t align, void *data, size_t newsize)
{
	// C11 has aligned_alloc but neither C11 nor POSIX nor any other
	// decent system has aligned_REalloc, however, the realloc code path
	// will almost never be executed in our code anyway.
	data = realloc(data, newsize);
	uintptr_t dataptr = (uintptr_t)data;
	if ((dataptr % align) == 0)
		return data;
#ifndef NO_DEBUG
	fprintf(stderr, "realloc() botched the alignment\n");
#endif
	void *newdata = aligned_alloc(align, newsize);
	memcpy(newdata, data, newsize);
	free(data);
	return newdata;
}

static void
Vector_realloc(Vector *self, size_t newcap)
{
	size_t newsize = newcap * self->slot_size;
	uint8_t *newdata = realign(self->entry_align, self->data, newsize);
	self->data = newdata;
	self->capacity = newcap;
}

static inline size_t
next_capacity(size_t cap)
{
	// FIXME: grow by a fixed amount at some point?
	return cap ? cap*2 : 8;
}

static void
Vector_more(Vector *self)
{
	Vector_realloc(self, next_capacity(self->capacity));
}

static void
Vector_less(Vector *self)
{
	size_t newcap = self->capacity;
	while (self->count < newcap/2)
		newcap /= 2;
	if (newcap == self->capacity)
		return;
	Vector_realloc(self, newcap);
}

void
Vector_push(Vector *self, void *entry)
{
	if (self->count == self->capacity)
		Vector_more(self);
	memcpy(Vector_end(self), entry, self->entry_size);
	self->count++;
}

void
Vector_insert(Vector *self, size_t index, void *entry)
{
	if (index == self->count) {
		Vector_push(self, entry);
		return;
	}

	// When realloc+move is too slow with big files emap should probably
	// switch to a tree ;-)
	if (self->count == self->capacity)
		Vector_more(self);
	void *dest = Vector_at(self, index);
	void *after = Vector_at(self, index+1);
	memmove(after, dest, (self->count - index) * self->slot_size);
	memcpy(dest, entry, self->entry_size);
	self->count++;
}

void
Vector_pop(Vector *self)
{
	if (!self->count)
		return;
	if (self->dtor)
		self->dtor(Vector_last(self));
	self->count--;
	Vector_less(self);
}

void
Vector_remove(Vector *self, size_t index, size_t count)
{
	const size_t end = index + count;
	if (self->dtor) {
		for (size_t i = index; i != end; ++i)
			self->dtor(Vector_at(self, i));
	}
	size_t remaining = self->count - end;
	memmove(Vector_at(self, index),
	        Vector_at(self, end),
	        remaining * self->slot_size);
	self->count -= count;
	Vector_less(self);
}

void
Vector_replace(Vector *self, size_t index, void *entry)
{
	void *p = Vector_at(self, index);
	if (self->dtor)
		self->dtor(p);
	memcpy(p, entry, self->entry_size);
}
