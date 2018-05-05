#ifndef FIES_SRC_CLI_UTIL_H
#define FIES_SRC_CLI_UTIL_H

#include <stdarg.h>

#include "../lib/util.h"
#include "../lib/vector.h"

#define PATH_PARTS_RELATIVE_DOT 0x01
#define PATH_PARTS_ALWAYS       0x02

bool parse_long(const char **str, long *numptr);
bool str_to_long(const char *str, long *numptr);
bool str_to_ulong(const char *str, unsigned long *numptr);
bool str_to_bool(const char *str, bool default_value);
bool arg_stol(const char *str, long *nump, const char *err, const char *arg0);

char* make_path(const char *base, ...) FIES_SENTINEL;
int path_parts(const char *inpath, char **dir, char **base, int flags);
char *strip_components(const char *path, size_t count);

const char *next_tail_component(const char *path, size_t *i);

char **split_command(const char *cmd);

char *str_replace(const char *str, ...) FIES_SENTINEL;
char *vstr_replace(const char *str, va_list);

char **strvec_replace(const char *const *strv, ...) FIES_SENTINEL;
char **vstrvec_replace(const char *const *strv, va_list);

void format_size(unsigned long long size, char *buffer, size_t bufsize);
int multiply_size(unsigned long long *size, const char *suffix);

uint32_t crc32c(const void *data, size_t length);

#endif
