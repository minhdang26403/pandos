/************************** INITIAL.C ******************************
 *
 * The Nucleus Initialization Module.
 *
 * Description:
 *   This module implements the Nucleus Initialization routines for Phase 2. It sets up
 *   the environment needed formultiprogramming by:
 *     - Defining relevant global variables for Nucleus (procCnt, softBlockCnt,
 *       readyQueue, currentProc, deviceSem).
 *     - Populating the Processor 0 Pass Up Vector with the appropriate handler
 *       addresses and stack pointers.
 *     - Initializing the PCB free list and the Active Semaphore List (ASL).
 *     - Setting up Nucleus maintained global variables
 *     - Loading the system-wide Interval Timer with a 100-millisecond tick.
 *     - Instantiating an initial test process with the proper processor state.
 *     - Calling the scheduler to dispatch processes.
 *
 * Written by Dang Truong, Loc Pham
 */

/***************************************************************/

#include "../h/initial.h"

#include "../h/asl.h"
#include "../h/exceptions.h"
#include "../h/pcb.h"
#include "../h/scheduler.h"

/* Test function of phase 2 */
extern void test();

/*
 * The definition of this function is given in p2test.c. This function will be
 * implemented in exceptions.c when the Support Level is implemented.
 */
extern void uTLB_RefillHandler();

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

/*
 * Function: main
 * --------------------
 * Purpose:
 *   Performs the Nucleus initialization for Phase 2.
 *   This includes:
 *     - Populating the Processor 0 Pass Up Vector with TLB refill and exception
 *       handler addresses and corresponding stack pointers.
 *     - Initializing the PCB free list and Active Semaphore List.
 *     - Setting the Nucleus global variables (process count, soft-block count,
 *       ready queue, and current process) to their initial states.
 *     - Loading the system-wide Interval Timer with a 100 millisecond interval.
 *     - Creating an initial test process, configuring its processor state with:
 *         * Interrupts enabled.
 *         * Processor Local Timer enabled.
 *         * Kernel mode activated.
 *         * Stack pointer set to the top of RAM.
 *         * Program counter (and t9) set to the test function.
 *     - Invoking the scheduler to start process dispatching.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   This function does not return.
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

  /* Set PC to jump to the test function of p2test.c */
  p->p_s.s_pc = (memaddr)test;
  p->p_s.s_t9 = (memaddr)test; /* This register must get the same value as
                                 PC whenever PC is assigned a new value */

  /* 7. Call the scheduler */
  scheduler();
}
