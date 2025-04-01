
#ifndef VM_SUPPORT
#define VM_SUPPORT

/************************** VMSUPPORT.H ******************************
 *
 *  The externals declaration file for the Support Level Module.
 *
 *  Written by Dang Truong
 */

#include "../h/types.h"
#include "../h/const.h"

extern memaddr swapPool;
extern spte_t swapPoolTable[SWAP_POOL_SIZE];
extern int swapPoolSem;

void initSwapStructs();
void uTLB_ExceptionHandler();
int readFlashPage(int asid, int blockNum, memaddr dest);

#endif
