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

#include "../h/alsl.h"
#include "../h/deviceSupportDMA.h"
#include "../h/exceptions.h"
#include "../h/supportAlloc.h"
#include "../h/sysSupport.h"
#include "../h/vmSupport.h"
#include "umps3/umps/libumps.h"

/* Support Level's global variables */
int masterSemaphore;              /* Master semaphore for termination */
int supportDeviceSem[NUMDEVICES]; /* support level device semaphore */

pte_t globalPgTbl[KUSEGSHARE_PAGES];

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
 * @brief Initialize a U-proc's page table with all writable pages.
 *
 * @param sup Pointer to the U-proc's support structure containing its page
 * table.
 * @param asid Address Space Identifier (ASID) for the U-proc.
 */
HIDDEN void initPageTable(support_t *sup, int asid) {
  /* Initialize the first 31 entries (text and data pages) */
  int i;
  for (i = 0; i < STACKPAGE; i++) {
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
 * @brief Copy each U-proc's logical image from its flash device to the global
 * backing store disk (DISK0).
 *
 * For each flash device (0-7):
 *   1. Read block 0 into the device's DMA buffer to extract the U-proc header,
 *      obtaining the .text and .data sizes.
 *   2. Compute the number of 4KB pages containing code+data.
 *   3. For each block up to that count:
 *        - Read the block into the DMA buffer.
 *        - Write the buffer to DISK0 at correct sector.
 *   4. On any error, terminate the current process (SYS9).
 */
HIDDEN void initBackingStore() {
  /* Copy each U-proc's execution image from its flash device to DISK0 */
  int flashNum;
  for (flashNum = 0; flashNum < DEVPERINT; flashNum++) {
    /* Compute physical DMA buffer address for this flash */
    memaddr dmaBuf = FLASH_DMA_BASE + flashNum * PAGESIZE;

    /* Read the first block of each flash device to examine the U-proc's header
     * information */
    if (flashOperation(flashNum, 0, dmaBuf, FLASH_READBLK) < 0) {
      SYSCALL(TERMINATEPROCESS, 0, 0, 0);
    }

    /* Extract the .text and .data file sizes from the header */
    int textFileSize = *(int *)(dmaBuf + TEXT_FILE_SIZE_OFFSET);
    int dataFileSize = *(int *)(dmaBuf + DATA_FILE_SIZE_OFFSET);

    /* Compute the number of pages containing the .text and .data sections */
    int numPages = (textFileSize + dataFileSize) / PAGESIZE;

    /* Only copy the blocks containing the U-proc's .text and .data. The
     * remainder of the U-proc's logical address space is uninitialized and need
     * not be (unnecessarily) copied from the flash device to DISK0 */
    int blockNum;
    for (blockNum = 0; blockNum < numPages; blockNum++) {
      if (flashOperation(flashNum, blockNum, dmaBuf, FLASH_READBLK) < 0) {
        SYSCALL(TERMINATEPROCESS, 0, 0, 0);
      }

      int sectorNum = flashNum * MAXPAGES + blockNum;
      if (diskOperation(BACKING_DISK, sectorNum, dmaBuf, DISK_WRITEBLK) < 0) {
        SYSCALL(TERMINATEPROCESS, 0, 0, 0);
      }
    }
  }
}

HIDDEN void initGlobalPageTable() {
  int i;
  for (i = 0; i < KUSEGSHARE_PAGES; i++) {
    /* ASID is set to zero */
    globalPgTbl[i].pte_entryHI = (VPN_KUSEGSHARE_BASE + i) << VPN_SHIFT;
    globalPgTbl[i].pte_entryLO = PTE_GLOBAL | PTE_DIRTY;
  }
}

/**
 * @brief Support Level instantiator process (Phase 3 entry point).
 *
 * Performs global support-level setup and launches user processes (U-procs):
 * - Initializes the swap pool and support-level device semaphores.
 * - Sets up the support structure free list.
 * - Sets up the backing store.
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

  /* Initialize the backing store (DISK0) by copying U-proc's execution images
   * from flash devices */
  initBackingStore();

  /* Initialize the global page table for the logical address space shared
   * between U-procs  */
  initGlobalPageTable();

  /* Initialize Active Logical Semaphore List to support shared logical address
   * space between U-procs */
  initALSL();

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
