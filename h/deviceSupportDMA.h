
#ifndef DEVICE_SUPPORT_DMA
#define DEVICE_SUPPORT_DMA

/************************** DEVICESUPPORTDMA.H ******************************
 *
 *  The externals declaration file for the DMA Device Support module.
 *
 *  Written by Dang Truong
 */

#include "../h/types.h"

int diskOperation(unsigned int diskNum, unsigned int sectorNum,
                  memaddr frameAddr, unsigned int op);
int flashOperation(unsigned int flashNum, unsigned int blockNum,
                   memaddr frameAddr, unsigned int op);

void sysDiskWrite(state_t *excState, support_t *sup);
void sysDiskRead(state_t *excState, support_t *sup);
void sysFlashWrite(state_t *excState, support_t *sup);
void sysFlashRead(state_t *excState, support_t *sup);

#endif
