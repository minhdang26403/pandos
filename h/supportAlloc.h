#ifndef SUPPORT_ALLOC_H
#define SUPPORT_ALLOC_H

#include "types.h"

void initSupportFreeList();
support_t *supportAlloc();
void supportDeallocate(support_t *);

#endif
