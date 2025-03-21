
#ifndef VM_SUPPORT
#define VM_SUPPORT

/************************** VMSUPPORT.H ******************************
 *
 *  The externals declaration file for the Support Level Module.
 *
 *  Written by Dang Truong
 */

extern int swapPoolSem; /* Swap Pool semaphore: mutex */

void initSwapStructs();
void pager();

#endif
