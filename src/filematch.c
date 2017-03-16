#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <fnmatch.h>

#include "../config.h"

#include "cli_common.h"
#include "filematch.h"

void
FileMatch_destroy(FileMatch *self)
{
	Regex_destroy(self->regex);
}

bool
FileMatch_matches(FileMatch *self, const char *path, mode_t st_mode)
{
	int fileflags = 0;
	if      (S_ISREG(st_mode)) fileflags = FMATCH_F_REG;
	else if (S_ISDIR(st_mode)) fileflags = FMATCH_F_DIR;
	else if (S_ISLNK(st_mode)) fileflags = FMATCH_F_LNK;
	else if (S_ISBLK(st_mode)) fileflags = FMATCH_F_BLK;
	else if (S_ISCHR(st_mode)) fileflags = FMATCH_F_CHR;

	int type = self->flags & FMATCH_MODE_MASK;

	if (self->flags &&
	    (fileflags == type) != !(self->flags & FMATCH_NEGATIVE))
	{
		return 0;
	}

	if (self->glob) {
		if ((self->flags & FMATCH_FIXED_TEXT))
			return strcmp(self->glob, path) == 0;

		int flags = FNM_EXTMATCH;
		if (!(self->flags & FMATCH_WILDCARD_SLASH))
			flags |= FNM_PATHNAME;
		return 0 == fnmatch(self->glob, path, flags);
	}
	if (self->regex)
		return Regex_matches(self->regex, path, 0);
	warn(WARN_GENERIC, "fies: bad FileMatch object\n");
	return false;
}

