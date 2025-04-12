
#ifndef DEVICE_SUPPORT_DMA
#define DEVICE_SUPPORT_DMA

/************************** DEVICESUPPORTDMA.H ******************************
 *
 *  The externals declaration file for the DMA Device Support module.
 *
 *  Written by Dang Truong
 */

#include "../h/types.h"

void sysDiskWrite(state_t *excState, support_t *sup);
void sysDiskRead(state_t *excState, support_t *sup);
void sysFlashWrite(state_t *excState, support_t *sup);
void sysFlashRead(state_t *excState, support_t *sup);

int flashOperation(unsigned int flashNum, unsigned int blockNum,
                   memaddr frameAddr, unsigned int op);

#endif
