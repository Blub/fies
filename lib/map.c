#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "map.h"
#include "util.h"

void
Map_init(Map         *self,
         Map_cmp     *compare,
         size_t       key_size,
         size_t       key_align,
         Vector_dtor *key_del,
         size_t       value_size,
         size_t       value_align,
         Vector_dtor *value_dtor)
{
	Vector_init(&self->keys, key_size, key_align);
	Vector_set_destructor(&self->keys, key_del);

	Vector_init(&self->values, value_size, value_align);
	Vector_set_destructor(&self->values, value_dtor);

	self->compare = compare;
}

Map*
Map_new(Map_cmp     *compare,
        size_t       key_size,
        size_t       key_align,
        Vector_dtor *key_del,
        size_t       value_size,
        size_t       value_align,
        Vector_dtor *value_dtor)
{
	Map *self = malloc(sizeof(*self));
	if (!self)
		return NULL;
	Map_init(self, compare,
	         key_size, key_align, key_del,
	         value_size, value_align, value_dtor);
	return self;
}

void
Map_destroy(Map *self)
{
	Vector_destroy(&self->keys);
	Vector_destroy(&self->values);
}

void
Map_delete(Map *self)
{
	Map_destroy(self);
	free(self);
}

void
Map_clear(Map *self)
{
	Vector_clear(&self->keys);
	Vector_clear(&self->values);
}

static bool
Map_findIndex(Map *self, const void *key, size_t *index)
{
	size_t a = 0;
	size_t b = Vector_length(&self->keys);
	if (!b) {
		*index = 0;
		return false;
	}
	while (a < b) {
		size_t i = (a+b)/2;
		const void *k = Vector_at(&self->keys, i);
		int c = self->compare(key, k);
		if      (c < 0) b = i;
		else if (c > 0) a = i+1;
		else {
			*index = i;
			return true;
		}
	}
	*index = b;
	if (b == Vector_length(&self->keys))
		return false;
	return self->compare(key, Vector_at(&self->keys, b)) == 0;
}

void*
Map_get(Map *self, const void *key)
{
	size_t index;
	if (Map_findIndex(self, key, &index))
		return Vector_at(&self->values, index);
	return NULL;
}

bool
Map_remove(Map *self, const void *key)
{
	size_t index;
	if (!Map_findIndex(self, key, &index))
		return false;
	Vector_remove(&self->keys, index, 1);
	Vector_remove(&self->values, index, 1);
	return true;
}

bool
Map_insert(Map *self, void *key, void *value)
{
	size_t index;
	if (Map_findIndex(self, key, &index)) {
		Vector_replace(&self->keys, index, key);
		Vector_replace(&self->values, index, value);
		return true;
	}
	Vector_insert(&self->keys, index, key);
	Vector_insert(&self->values, index, value);
	return false;
}

int
Map_strcmp(const void *a, const void *b)
{
	return strcmp(*(const char*const*)a, *(const char*const*)b);
}

void
Map_pfree(void *ptr)
{
	free(*(void**)ptr);
}
