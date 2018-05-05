#ifndef FIES_SRC_CLI_FILEMATCH_H
#define FIES_SRC_CLI_FILEMATCH_H

#include <sys/stat.h>

#include "fies_regex.h"

#define FMATCH_F_REG          0x0001
#define FMATCH_F_DIR          0x0002
#define FMATCH_F_LNK          0x0004
#define FMATCH_F_BLK          0x0008
#define FMATCH_F_CHR          0x0010
#define FMATCH_MODE_MASK      0x001F
#define FMATCH_NEGATIVE       0x8000
#define FMATCH_FIXED_TEXT     0x4000
#define FMATCH_WILDCARD_SLASH 0x2000

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	int flags;
	Regex *regex;
	const char *glob; // not free()d
} FileMatch;
#pragma clang diagnostic pop

void FileMatch_destroy(FileMatch*);
bool FileMatch_matches(FileMatch*, const char *path, mode_t st_mode);

#endif
