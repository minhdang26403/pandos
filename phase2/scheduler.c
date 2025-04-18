/**
 * @file scheduler.c
 * @author Dang Truong, Loc Pham
 * @brief This module implements the scheduler functionality for Phase 2. It is
 * responsible for dispatching processes from the ready queue using a
 * round-robin scheduling algorithm with a time slice of 5 milliseconds.
 * Additionally, it provides critical wrapper functions for context switching
 * using the LDST and LDCXT instructions.
 * @date 2025-04-17
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/scheduler.h"

#include "../h/initial.h"
#include "../h/pcb.h"
#include "umps3/umps/libumps.h"

/* Timestamp (in microseconds) when the current process's time slice began. */
cpu_t quantumStartTime = 0;

/**
 * @brief Load a new processor state using the LDST instruction.
 *
 * LDST replaces the current process context with the one specified by `state`.
 * This is a privileged operation that does not return if successful.
 * Should only be invoked via this wrapper for safety and clarity.
 *
 * @param state Pointer to the processor state to load.
 * @return This function does not return; control is transferred to the new
 * state.
 */
void switchContext(state_t *state) { LDST(state); }

/**
 * @brief Atomically load a new processor context using LDCXT.
 *
 * Loads a full processor context, including stack pointer, status register,
 * and program counter. Used when passing control to support-level exception
 * handlers.
 *
 * @param context Pointer to the context structure to load.
 * @return This function does not return; control is transferred to the new
 * context.
 */
void loadContext(context_t *context) {
  LDCXT(context->c_stackPtr, context->c_status, context->c_pc);
}

/**
 * @brief Round-robin scheduler for selecting and dispatching processes.
 *
 * Removes the next process from the ready queue and:
 * - If no processes exist, halts the system (successful termination).
 * - If all processes are blocked, enables interrupts and disables the PLT, then
 * waits for a device interrupt.
 * - Otherwise, invokes PANIC due to a deadlock or invalid state.
 *
 * If a ready process is found:
 * - Sets it as `currentProc`
 * - Records the current time as `quantumStartTime`
 * - Loads the processor timer with a 5ms time slice
 * - Performs a context switch to the selected process
 *
 * @return This function does not return; control is passed via switchContext or
 * HALT/WAIT/PANIC.
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
