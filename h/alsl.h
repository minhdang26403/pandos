#ifndef ALSL
#define ALSL

/**
 * @file alsl.h
 * @author Dang Truong
 * @brief The externals declaration file for the Active Logical Semaphore List
 * (ALSL)
 * @date 2025-04-23
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/types.h"

void sysPasserenLogicalSem(state_t *excState, support_t *sup);
void sysVerhogenLogicalSem(state_t *excState, support_t *sup);
void initALSL();

#endif
