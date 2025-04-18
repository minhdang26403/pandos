#ifndef SYS_SUPPORT
#define SYS_SUPPORT

/**
 * @file sysSupport.h
 * @author Dang Truong
 * @brief The externals declaration file for the Support-Level Exception
 * Handling Module.
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/types.h"

void supportExceptionHandler();
void programTrapHandler(support_t *sup);

#endif
