#ifndef FIES_SRC_FIES_H
#define FIES_SRC_FIES_H

//include "../config.h"
#include "../include/fies.h"

#if defined(MAJOR_IN_MKDEV)
# define FIES_MAJOR_MACRO_HEADER <sys/mkdev.h>
#elif defined(MAJOR_IN_SYSMACROS)
# define FIES_MAJOR_MACRO_HEADER <sys/sysmacros.h>
#endif

int fies_id_cmp(const void *pa, const void *pb);

#endif
