#ifndef SYS_SUPPORT
#define SYS_SUPPORT

/************************** SYSSUPPORT.H ******************************
 *
 *  The externals declaration file for the support-level Exception Handling
 * module.
 *
 *  Written by Dang Truong
 */

#include "../h/types.h"

void supportExceptionHandler();
void programTrapHandler(support_t *sup);

#endif
