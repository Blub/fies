#ifndef FIES_SRC_FIES_CLI_COMMON_H
#define FIES_SRC_FIES_CLI_COMMON_H

#include <stdio.h>
#include <stdarg.h>

#include "../lib/vector.h"

#define EUSAGE -1

typedef struct FiesReader FiesReader;
typedef struct FiesWriter FiesWriter;
typedef struct FiesFile FiesFile;
typedef struct FiesFile_Extent FiesFile_Extent;

typedef struct {
	// not actually options...
	unsigned int error_count;
	unsigned int warn_count;
	unsigned int warn_enable;
	unsigned int verbose;
	unsigned int quiet;
} Common;

extern Common common;

bool handle_file_re_opt(const char *pattern, Vector *dest, const char *prog);

enum {
# define FIES_ADD(X) WARN_##X,
#  include "warnlist.h"
# undef FIES_ADD
	WARN_MAX
};
static const char * const warn_names[] = {
# define FIES_ADD(X) #X,
#  include "warnlist.h"
# undef FIES_ADD
	NULL
};

enum {
	VERBOSE_NONE,
	VERBOSE_FILES      = 1,
	VERBOSE_EXCLUSIONS = 2,
	VERBOSE_ACTIONS    = 3
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
static inline void
showerr(const char *fmt, ...)
{
	common.error_count++;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static inline bool
warn_onetime(unsigned int what) {
	if (common.warn_enable & (1<<what))
		return false;
	if (what == WARN_ABSOLUTE_PATHS)
		common.warn_enable |= (1<<WARN_ABSOLUTE_PATHS);
	return true;
}

static inline void
warn(unsigned int what, const char *fmt, ...)
{
	if (!warn_onetime(what))
		return;
	common.warn_count++;
	(void)what; // FIXME: ability to disable warnings
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

static inline void
verbose(unsigned int level, const char *fmt, ...)
{
	if (common.verbose < level)
		return;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
#pragma GCC diagnostic pop

#endif
