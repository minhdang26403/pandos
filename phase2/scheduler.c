/************************** SCHEDULER.C ******************************
 *
 * The Scheduler Module.
 *
 * Description:
 *   This module implements the scheduler functionality for Phase 2.
 *   It is responsible for dispatching processes from the ready queue using a
 *   round-robin scheduling algorithm with a time slice of 5 milliseconds.
 *   Additionally, it provides critical wrapper functions for context switching
 *   using the LDST and LDCXT instructions.
 *
 * Written by Dang Truong, Loc Pham
 */

/***************************************************************/

#include "../h/scheduler.h"

#include "../h/initial.h"
#include "../h/pcb.h"
#include "umps3/umps/libumps.h"

/* Global variable to track when the current process's quantum started */
cpu_t quantumStartTime = 0;

/**
 * Function: switchContext
 * -----------------------
 * Purpose:
 *   Performs a context switch by loading the specified processor state using
 *   the LDST instruction. LDST is a critical and dangerous instruction,
 *   so every call to context switch should be performed through this function.
 *
 * Parameters:
 *   state - A pointer to a state_t structure representing the processor state
 *           to be loaded for the new context.
 *
 * Returns:
 *   This function does not return if the context switch is successful.
 */
void switchContext(state_t *state) { LDST(state); }

/**
 * Function: loadContext
 * ---------------------
 * Purpose:
 *   Atomically loads a new processor context using the LDCXT instruction.
 *   LDCXT is a critical kernel-mode operation, so this function serves as
 *   a centralized wrapper for LDCXT to switch to a new context.
 *
 * Parameters:
 *   context: A pointer to context_t that contains the following fields:
 *    - c_stackPtr: The new stack pointer value.
 *    - c_status:   The new status register value.
 *    - c_pc:       The new program counter value.
 *
 * Returns:
 *   This function does not return if the context load is successful.
 */
void loadContext(context_t *context) {
  LDCXT(context->c_stackPtr, context->c_status, context->c_pc);
}

/**
 * Function: scheduler
 * -------------------
 * Purpose:
 *   Implements the core scheduling functionality of the Nucleus. This function:
 *     - Removes the next process from the ready queue.
 *     - Handles different scenarios where the ready queue is empty:
 *         * If no processes remain, HALT is invoked.
 *         * If there are processes but they are all blocked,
 *           it enables global interrupts, disables the Processor Local Timer (PLT),
 *           and waits for a device interrupt.
 *         * Otherwise, PANIC is invoked in case of a deadlock or abnormal state.
 *     - Sets the selected process as the current process.
 *     - Starts its quantum by recording the start time and setting a 5-millisecond
 *       timer.
 *     - Performs a context switch to transfer control to the selected process.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   This function does not return, as control is transferred to a process via
 *   a context switch.
 */
void scheduler() {
  pcb_PTR p = removeProcQ(&readyQueue);
  if (p == NULL) {
    /* The Ready Queue is empty */
    if (procCnt == 0) {
      /* Job well done */
      HALT();
    } else if (procCnt > 0 && softBlockCnt > 0) {
      /* Enable global interrupts and disable the PLT */
      unsigned int currentStatus = getSTATUS();
      currentStatus |= STATUS_IEC;
      currentStatus &= ~STATUS_TE;
      setSTATUS(currentStatus);
      /* Waiting for a device interrupt to occur */
      WAIT();
    } else {
      /* Deadlock or abnormal case */
      PANIC();
    }
  }

  /* Set new current process and start its quantum */
  currentProc = p;
  STCK(quantumStartTime);
  setTIMER(QUANTUM); /* Each process gets a time slice of 5ms */
  switchContext(&p->p_s);
}
