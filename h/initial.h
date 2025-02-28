#ifndef INITIAL
#define INITIAL

/************************** INITIAL.H ******************************
 *
 *  The externals declaration file for the Nucleus Initialization Module.
 *
 *  Written by Dang Truong
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
