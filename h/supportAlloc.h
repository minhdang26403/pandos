#ifndef SUPPORT_ALLOC_H
#define SUPPORT_ALLOC_H

/************************** SUPPORTALLOC.H ******************************
 *
 *  The externals declaration file for the Support Level Module.
 *
 *  Written by Dang Truong
 */

#include "types.h"

support_t *supportAlloc();
void supportDeallocate(support_t *sup);
void initSupportFreeList();

#endif
