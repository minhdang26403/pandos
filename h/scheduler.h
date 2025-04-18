#ifndef SCHEDULER
#define SCHEDULER

/**
 * @file scheduler.h
 * @author Dang Truong
 * @brief The externals declaration file for the Scheduler Module.
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/types.h"

extern cpu_t quantumStartTime;

extern void switchContext(state_t *state);
extern void loadContext(context_t *context);
extern void scheduler();

#endif
