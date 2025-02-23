#ifndef SCHEDULER
#define SCHEDULER

/************************** SCHEDULER.H ******************************
 *
 *  The externals declaration file for the Scheduler Module.
 *
 *  Written by Dang Truong
 */

/***************************************************************/

extern void switchContext(state_t *);

extern void loadContext(context_t *);

extern void scheduler();

#endif
