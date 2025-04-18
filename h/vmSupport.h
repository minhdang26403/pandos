#ifndef VM_SUPPORT
#define VM_SUPPORT

/**
 * @file vmSupport.h
 * @author Dang Truong
 * @brief The externals declaration file for the Virtual Memory Module.
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/const.h"
#include "../h/types.h"

void initSwapStructs();
int isValidAddr(memaddr addr);
void uTLB_ExceptionHandler();

#endif
