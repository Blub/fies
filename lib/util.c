#include <errno.h>

#include "fies.h"
#include "util.h"

extern size_t
fies_mtree_decode(char *dst,
                  size_t dst_size,
                  const char *src,
                  size_t src_len)
{
	if (src_len == 0)
		return 0;

	size_t at = 0;
	const char *src_end = src + src_len;
	while (*src && src != src_end && (!dst || at != dst_size)) {
		if (*src == '\\') {
			if (++src == src_end)
				break;

			if (*src == '\\') {
				if (dst)
					dst[at] = '\\';
				++at;
				++src;
				continue;
			}

			uint8_t byte = 0;
			unsigned count = 0;
			while (src != src_end &&
			       count++ != 3 &&
			       *src >='0' && *src <= '7')
			{
				byte = byte*8 + (uint8_t)(*src - '0');
				++src;
			}
			if (dst)
				dst[at] = (char)byte;
			++at;
		} else {
			if (dst)
				dst[at] = *src;
			++at;
			++src;
		}
	}
	// we always nul-terminate
	if (at && at == dst_size)
		--at;
	if (dst)
		dst[at] = 0;
	if (src == src_end)
		errno = 0;
	else
		errno = ENOBUFS;
	return at;
}

extern size_t
fies_mtree_encode(char *dst,
                  size_t dst_size,
                  const char *src,
                  size_t src_len)
{
	if (src_len == 0)
		return 0;

	size_t at = 0;
	const char *src_end = src + src_len;
	while (*src && src != src_end && (!dst || at != dst_size)) {
		if (*src == '\\') {
			if (dst)
				dst[at] = '\\';
			if (++at == dst_size)
				break;
			if (dst)
				dst[at] = '\\';
			if (++at == dst_size)
				break;
		} else if (*src == ' ' || *src == '\t' || *src == '\f' ||
		           *src == '\r' || *src == '\n')
		{
			if (dst && at+4 >= dst_size)
				break;

			if (dst) {
				dst[at++] = '\\';
				dst[at++] = '0' + (((*src)>>6) & 007);
				dst[at++] = '0' + (((*src)>>3) & 007);
				dst[at++] = '0' + (((*src)>>0) & 007);
			} else {
				at += 4;
			}
		} else {
			if (dst)
				dst[at] = *src;
			++at;
		}
		++src;
	}
	// we always nul-terminate
	if (at && at == dst_size)
		--at;
	if (dst)
		dst[at] = 0;
	if (src == src_end)
		errno = 0;
	else
		errno = ENOBUFS;
	return at;
}
