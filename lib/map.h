#ifndef FIES_SRC_MAP_H
#define FIES_SRC_MAP_H

#include <stdbool.h>

#include "vector.h"

// Keys in this map must be at most pointer sized.
// Values can be arbitrary.

typedef int Map_cmp(const void*, const void*);

typedef struct {
	Vector keys;
	Vector values;
	Map_cmp *compare;
} Map;

#define MapOf(SRC,DST) Map

void  Map_init(Map         *self,
               Map_cmp     *compare,
               size_t       key_size,
               size_t       key_align,
               Vector_dtor *key_del,
               size_t       value_size,
               size_t       value_align,
               Vector_dtor *value_dtor);
Map*  Map_new(Map_cmp     *compare,
              size_t       key_size,
              size_t       key_align,
              Vector_dtor *key_del,
              size_t       value_size,
              size_t       value_align,
              Vector_dtor *value_dtor);
void  Map_destroy(Map*);
void  Map_delete (Map*);
void  Map_clear  (Map*);
void* Map_get    (Map*, const void *key_pointer);
bool  Map_remove (Map*, const void *key_pointer);
bool  Map_insert (Map*, void *key_pointer, void *value_pointer);

int  Map_strcmp(const void*, const void*);
void Map_pfree(void*);

static inline void*
Map_getp(Map *self, const void *key)
{
	return Map_get(self, &key);
}

static inline void*
PMap_get(Map *self, const void *key)
{
	void **v = Map_get(self, key);
	return v ? *v : NULL;
}

static inline void*
PMap_getp(Map *self, const void *key)
{
	return PMap_get(self, &key);
}

static inline bool
Map_removep(Map *self, const void *key)
{
	return Map_remove(self, &key);
}

static inline bool
Map_insertp(Map *self, void *key, void *value)
{
	return Map_insert(self, &key, value);
}

static inline bool
PMap_insert(Map *self, void *key, void *value)
{
	return Map_insert(self, key, &value);
}

static inline bool
PMap_insertp(Map *self, void *key, void *value)
{
	return Map_insert(self, &key, &value);
}

static inline bool
Map_empty(const Map *self)
{
	return Vector_empty(&self->keys);
}

#define \
Map_init_type(M, C, KT, KD, VT, VD) \
	Map_init((M), (C), \
	         sizeof(KT), _Alignof(KT), (KD), \
	         sizeof(VT), _Alignof(VT), (VD))

#define \
Map_new_type(C, KT, KD, VT, VD) \
	Map_new((C), \
	        sizeof(KT), _Alignof(KT), (KD), \
	        sizeof(VT), _Alignof(VT), (VD))

#define \
Map_foreach(M, KP, VP) \
	for ((KP) = Vector_begin(&(M)->keys), (VP) = Vector_begin(&(M)->values); \
	     (KP) != Vector_end(&(M)->keys); \
	     Vector_advance(&(M)->keys, (KP)), Vector_advance(&(M)->values, (VP)))

#endif
