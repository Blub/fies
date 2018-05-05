#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <inttypes.h>
#include <regex.h>

#include "../lib/fies.h"
#include "../lib/vector.h"

#include "cli_common.h"
#include "util.h"

bool
parse_long(const char **str, long *numptr)
{
	char *endp = NULL;
	errno = 0;
	*numptr = strtol(*str, &endp, 0);
	*str = endp;
	return !errno;
}

bool
str_to_long(const char *str, long *numptr)
{
	return parse_long(&str, numptr) && str && !*str;
}

bool
arg_stol(const char *str, long *numptr, const char *errstr, const char *arg0)
{
	if (str_to_long(str, numptr))
		return true;
	if (errno)
		fprintf(stderr, "%s: %s: %s: %s\n",
		        arg0, errstr, strerror(errno), str);
	else
		fprintf(stderr, "%s: %s: %s\n", arg0, errstr, str);
	return true;
}

bool
str_to_ulong(const char *str, unsigned long *numptr)
{
	char *endp = NULL;
	errno = 0;
	*numptr = strtoul(str, &endp, 0);
	return endp && !*endp && !errno;
}

bool
str_to_bool(const char *str, bool default_value)
{
	if (!str)
		return default_value;
	return strcmp(str, "1") == 0 ||
	       strcasecmp(str, "on") == 0 ||
	       strcasecmp(str, "yes") == 0 ||
	       strcasecmp(str, "true") == 0 ||
	       strcasecmp(str, "enabled") == 0;
}

char*
make_path(const char *base, ...)
{
	va_list ap;
	size_t len = strlen(base);
	size_t clen;

	const char *component;
	va_start(ap, base);
	while ((component = va_arg(ap, const char*))) {
		clen = strlen(component);
		if (!clen)
			continue;
		if (component[clen-1] != '/')
			++len;
		len += clen;
	}
	va_end(ap);

	char *path = malloc(len+1);
	if (!path)
		return NULL;
	char *at = path;

	const char *prev = base;

	clen = strlen(prev);
	memcpy(at, prev, clen);
	at += clen;
	// if the first component is empty don't turn it into a slash
	//   => ( (clen==0) )
	bool hadslash = clen == 0 || prev[clen-1] == '/';

	va_start(ap, base);
	while ((component = va_arg(ap, const char*))) {
		clen = strlen(component);
		if (!clen)
			continue;
		if (!hadslash)
			*at++ = '/';
		memcpy(at, component, clen);
		hadslash = at[clen-1] == '/';
		at += clen;
	}
	va_end(ap);
	*at = 0;
	return path;
}

int
path_parts(const char *inpath, char **dir, char **base, int flags)
{
	if (!*inpath) {
		*dir = NULL;
		*base = NULL;
		return -ENOENT;
	}

	char *last_slash = strrchr(inpath, '/');
	if (!last_slash) {
		if (flags & PATH_PARTS_RELATIVE_DOT) {
			*dir = strdup(".");
			if (!*dir)
				return -errno;
		} else {
			*dir = NULL;
		}
		*base = strdup(inpath);
		if (!*base) {
			int rc = -errno;
			free(*dir);
			return -rc;
		}
		return 0;
	}

	size_t len;
	if (!last_slash[1]) {
		// trailing slash
		*base = NULL;
		while (last_slash != inpath && *last_slash == '/')
			--last_slash;
		if (last_slash == inpath) {
			// path consists only of slashes, let the code below
			// fill dir with "/"
			++last_slash;
		}
	} else {
		*base = strdup(last_slash+1);
		// for absolute paths with no subdir make the code
		// below fill dir with "/".
		if (last_slash == inpath)
			++last_slash;
	}

	len = (size_t)(last_slash - inpath);
	*dir = malloc(len+1);
	if (!*dir)
		return -errno;
	memcpy(*dir, inpath, len);
	(*dir)[len] = 0;
	return 0;
}

const char*
next_tail_component(const char *path, size_t *pi)
{
	if (!*pi)
		return NULL;

	size_t i = *pi;
	// skip trailing slashes
	while (i && path[i] == '/')
		--i;
	if (!i) {// path is all slashes
		*pi = i;
		return path;
	}
	// find the first directory separating slash
	while (i && path[i] != '/')
		--i;
	*pi = i;
	return path+i + (path[i] == '/' ? 1 : 0);
}

char*
strip_components(const char *path, size_t count)
{
	if (path[0] == '/') {
		warn(WARN_ABSOLUTE_PATHS,
		     "stripping leading slashes from entries\n");
		while (*path == '/')
			++path;
	}

	const char *prev = path;
	while (count--) {
		const char *slash = strchr(prev, '/');
		if (!slash)
			break;
		while (slash && *slash == '/')
			++slash;
		prev = slash;
	}
	return strdup(prev);
}

// I'd just split at spaces no matter what, but this is just more convenient
// for most use cases. Although the general recommendation for the -b option
// is still to just use one single command and pass %n %s as exactly two
// parameters. -bsnapshot="/my/snapshot/script %n %s"
// %n and %s will be expanded after splitting so this is the correct way.
// ==> WRONG would be: -bsnapshot="/my/snapshot/script '%n' '%s'"
// FIXME: ^ document this in a more visible place
char**
split_command(const char *cmd)
{
	VectorOf(char*) argv;

	size_t len = strlen(cmd);
	char *buffer = malloc(len+1);
	size_t at = 0;
	bool escaped = false;
	char quote = 0;

	Vector_init(&argv, sizeof(char*), _Alignof(char*));

	for (; *cmd; ++cmd) {
		if (escaped) {
			escaped = false;
			int switchchar = (quote == '"' ? *cmd : 0);
			switch (switchchar) {
			case 'n': buffer[at++] = '\n'; break;
			case 'r': buffer[at++] = '\r'; break;
			case 't': buffer[at++] = '\t'; break;
			case 'f': buffer[at++] = '\f'; break;
			case 'v': buffer[at++] = '\v'; break;
			case 'a': buffer[at++] = '\a'; break;
			case 'b': buffer[at++] = '\b'; break;
			default: buffer[at++] = *cmd; break;
			}
			continue;
		}
		if (*cmd == quote) {
			quote = 0;
			continue;
		} else if (quote != '\'' && *cmd == '\\') {
			escaped = true;
		} else if (!quote && (*cmd == '"' || *cmd == '\'')) {
			quote = *cmd;
		} else if (!quote && *cmd == ' ') {
			if (at) {
				buffer[at++] = 0;
				char *arg = malloc(at);
				memcpy(arg, buffer, at);
				at = 0;
				Vector_push(&argv, &arg);
			}
		} else {
			buffer[at++] = *cmd;
		}
	}
	if (at) {
		buffer[at++] = 0;
		char *arg = malloc(at);
		memcpy(arg, buffer, at);
		Vector_push(&argv, &arg);
	}
	free(buffer);
	if (!Vector_length(&argv))
		return NULL;
	buffer = NULL;
	Vector_push(&argv, &buffer);
	return Vector_release(&argv);
}

static size_t
CountSubstrings(const char *str, const char *sub, size_t sublen)
{
	size_t count = 0;
	char *at = strstr(str, sub);
	while (at) {
		++count;
		at = strstr(at+sublen, sub);
	}
	return count;
}

static char*
ReplaceStrsKnownSize(char *str,
                     size_t oldlen,
                     size_t newlen,
                     const char *what,
                     size_t whatlen,
                     const char *with,
                     size_t withlen)
{
	char *out = malloc(newlen+1);
	char *at = out;
	const char *prev = str;
	char *sub = strstr(str, what);
	while (sub) {
		size_t partlen = (size_t)(sub-prev);
		memcpy(at, prev, partlen);
		at += partlen;
		memcpy(at, with, withlen);
		at += withlen;
		prev = sub + whatlen;
		sub = strstr(prev, what);
	}
	sub = str + oldlen;
	size_t partlen = (size_t)(sub-prev);
	memcpy(at, prev, partlen);
	at[partlen] = 0;
	return out;
}

char*
vstr_replace(const char *in_str, va_list ap)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"
	char *str = (char*)in_str;
#pragma GCC diagnostic pop
	size_t len = strlen(str);

	while (true) {
		const char *what = va_arg(ap, const char*);
		if (!what)
			break;
		const char *with = va_arg(ap, const char*);
		if (!with)
			continue;
		const size_t whatlen = strlen(what);
		const size_t withlen = strlen(with);
		const size_t count = CountSubstrings(str, what, whatlen);
		assert(len + count*withlen >= count*whatlen);
		size_t newlen = len + count*withlen - count*whatlen;

		char *newstr = ReplaceStrsKnownSize(str, len, newlen,
		                                    what, whatlen,
		                                    with, withlen);
		if (str != in_str)
			free(str);
		str = newstr;
		len = newlen;
	}
	if (str == in_str) {
		str = malloc(len+1);
		memcpy(str, in_str, len+1);
	}
	return str;
}

char*
str_replace(const char *str, ...)
{
	va_list ap;
	va_start(ap, str);
	char *result = vstr_replace(str, ap);
	va_end(ap);
	return result;
}

char**
vstrvec_replace(const char *const *strv, va_list ap)
{
	size_t i = 0;
	do {} while (strv[i++]);
	char **out = malloc(i * sizeof(*out));
	for (i = 0; strv[i]; ++i) {
		va_list cap;
		va_copy(cap, ap);
		out[i] = vstr_replace(strv[i], cap);
		va_end(cap);
	}
	out[i] = NULL;
	return out;
}

char**
strvec_replace(const char *const *strv, ...)
{
	va_list ap;
	va_start(ap, strv);
	char **result = vstrvec_replace(strv, ap);
	va_end(ap);
	return result;
}

void
format_size(unsigned long long insize, char *buffer, size_t bufsize)
{
	fies_sz size = insize;
	static const char suffixes[] = " KMGTPE";
	size_t u = 0;
	while (u < sizeof(suffixes)-1 && size > 1024) {
		size = (size+512)>>10;
		++u;
	}
	if (!u)
		snprintf(buffer, bufsize, "%" PRI_D_FIES_SZ, size);
	else if ((insize & (insize-1)) == 0)
		snprintf(buffer, bufsize, "%" PRI_D_FIES_SZ "%c",
		         size, suffixes[u]);
	else
		snprintf(buffer, bufsize, "%.2f%c",
		         (double)insize / (double)(1ULL<<(10*u)), suffixes[u]);
}

int
multiply_size(unsigned long long *size, const char *suffix)
{
	switch (*suffix) {
		default:
			return -1;
		case 't': case 'T': *size *= 1024; FIES_FALLTHROUGH;
		case 'g': case 'G': *size *= 1024; FIES_FALLTHROUGH;
		case 'r': case 'M': *size *= 1024; FIES_FALLTHROUGH;
		case 'k': case 'K': *size *= 1024;
			return 1;
		case 0: return 0;
	}
}

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
