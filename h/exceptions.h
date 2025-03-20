#ifndef EXCEPTIONS
#define EXCEPTIONS

/************************** EXCEPTIONS.H ******************************
 *
 *  The externals declaration file for the Exceptions Module.
 *
 *  Written by Dang Truong
 */

/***************************************************************/

#include "../h/types.h"

extern void copyState(state_t *dest, state_t *src);
extern void generalExceptionHandler();
extern void uTLB_RefillHandler();

#endif
