#ifndef INIT_PROC
#define INIT_PROC

/************************** INITPROC.H ******************************
 *
 *  The externals declaration file for the Support Level Module.
 *
 *  Written by Dang Truong
 */

#include "../h/const.h"
#include "../h/types.h"

extern int masterSemaphore;
extern int supportDeviceSem[NUMDEVICES];

#endif
