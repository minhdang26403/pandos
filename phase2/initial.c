/**
 * @file initial.c
 * @author Dang Truong, Loc Pham
 * @brief This module implements the Nucleus Initialization routines for
 * Phase 2. It sets up the environment needed formultiprogramming by:
 * 1. Defining relevant global variables for Nucleus.
 * 2. Populating the Processor 0 Pass Up Vector with the appropriate handler
 * addresses and stack pointers.
 * 3. Initializing the PCB free list and the Active Semaphore List (ASL).
 * 4. Setting up Nucleus maintained global variables.
 * 5. Loading the system-wide Interval Timer with a 100-millisecond tick.
 * 6. Instantiating an initial test process with the proper processor state.
 * 7. Calling the scheduler to dispatch processes.
 * @date 2025-04-17
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/initial.h"

#include "../h/asl.h"
#include "../h/exceptions.h"
#include "../h/pcb.h"
#include "../h/scheduler.h"

/* Test function of phase 2 */
extern void test();

/* Init function of phase 3 */
extern void init();

/* 1. Define Nucleus's global variables */
int procCnt;      /* The number of started, but not yet terminated processes */
int softBlockCnt; /* The number of started, but not terminated processes that
                   * are in the "block" state due to an I/O or time request
                   */
pcb_PTR readyQueue;  /* Tail pointer to a queue of pcbs that are in the "ready"
                        state */
pcb_PTR currentProc; /* Pointer to the pcb that is in the "running" state */
int deviceSem[NUMDEVICES +
              1]; /* One additional semaphore to support the Pseudo-clock */

/**
 * @brief Nucleus initialization for Phase 2.
 *
 * Sets up the OS kernel environment to support multiprogramming:
 * - Sets TLB and exception handlers in the Pass Up Vector.
 * - Initializes the PCB free list and Active Semaphore List (ASL).
 * - Resets global variables: process count, soft-block count, ready-process
 * queue, current process pointer, device semaphore.
 * - Loads the interval timer with a 100ms tick.
 * - Creates an initial process with kernel-mode state, stack pointer set to
 * RAMTOP, and entry point set to `init()`.
 * - Calls the scheduler to begin process execution.
 *
 * @return This function does not return; control passes to the scheduler.
 */
void main() {
  /* 2. Populate the Processor 0 Pass Up Vector */
  passupvector_t *passUpVector = (passupvector_t *)PASSUPVECTOR;
  passUpVector->tlb_refll_handler = (memaddr)uTLB_RefillHandler;
  passUpVector->tlb_refll_stackPtr = (memaddr)STACKTOP;
  passUpVector->execption_handler = (memaddr)generalExceptionHandler;
  passUpVector->exception_stackPtr = (memaddr)STACKTOP;

  /* 3. Initialize pcb free list and active semaphore list */
  initPcbs();
  initASL();

  /* 4. Initialize all Nuclueus maintained variables */
  procCnt = 0;
  softBlockCnt = 0;
  readyQueue = mkEmptyProcQ();
  currentProc = NULL;
  int i;
  for (i = 0; i < NUMDEVICES + 1; i++) {
    deviceSem[i] = 0;
  }

  /* 5. Load the system-wide Interval Timer with 100 milliseconds */
  LDIT(SYSTEM_TICK_INTERVAL);

  /* 6. Instantiate a single process */
  pcb_PTR p = allocPcb();
  insertProcQ(&readyQueue, p);
  procCnt++;

  /* Note: When setting up a new processor state, one must set the previous bits
   * (i.e. IEp & KUp) and not the current bits (i.e. IEc & KUc) in the Status
   * register for the desired assignment to take effect after the initial LDST
   * loads the processor state.
   */
  /* Enable interrupts and processor Local Timer and turn kernel-mode on */
  p->p_s.s_status = ZERO_MASK | STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;

  /* Set SP to RAMTOP */
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  p->p_s.s_sp = RAMSTART + busRegArea->ramsize;

  /* Set PC to jump to the init function of initProc.c */
  p->p_s.s_pc = (memaddr)init;
  p->p_s.s_t9 = (memaddr)init; /* This register must get the same value as
                                 PC whenever PC is assigned a new value */

  /* 7. Call the scheduler */
  scheduler();
}
