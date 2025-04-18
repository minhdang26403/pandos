#ifndef SUPPORT_ALLOC_H
#define SUPPORT_ALLOC_H

/**
 * @file supportAlloc.h
 * @author Dang Truong
 * @brief The externals declaration file for the Support Structure Allocation
 * Module.
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "types.h"

support_t *supportAlloc();
void supportDeallocate(support_t *sup);
void initSupportFreeList();

#endif
