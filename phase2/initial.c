#include "../h/asl.h"
#include "../h/scheduler.h"
#include "../h/initial.h"

/* Test function of phase 2 */
extern void test();

/* 
 * The definition of this function is given in p2test.c. This function will be
 * implemented in exceptions.c when the Support Level is implemented.
 */
extern void uTLB_Refillhandler();

/* 1. Define Nucleus's global variables */
int procCnt; /* The number of started, but not yet terminated processes */
int softBlockCnt; /* The number of started, but not terminated processes that
                   * are in the "block" state due to an I/O or time request 
                   */
pcb_PTR readyQueue; /* Tail pointer to a queue of pcbs that are in the "ready" state */
pcb_PTR currentProc; /* Pointer to the pcb that is in the "running" state */
int deviceSem[NUMDEVICES + 1]; /* One additional semaphore to support the Pseudo-clock */


void generalExceptionHandler() {

}

int main() {
  /* 2. Populate the Processor 0 Pass Up Vector */
  passupvector_t *passUpVector = (passupvector_t *)PASSUPVECTOR;
  passUpVector->tlb_refll_handler = (memaddr) uTLB_Refillhandler;
  passUpVector->tlb_refll_stackPtr = (memaddr) STACKTOP;
  passUpVector->execption_handler=  (memaddr) generalExceptionHandler;
  passUpVector->exception_stackPtr = (memaddr) STACKTOP;

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
  LDIT(100000);

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
  p->p_s.s_status = ZERO_MASK | STATUS_IEP | STATUS_TE;
  
  /* Set SP to RAMTOP */
  devregarea_t* busRegArea = (devregarea_t *) RAMBASEADDR;
  p->p_s.s_sp = RAMSTART + busRegArea->ramsize;

  /* Set PC to jump to the test function of p2test.c */
  p->p_s.s_pc = (memaddr) test;
  p->p_s.s_t9 = (memaddr) test; /* This register must get the same value as 
                                  PC whenever PC is assigned a new value */

  /* 7. Call the scheduler */
  scheduler();
}
