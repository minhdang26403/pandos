
#ifndef DEVICE_SUPPORT_DMA
#define DEVICE_SUPPORT_DMA

/************************** DEVICESUPPORTDMA.H ******************************
 *
 *  The externals declaration file for the DMA Device Support module.
 *
 *  Written by Dang Truong
 */

#include "../h/types.h"

void sysDiskPut(state_t *excState, support_t *sup);
void sysDiskGet(state_t *excState, support_t *sup);

#endif
