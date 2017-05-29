#ifndef FIES_SRC_VECTOR_H
#define FIES_SRC_VECTOR_H

#include <stdint.h>
#include <stdbool.h>

typedef void Vector_dtor(void*);

typedef struct {
	void *data;
	size_t count;
	size_t capacity;
	size_t entry_size;
	size_t entry_align;
	size_t slot_size; // entry size aligned to entry alignment
	Vector_dtor *dtor;
} Vector;

// The type in macro is purely informational.
#define VectorOf(Type) Vector

void   Vector_init(Vector*, size_t entry_size, size_t align);
void   Vector_clear(Vector*);
void   Vector_destroy(Vector*);
void   Vector_push(Vector*, const void*);
void   Vector_pop(Vector*);
void   Vector_insert(Vector*, size_t index, const void*);
size_t Vector_search(Vector*, const void *value, int(*cmp)(const void*));
void   Vector_remove(Vector*, size_t index, size_t count);
void   Vector_replace(Vector*, size_t index, const void*);
void*  Vector_appendUninitialized(Vector*, size_t count);

static inline void
Vector_set_destructor(Vector *self, Vector_dtor *dtor) {
	self->dtor = dtor;
}

static inline Vector_dtor*
Vector_destructor(Vector *self) {
	return self->dtor;
}

static inline size_t
Vector_length(const Vector *self) {
	return self->count;
}

static inline bool
Vector_empty(const Vector *self) {
	return self->count == 0;
}

static inline void*
Vector_at(Vector *self, size_t index) {
	return ((uint8_t*)self->data) + index * self->slot_size;
}

static inline void*
Vector_data(Vector *self) {
	return self->data;
}

static inline void*
Vector_last(Vector *self) {
	return Vector_at(self, self->count-1);
}

static inline void*
Vector_begin(Vector *self) {
	return Vector_at(self, 0);
}

static inline void*
Vector_end(Vector *self) {
	return Vector_at(self, self->count);
}

static inline void*
Vector_release(Vector *self) {
	void *data = self->data;
	self->data = NULL;
	self->count = 0;
	self->capacity = 0;
	return data;
}

#define \
Vector_advance(VEC, PTR) \
	(*((uint8_t**)&(PTR)) += (VEC)->slot_size)

#define \
Vector_foreach(VEC, PTR) \
	for ((PTR) = Vector_begin(VEC); \
	     (PTR) != Vector_end(VEC); \
	     Vector_advance((VEC), (PTR)))

#define \
Vector_init_type(VEC, TYP) \
	Vector_init((VEC), sizeof(TYP), _Alignof(TYP))

#endif
