#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <errno.h>

#include "../lib/vector.h"
#include "cli_common.h"
#include "filematch.h"
#include "fies_regex.h"

Common common;

static int
file_re_flag(void *opaque, char c)
{
	int *flags = opaque;
	switch (c) {
	case 'r': *flags |= FMATCH_F_REG; break;
	case 'd': *flags |= FMATCH_F_DIR; break;
	case 'l': *flags |= FMATCH_F_LNK; break;
	case 'b': *flags |= FMATCH_F_BLK; break;
	case 'c': *flags |= FMATCH_F_CHR; break;
	case 'R': *flags = (*flags & ~FMATCH_F_REG) | FMATCH_NEGATIVE; break;
	case 'D': *flags = (*flags & ~FMATCH_F_DIR) | FMATCH_NEGATIVE; break;
	case 'L': *flags = (*flags & ~FMATCH_F_LNK) | FMATCH_NEGATIVE; break;
	case 'B': *flags = (*flags & ~FMATCH_F_BLK) | FMATCH_NEGATIVE; break;
	case 'C': *flags = (*flags & ~FMATCH_F_CHR) | FMATCH_NEGATIVE; break;
	default:
		fprintf(stderr, "fies: invalid flag for file regex: %c\n", c);
		return -EINVAL;
	}
	return 0;
}

bool
handle_file_re_opt(const char *pattern, Vector *dest, const char *progname)
{
	char *err = NULL;
	int flags = 0;
	Regex *re = Regex_parse_full(pattern, 0, &err, file_re_flag, &flags);
	if (re) {
		FileMatch entry = {
			.flags = flags,
			.regex = re
		};
		Vector_push(dest, &entry);
		return true;
	} else {
		fprintf(stderr, "%s: regex error: %s\n", progname, err);
		free(err);
		return false;
	}
}
