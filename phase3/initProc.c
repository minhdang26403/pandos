/**
 * @file initProc.c
 * @author Dang Truong, Loc Pham
 * @brief Implements the instantiator process for the Support Level (Phase 3) by
 * initializing global support-level data structures (such as device semaphores,
 * swap pool table, and support structure free list), setting up U-proc
 * processor state and page tables, and launching U-procs via the CREATEPROCESS
 * syscall.
 * @date 2025-04-17
 *
 * @copyright Copyright (c) 2025
 *
 */

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
 * sysSupport.c) needs to invalidate frames of terminated u-procs (optimization)
 */
spte_t swapPoolTable[SWAP_POOL_SIZE]; /* Swap Pool table */
int swapPoolSem;                      /* Swap Pool semaphore: mutex */

/**
 * @brief Initialize the processor state of a U-proc for execution.
 *
 * Sets the program counter (PC and T9), stack pointer (SP), processor status
 * (user mode, interrupts enabled, local timer enabled), and EntryHI (with
 * ASID).
 *
 * @param state Pointer to the state_t structure representing the U-proc's
 * state.
 * @param asid Address Space Identifier (ASID) for the U-proc.
 */
HIDDEN void initUProcState(state_t *state, int asid) {
  state->s_pc = state->s_t9 = UPROC_PC;
  state->s_sp = UPROC_SP;
  state->s_status = STATUS_KUP | STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;
  state->s_entryHI = asid << ASID_SHIFT;
}

/**
 * @brief Initialize a U-proc's page table according to its executable format.
 *
 * Reads the executable header from flash (block 0), extracts the .text memory
 * size, calculates the number of required pages, and configures the PTEs
 * accordingly:
 * - .text pages are clean (not writable)
 * - data and stack pages are writable (dirty bit set)
 *
 * @param sup Pointer to the U-proc's support structure containing its page
 * table.
 * @param asid Address Space Identifier (ASID) for the U-proc.
 */
HIDDEN void initPageTable(support_t *sup, int asid) {
  char headerBuf[PAGESIZE];

  /* Read the header from flash (block 0) into headerBuf */
  if (flashOperation(asid - 1, 0, (memaddr)headerBuf, FLASH_READBLK) < 0) {
    /* If reading the header fails, treat it as a program trap */
    programTrapHandler(sup);
  }

  /* Extract the .text memory size from the header. The header field for .text
   * Memory Size is at offset 0x000C. */
  unsigned int textMemSize = *(unsigned int *)(headerBuf + 0x000C);

  /* Compute the number of pages required for the .text section. Rounding up if
   * necessary. */
  int textPages = (textMemSize + PAGESIZE - 1) / PAGESIZE;

  /**
   * Initialize the first 31 entries (text and data pages). Turn off dirty bit
   * for .text pages and turn on for others.
   */
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

/**
 * @brief Initialize the support structure for a U-proc.
 *
 * Sets the ASID, configures the exception contexts for TLB refill and general
 * exceptions with their respective handlers and stack areas, and initializes
 * the private page table using `initPageTable`.
 *
 * @param sup Pointer to the support structure.
 * @param asid Address Space Identifier (ASID) for the U-proc.
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

/**
 * @brief Support Level instantiator process (Phase 3 entry point).
 *
 * Performs global support-level setup and launches user processes (U-procs):
 * - Initializes the swap pool and support-level device semaphores.
 * - Sets up the support structure free list.
 * - For each U-proc (ASID 1 to MAX_UPROCS):
 *     - Initializes its processor state and support structure.
 *     - Calls CREATEPROCESS to launch the U-proc.
 *     - Terminates if any error occurs during setup.
 * - Waits for all U-procs to terminate by PASSEREN on a master semaphore.
 * - Terminates itself via TERMINATEPROCESS, triggering HALT.
 *
 * Note: Called by the initial test process during Nucleus startup.
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
