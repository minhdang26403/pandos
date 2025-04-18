#ifndef DEVICE_SUPPORT_DMA
#define DEVICE_SUPPORT_DMA

/**
 * @file deviceSupportDMA.h
 * @author Dang Truong
 * @brief The externals declaration file for the DMA Device Module.
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
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
