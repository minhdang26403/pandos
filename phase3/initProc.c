/************************** INITPROC.C ******************************
 *
 * This module implements `test` and exports the Support Level's global
 * variables (e.g. device semaphores (4.9), and optionally a masterSemaphore)
 *
 * Written by Dang Truong
 */

/***************************************************************/

#include "../h/initProc.h"

#include "../h/exceptions.h"
#include "../h/sysSupport.h"
#include "../h/vmSupport.h"

/* Support Level's global variables */
int masterSemaphore;              /* Master semaphore for termination */
int supportDeviceSem[NUMDEVICES]; /* support level device semaphore */

/* Initialize a U-proc's processor state */
HIDDEN void initUProcState(state_t *state, int asid) {
  state->s_pc = state->s_t9 = UPROC_PC;
  state->s_sp = UPROC_SP;
  state->s_status = STATUS_KUC | STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;
  state->s_entryHI = asid << ASID_SHIFT;
}

/* Helper function to initialize a Page Table for a U-proc */
HIDDEN void initPageTable(support_t *sup, int asid) {
  /* Initialize the first 31 entries (text and data pages) */
  int i;
  for (i = 0; i < MAXPAGES - 1; i++) {
    sup->sup_privatePgTbl[i].pte_entryHI =
        ((VPN_TEXT_BASE + i) << VPN_SHIFT) | (asid << ASID_SHIFT);
    sup->sup_privatePgTbl[i].pte_entryLO = PTE_DIRTY;
  }

  /* Initialize the stack page (entry 31) */
  sup->sup_privatePgTbl[STACKPAGE].pte_entryHI =
      (VPN_STACK << VPN_SHIFT) | (asid << ASID_SHIFT);
  sup->sup_privatePgTbl[STACKPAGE].pte_entryLO = PTE_DIRTY;
}

/* Initialize a Support Structure */
HIDDEN void initSupportStruct(support_t *sup, int asid) {
  /* Set ASID for the process */
  sup->sup_asid = asid;

  /* TLB exception context */
  context_t *excCtxTLB = &sup->sup_exceptContext[PGFAULTEXCEPT];
  excCtxTLB->c_pc = (memaddr)uTLB_ExceptionHandler;
  excCtxTLB->c_status = STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;
  excCtxTLB->c_stackPtr = (memaddr)&sup->sup_stackTLB[499];

  /* General exception context */
  context_t *excCtxGen = &sup->sup_exceptContext[GENERALEXCEPT];
  excCtxGen->c_pc = (memaddr)supportExceptionHandler;
  excCtxGen->c_status = STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;
  excCtxGen->c_stackPtr = (memaddr)&sup->sup_stackGen[499];

  initPageTable(sup, asid);
}

/* The instantiator process. Note that the Nucleus (Level 3/Phase 2) has an
 * external reference to this function */
void test() {
  /* Initialize the Swap Pool table and Swap Pool semaphore */
  initSwapStructs();

  /* Initialize support level device semaphores to 1 since they will be used for
   * mutual exclusion */
  int i;
  for (i = 0; i < NUMDEVICES; i++) {
    supportDeviceSem[i] = 1;
  }

  /* Storage for U-procs' support structures */
  static support_t uProcSupport[MAX_UPROCS];

  /* Launch U-procs */
  int asid;
  for (asid = 1; asid <= MAX_UPROCS; asid++) {
    state_t uProcState;
    initUProcState(&uProcState, asid);
    support_t *sup = &uProcSupport[asid - 1];
    initSupportStruct(sup, asid);
    SYSCALL(CREATEPROCESS, (int)&uProcState, (int)sup, 0);
  }

  /* Wait for all U-procs to terminate */
  masterSemaphore = 0;
  for (int i = 0; i < MAX_UPROCS; i++) {
    SYSCALL(PASSEREN, (int)&masterSemaphore, 0, 0);
  }

  /* All U-procs done—terminate gracefully */
  SYSCALL(TERMINATEPROCESS, 0, 0, 0); /* Triggers HALT */
}
