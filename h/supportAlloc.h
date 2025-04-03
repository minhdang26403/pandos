#ifndef SUPPORT_ALLOC_H
#define SUPPORT_ALLOC_H

#include "types.h"

support_t *supportAlloc();
void supportDeallocate(support_t *sup);
void initSupportFreeList();

#endif
