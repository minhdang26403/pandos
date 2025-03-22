
#ifndef VM_SUPPORT
#define VM_SUPPORT

/************************** VMSUPPORT.H ******************************
 *
 *  The externals declaration file for the Support Level Module.
 *
 *  Written by Dang Truong
 */

extern memaddr swapPool;
extern spte_t swapPoolTable[SWAP_POOL_SIZE];
extern int swapPoolSem;

void initSwapStructs();
void pager();

#endif
