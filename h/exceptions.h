#ifndef EXCEPTIONS
#define EXCEPTIONS

/**
 * @file exceptions.h
 * @author Dang Truong
 * @brief The externals declaration file for the Exceptions Module.
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/types.h"

extern void copyState(state_t *dest, state_t *src);
extern void generalExceptionHandler();
void sysTerminateProc(state_t *savedExcState);

#endif
