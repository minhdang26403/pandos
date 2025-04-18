#ifndef INITIAL
#define INITIAL

/**
 * @file initial.h
 * @author Dang Truong
 * @brief The externals declaration file for the Nucleus Initialization Module.
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/const.h"
#include "../h/types.h"

extern int procCnt;
extern int softBlockCnt;
extern pcb_PTR readyQueue;
extern pcb_PTR currentProc;
extern int deviceSem[NUMDEVICES + 1];

/***************************************************************/

#endif
