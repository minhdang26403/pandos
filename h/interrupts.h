#ifndef INTERRUPTS
#define INTERRUPTS

/**
 * @file interrupts.h
 * @author Dang Truong
 * @brief The externals declaration file for Device Interrupt Handler Module.
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/types.h"

extern void interruptHandler(state_t *savedExcState);

#endif
