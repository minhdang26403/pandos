#ifndef INTERRUPTS
#define INTERRUPTS

/************************** INTERRUPTS.H ******************************
 *
 *  The externals declaration file for the Device Interrupt Handler
 *    Module.
 *
 *  Written by Dang Truong
 */

/***************************************************************/

#include "../h/types.h"

extern void interruptHandler(state_t *savedExcState);

#endif
