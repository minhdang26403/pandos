#ifndef DELAYDAEMON_H
#define DELAYDAEMON_H

/**
 * @file delayDaemon.h
 * @author Dang Truong
 * @brief The externals declaration file for the Delay Facility Module.
 * @date 2025-04-24
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "../h/types.h"

void initADL();
void sysDelay(state_t *excState,support_t *sup);

#endif
