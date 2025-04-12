/************************** INITPROC.C ******************************
 *
 * Purpose: Implements the instantiator process for the Support Level (Phase 3)
 *          by initializing global support-level data structures (such as device semaphores,
 *          swap pool table, and support structure free list), setting up U-proc processor
 *          state and page tables, and launching U-procs via the CREATEPROCESS syscall.
 *
 * Written by Dang Truong, Loc Pham
 *
 ***************************************************************/

#include "../h/initProc.h"

#include "../h/deviceSupportDMA.h"
#include "../h/exceptions.h"
#include "../h/supportAlloc.h"
#include "../h/sysSupport.h"
#include "../h/vmSupport.h"
#include "umps3/umps/libumps.h"

/* Support Level's global variables */
int masterSemaphore;              /* Master semaphore for termination */
int supportDeviceSem[NUMDEVICES]; /* support level device semaphore */

/* These variables need to be declared globally since TERMINATE syscall (in
 * sysSupport.c) needs to invalidate frames of terminated u-procs (optimization) */
spte_t swapPoolTable[SWAP_POOL_SIZE]; /* Swap Pool table */
int swapPoolSem;                      /* Swap Pool semaphore: mutex */

/*
 * Function: initUProcState
 * Purpose: Initialize a U-proc's processor state so that it is ready for execution.
 *          This function sets the program counter (PC), the stack pointer (SP),
 *          the processor status (to user mode with interrupts enabled), and the
 *          EntryHi field with the ASID.
 * Parameters:
 *    - state: Pointer to the state_t structure representing the U-proc's processor state.
 *    - asid:  The Address Space Identifier for the U-proc.
 */
HIDDEN void initUProcState(state_t *state, int asid) {
  state->s_pc = state->s_t9 = UPROC_PC;
  state->s_sp = UPROC_SP;
  state->s_status = STATUS_KUP | STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;
  state->s_entryHI = asid << ASID_SHIFT;
}

/*
 * Function: initPageTable
 * Purpose: Initialize the U-proc's page table by reading its executable header from flash,
 *          determining the .text memory size, and setting up page table entries for the text,
 *          data, and stack pages according to the Pandos specifications.
 * Parameters:
 *    - sup: Pointer to the support_t structure which contains the U-proc's page table.
 *    - asid: The Address Space Identifier for the U-proc.
 */
HIDDEN void initPageTable(support_t *sup, int asid) {
  char headerBuf[PAGESIZE];

  /* Read the header from flash (block 0) into headerBuf */
  if (flashOperation(asid - 1, 0, (memaddr)headerBuf, FLASH_READBLK) == ERR) {
    /* If reading the header fails, treat it as a program trap */
    programTrapHandler(sup);
  }

  /* Extract the .text memory size from the header. The header field for .text
   * Memory Size is at offset 0x000C. */
  unsigned int textMemSize = *(unsigned int *)(headerBuf + 0x000C);

  /* Compute the number of pages required for the .text section. Rounding up if
   * necessary */
  int textPages = (textMemSize + PAGESIZE - 1) / PAGESIZE;

  /* Initialize the first 31 entries (text and data pages) */
  /* Turn off dirty bit for .text pages and turn on for others */
  int i;
  for (i = 0; i < textPages; i++) {
    sup->sup_privatePgTbl[i].pte_entryHI =
        ((VPN_TEXT_BASE + i) << VPN_SHIFT) | (asid << ASID_SHIFT);
    sup->sup_privatePgTbl[i].pte_entryLO = ZERO_MASK;
  }

  for (; i < STACKPAGE; i++) {
    sup->sup_privatePgTbl[i].pte_entryHI =
        ((VPN_TEXT_BASE + i) << VPN_SHIFT) | (asid << ASID_SHIFT);
    sup->sup_privatePgTbl[i].pte_entryLO = PTE_DIRTY;
  }

  /* Initialize the stack page (entry 31) */
  sup->sup_privatePgTbl[STACKPAGE].pte_entryHI =
      (VPN_STACK << VPN_SHIFT) | (asid << ASID_SHIFT);
  sup->sup_privatePgTbl[STACKPAGE].pte_entryLO = PTE_DIRTY;
}

/*
 * Function: initSupportStruct
 * Purpose: Initialize the support structure for a U-proc by setting its ASID, configuring the
 *          exception contexts for TLB and general exceptions (using designated stack areas), and
 *          initializing its page table.
 * Parameters:
 *    - sup: Pointer to the support_t structure for the U-proc.
 *    - asid: The Address Space Identifier for the U-proc.
 */
HIDDEN void initSupportStruct(support_t *sup, int asid) {
  /* Set ASID for the process */
  sup->sup_asid = asid;

  /* Determine RAMTOP */
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  memaddr RAMTOP = RAMSTART + busRegArea->ramsize;

  /* Avoiding the last frame: Reserve the last frame below RAMTOP for the test
   * process */
  memaddr SUPPORT_STACK_BASE = RAMTOP - (asid * PAGESIZE * 2);

  /* TLB exception context */
  context_t *excCtxTLB = &sup->sup_exceptContext[PGFAULTEXCEPT];
  excCtxTLB->c_pc = (memaddr)uTLB_ExceptionHandler;
  excCtxTLB->c_status = STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;
  excCtxTLB->c_stackPtr = SUPPORT_STACK_BASE;

  /* General exception context */
  context_t *excCtxGen = &sup->sup_exceptContext[GENERALEXCEPT];
  excCtxGen->c_pc = (memaddr)supportExceptionHandler;
  excCtxGen->c_status = STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;
  excCtxGen->c_stackPtr = SUPPORT_STACK_BASE - PAGESIZE;

  initPageTable(sup, asid);
}

/*
 * Function: init
 * Purpose: Acts as the instantiator process for the Support Level. This function
 *          initializes global data structures (Swap Pool table, device semaphores, and support
 *          structure free list), creates and launches U-procs by initializing their processor state
 *          and support structures, and waits for all U-procs to terminate before gracefully
 *          terminating the process (triggering HALT). Note that the Nucleus (Level 3/Phase 2) has an
 *          external reference to this function.
 * Parameters: None.
 */
void init() {
  int i;

  /* Initialize the Swap Pool table and Swap Pool semaphore */
  initSwapStructs();

  /* Initialize support level device semaphores to 1 since they will be used for
   * mutual exclusion */
  for (i = 0; i < NUMDEVICES; i++) {
    supportDeviceSem[i] = 1;
  }

  /* Initialize the free list of Support Structures */
  initSupportFreeList();

  /* Launch U-procs */
  int asid;
  for (asid = 1; asid <= MAX_UPROCS; asid++) {
    state_t uProcState;
    initUProcState(&uProcState, asid);
    support_t *sup = supportAlloc();
    if (sup == NULL) {
      /* Error: no support structure available */
      SYSCALL(TERMINATEPROCESS, 0, 0, 0);
    }
    initSupportStruct(sup, asid);
    int status = SYSCALL(CREATEPROCESS, (int)&uProcState, (int)sup, 0);
    if (status != OK) {
      /* Error creating u-procs, terminate the current process */
      SYSCALL(TERMINATEPROCESS, 0, 0, 0);
    }
  }

  /* Wait for all U-procs to terminate */
  masterSemaphore = 0;
  for (i = 0; i < MAX_UPROCS; i++) {
    SYSCALL(PASSEREN, (int)&masterSemaphore, 0, 0);
  }

  /* All U-procs done—terminate gracefully */
  SYSCALL(TERMINATEPROCESS, 0, 0, 0); /* Triggers HALT */
}
