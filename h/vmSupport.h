
#ifndef VM_SUPPORT
#define VM_SUPPORT

#include "../h/const.h"
#include "../h/types.h"

/* Global Swap Pool data structures */
extern swapPoolTableEntry swapPoolTable[SWAPPOOLSIZE];  /* Swap Pool table */
extern int swapPoolSem;                                /* Swap Pool semaphore */

#endif

