#ifndef FIES_SRC_CLI_DMTHIN_H
#define FIES_SRC_CLI_DMTHIN_H

#include "../../lib/map.h"
#include "../../include/fies/dmthin.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weverything"
#include <glib.h>
#pragma GCC diagnostic pop

char *GetDMName(dev_t dev);
bool DMMessage(const char *dmname, const char *message);
bool DMThinPoolInfo(const char *dmname,
                    dev_t *metadev,
                    dev_t *datadev,
                    unsigned int *datablocksectors);

typedef struct FiesDMThin FiesDMThin;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
typedef struct {
	char       *name;
	char       *poolname;
	FiesDMThin *dmthin;
	int         fd;
	fies_id     fid;
	size_t      size;
	size_t      blocksize;
	bool        release;
} ThinMeta;
#pragma clang diagnostic pop
// For *raw* access only:
ThinMeta* ThinMeta_new(const char *name,
                       const char *poolname,
                       FiesWriter *writer,
                       bool raw);
void ThinMeta_delete(ThinMeta*);

GHashTable* ThinMetaTable_new(void);
void        ThinMetaTable_delete(GHashTable*);
ThinMeta*   ThinMetaTable_addPool(GHashTable*,
                                  const char *poolname,
                                  FiesWriter *writer);
void ThinMeta_release(ThinMeta*);
bool ThinMeta_loadRoot(ThinMeta*, bool reserve);
ssize_t ThinMeta_map(ThinMeta*,
                     unsigned dev,
                     fies_pos logical_start,
                     FiesFile_Extent *buffer,
                     size_t count);

#endif
