
#ifndef VM_SUPPORT
#define VM_SUPPORT

/************************** VMSUPPORT.H ******************************
 *
 *  The externals declaration file for the Support Level Module.
 *
 *  Written by Dang Truong
 */

#include "../h/const.h"
#include "../h/types.h"

void initSwapStructs();
int readFlashPage(int asid, int blockNum, memaddr dest);
int writeFlashPage(int asid, int blockNum, memaddr src);
void uTLB_ExceptionHandler();

#endif
