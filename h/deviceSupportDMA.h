
#ifndef DEVICE_SUPPORT_DMA
#define DEVICE_SUPPORT_DMA

/************************** DEVICESUPPORTDMA.H ******************************
 *
 *  The externals declaration file for the DMA Device Support module.
 *
 *  Written by Dang Truong
 */

#include "../h/types.h"

int writeFlashPage(int flashNum, int blockNum, memaddr src);
int readFlashPage(int flashNum, int blockNum, memaddr dest);

void sysDiskWrite(state_t *excState, support_t *sup);
void sysDiskRead(state_t *excState, support_t *sup);
void sysFlashPut(state_t *excState, support_t *sup);
void sysFlashGet(state_t *excState, support_t *sup);

#endif
