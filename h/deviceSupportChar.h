#ifndef DEVICE_SUPPORT_CHAR
#define DEVICE_SUPPORT_CHAR

/**
 * @file deviceSupportChar.h
 * @author Dang Truong
 * @brief The externals declaration file for the Character Device Module.
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/types.h"

void sysWriteToPrinter(state_t *excState, support_t *sup);
void sysWriteToTerminal(state_t *excState, support_t *sup);
void sysReadFromTerminal(state_t *excState, support_t *sup);

#endif
