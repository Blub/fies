#ifndef FIES_SRC_UTIL_H
#define FIES_SRC_UTIL_H

#include <stdint.h>
#include <stdlib.h>
#include <endian.h>
#include <string.h>

//include "../config.h"

// Useful macros
#define FIES_ALIGN_DOWN(X, A) \
	( ((X)/(A))*(A) )
#define FIES_ALIGN_UP(X, A) \
	FIES_ALIGN_DOWN((X)+((A)-1), (A))

#ifndef __has_c_attribute
#  define __has_c_attribute(x) 0
#endif

// Attributes
#define FIES_SENTINEL __attribute__((__sentinel__))
#if __has_c_attribute(fallthrough)
#  define FIES_FALLTHROUGH [[fallthrough]]
#elif __GNUC__ >= 7
#  define FIES_FALLTHROUGH __attribute__((fallthrough))
#elif defined(__clang__)
#  define FIES_FALLTHROUGH
#else
#  define FIES_FALLTHROUGH
#endif

// Allocation
static inline void*
u_malloc0(size_t size) {
	void *data = malloc(size);
	if (data) memset(data, 0, size);
	return data;
}

static inline void*
u_memdup(const void *src, size_t size) {
	void *data = malloc(size);
	if (data) memcpy(data, src, size);
	return data;
}

static inline void
u_strfreev(char **v) {
	if (v) for (char **i = v; *i; ++i) free(*i);
	free(v);
}

static inline void
u_strptrfree(char **v) {
	free(*v);
}

static inline char*
u_strmemdup(const void *src, size_t size) {
	char *data = malloc(size+1);
	if (data) {
		memcpy(data, src, size);
		data[size] = 0;
	}
	return data;
}

extern size_t fies_mtree_encode(char *dst,
                                size_t dst_size,
                                const char *src,
                                size_t src_len);
extern size_t fies_mtree_decode(char *dst,
                                size_t dst_size,
                                const char *src,
                                size_t src_len);

// Endianess:
#define FIES_BSWAP8(S,X)  (X)
#define FIES_BSWAP16(S,X) ( (S##int16_t) ( \
                            ((((uint16_t)(X))>>8)&0x00FFL) | \
                            ((((uint16_t)(X))<<8)&0xFF00L) ) )
#define FIES_BSWAP32(S,X) ( (S##int32_t) ( \
                            ((((uint32_t)(X))>>24)&0x000000FFL) | \
                            ((((uint32_t)(X))>> 8)&0x0000FF00L) | \
                            ((((uint32_t)(X))<< 8)&0x00FF0000L) | \
                            ((((uint32_t)(X))<<24)&0xFF000000L) ) )
#define FIES_BSWAP64(S,X) ( (S##int64_t) ( \
                            (FIES_BSWAP32(u,((uint64_t)(X))<<32)) | \
                            (FIES_BSWAP32(u,((uint64_t)(X))>>32)) ) )

#ifdef CONFIG_BIG_ENDIAN
# define FIES_LITTLE(S,N,X) FIES_BSWAP##N(S,(X))
#else
# define FIES_LITTLE(S,N,X) (X)
#endif

static inline uint8_t  u_le8 (uint8_t  v) { return FIES_LITTLE(u, 8,  v); }
static inline uint16_t u_le16(uint16_t v) { return FIES_LITTLE(u, 16, v); }
static inline uint32_t u_le32(uint32_t v) { return FIES_LITTLE(u, 32, v); }
static inline uint64_t u_le64(uint64_t v) { return FIES_LITTLE(u, 64, v); }
static inline int8_t   i_le8 (int8_t  v)  { return FIES_LITTLE( , 8,  v); }
static inline int16_t  i_le16(int16_t v)  { return FIES_LITTLE( , 16, v); }
static inline int32_t  i_le32(int32_t v)  { return FIES_LITTLE( , 32, v); }
static inline int64_t  i_le64(int64_t v)  { return FIES_LITTLE( , 64, v); }
static inline void     le_broken(void) {}

#ifdef CONFIG_BIG_ENDIAN
#  define FIES_LE(X) _Generic((X), \
     uint8_t:  u_le8, \
     uint16_t: u_le16, \
     uint32_t: u_le32, \
     uint64_t: u_le64, \
     int8_t:   i_le8, \
     int16_t:  i_le16, \
     int32_t:  i_le32, \
     int64_t:  i_le64, \
     const uint8_t:  u_le8, \
     const uint16_t: u_le16, \
     const uint32_t: u_le32, \
     const uint64_t: u_le64, \
     const int8_t:   i_le8, \
     const int16_t:  i_le16, \
     const int32_t:  i_le32, \
     const int64_t:  i_le64, \
     default: le_broken \
     )(X)
#else
#  define FIES_LE(X) (X)
#endif

#define SwapLE(X) do { (X) = FIES_LE((X)); } while (0)

#endif
