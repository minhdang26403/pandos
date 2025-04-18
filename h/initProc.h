#ifndef INIT_PROC
#define INIT_PROC

/**
 * @file initProc.h
 * @author Dang Truong
 * @brief The externals declaration file for the Process Initialization Module.
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/const.h"
#include "../h/types.h"

extern int masterSemaphore;
extern int supportDeviceSem[NUMDEVICES];

extern spte_t swapPoolTable[SWAP_POOL_SIZE];
extern int swapPoolSem;

#endif
