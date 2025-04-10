
#ifndef VM_SUPPORT
#define VM_SUPPORT

/************************** VMSUPPORT.H ******************************
 *
 *  The externals declaration file for the Virtual Memory module.
 *
 *  Written by Dang Truong
 */

#include "../h/const.h"
#include "../h/types.h"

void initSwapStructs();
int isValidAddr(memaddr addr);
void uTLB_ExceptionHandler();

#endif
