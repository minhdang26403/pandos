#ifndef SYS_SUPPORT
#define SYS_SUPPORT

/************************** SYSSUPPORT.H ******************************
 *
 *  The externals declaration file for the Support Level Module.
 *
 *  Written by Dang Truong
 */

#include "../h/types.h"

void supportExceptionHandler();
void programTrapHandler(support_t *sup);

#endif
