#include <stddef.h>
#include <stdint.h>

#include "crc.h"

uint32_t
crc32c(const void *data_, size_t length)
{
	static uint32_t table[256];
	if (!table[4]) {
		for (uint32_t i = 0; i != 256; ++i) {
			uint32_t c = i;
			for (size_t j = 0; j != 8; ++j)
				c = (c>>1) ^ (-(c&1) & 0x82F63B78);
			table[i] = c;
		}
	}
	const uint8_t *data = data_;
	uint32_t crc = 0xFFFFFFFFU;
	for (size_t i = 0; i != length; ++i)
		crc = (crc >> 8) ^ table[(data[i] ^ crc)&0xFF];
	return crc;
}
