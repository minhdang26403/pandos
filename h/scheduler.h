#ifndef SCHEDULER
#define SCHEDULER

/************************** SCHEDULER.H ******************************
 *
 *  The externals declaration file for the Scheduler Module.
 *
 *  Written by Dang Truong
 */

/***************************************************************/

#include "../h/types.h"

extern cpu_t quantumStartTime;

extern void switchContext(state_t *state);
extern void loadContext(context_t *context);
extern void scheduler();

#endif
