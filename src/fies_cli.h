#ifndef FIES_SRC_FIES_CLI_FIES_H
#define FIES_SRC_FIES_CLI_FIES_H

#include "../lib/vector.h"

typedef enum {
	CLONE_NEVER = -1,
	CLONE_AUTO  =  0,
	CLONE_FORCE =  1,
} clone_mode_t;

extern bool                  opt_recurse;
extern size_t                opt_strip_components;
extern bool                  opt_dereference;
extern bool                  opt_hardlinks;
extern bool                  opt_noxdev;
extern long                  opt_uid;
extern long                  opt_gid;
extern VectorOf(RexReplace*) opt_xform;
extern bool                  opt_incremental;
extern clone_mode_t          opt_clone;
extern VectorOf(from_file_t) opt_files_from_list;

extern uint32_t              fies_flags;

typedef struct {
	unsigned long long stream_shared;
	unsigned long long shared;
	unsigned long long unshared;
} clone_info_t;
extern clone_info_t          clone_info;

int create_add(FiesWriter *fies, const char *arg, bool as_ref);
int do_create_add(FiesWriter *fies,
                  int dirfd,
                  const char *basepart,
                  const char *fullpath,
                  dev_t,
                  const char *xformed,
                  bool as_ref);
extern const struct FiesWriter_Funcs create_writer_funcs;
extern struct FiesReader_Funcs list_reader_funcs;
extern struct FiesReader_Funcs extract_reader_funcs;

void create_init(void);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	const char *file;
	bool transforming;
} from_file_t;
#pragma clang diagnostic pop

bool opt_is_path_excluded(const char *path, mode_t, bool skipinc, bool head);
bool opt_is_xattr_excluded(const char *name);
size_t display_time(char *buf, size_t bufsz, const struct fies_time*);

#endif
