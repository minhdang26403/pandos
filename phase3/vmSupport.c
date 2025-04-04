/************************** VMSUPPORT.C ******************************
 *
 * Purpose: Implements the Support Level's virtual memory support routines,
 *          including the TLB exception handler (Pager) and the functions for
 *          reading from and writing to flash devices. This module also manages
 *          the Swap Pool data structures used for paging.
 *
 * Written by Dang Truong, Loc Pham
 *
 ***************************************************************/

#include "../h/vmSupport.h"

#include "../h/const.h"
#include "../h/exceptions.h"
#include "../h/initProc.h"
#include "../h/initial.h"
#include "../h/scheduler.h"
#include "../h/sysSupport.h"
#include "../h/types.h"
#include "umps3/umps/libumps.h"

/* Module-wide variables */
HIDDEN memaddr swapPool; /* RAM frames set aside to support virtual memory */

/*
 * Function: initSwapStructs
 * Purpose: Initialize the Swap Pool data structures. This includes setting the starting
 *          address for the Swap Pool, initializing each entry in the Swap Pool table to
 *          indicate that it is unoccupied, and setting the Swap Pool semaphore for mutual
 *          exclusion.
 * Parameters: None.
 */
void initSwapStructs() {
  /* Place the Swap Pool after the end of the operating system code */
  swapPool = SWAP_POOL_BASE;

  int i;
  /* Initialize Swap Pool table entries */
  for (i = 0; i < SWAP_POOL_SIZE; i++) {
    swapPoolTable[i].spte_asid = ASID_UNOCCUPIED; /* Invalid ASID */
    swapPoolTable[i].spte_vpn = 0;
    swapPoolTable[i].spte_pte = NULL;
  }

  /* Initialize Swap Pool semaphore to 1, providing mutual exclusion for the
   * swapPoolTable */
  swapPoolSem = 1;
}

/*
 * Function: readFlashPage
 * Purpose: Reads a 4 KB page from the flash device into RAM. This function maps the U-proc's
 *          ASID to the appropriate flash device, locks the device register, sets the DMA address,
 *          issues the read command, waits for I/O completion, and then unlocks the device.
 * Parameters:
 *    - asid: The ASID of the U-proc whose flash device will be used.
 *    - blockNum: The block number (flash block) to read from.
 *    - dest: The destination address in RAM where the page will be loaded.
 * Returns:
 *    - 0 on success, or -1 if an error occurs during the read operation.
 */
int readFlashPage(int asid, int blockNum, memaddr dest) {
  /* Map ASID to flash device (1-8 -> 0-7) */
  int devNum = asid - 1;
  int devIdx = (FLASHINT - DISKINT) * DEVPERINT + devNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flash = &busRegArea->devreg[devIdx];

  /* 1. Lock device register */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[devNum], 0, 0);

  /* 2. Set DMA address */
  flash->d_data0 = dest;

  /* 3. Set block number and command (atomic with SYS5) */
  unsigned int command = (blockNum << 8) | FLASH_READBLK;
  flash->d_command = command;

  /* 4. Wait for I/O completion */
  int status = SYSCALL(WAITIO, FLASHINT, devNum, 0);
  /* TODO: do we need to mask status with `FLASH_STATUS_MASK` as for terminal
   * devices? */
  if (status != READY) {
    SYSCALL(VERHOGEN, (int)&supportDeviceSem[devNum], 0, 0);
    return -1; /* Error */
  }

  /* 5. Unlock device register */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[devNum], 0, 0);
  return 0;
}

/*
 * Function: writeFlashPage
 * Purpose: Writes a 4 KB page from RAM to the flash device. This function maps the U-proc's
 *          ASID to the correct flash device, locks the device register, sets the DMA address,
 *          issues the write command, waits for I/O completion, and then unlocks the device.
 * Parameters:
 *    - asid: The ASID of the U-proc whose flash device will be used.
 *    - blockNum: The block number (flash block) to write to.
 *    - src: The source address in RAM where the page to be written is located.
 * Returns:
 *    - 0 on success, or -1 if an error occurs during the write operation.
 */
int writeFlashPage(int asid, int blockNum, memaddr src) {
  /* Map ASID to flash device (1-8 -> 0-7) */
  int devNum = asid - 1;
  int devIdx = (FLASHINT - DISKINT) * DEVPERINT + devNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flash = &busRegArea->devreg[devIdx];

  /* 1. Lock device register */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[devNum], 0, 0);

  /* 2. Set DMA address */
  flash->d_data0 = src;

  /* 3. Set block number and command (atomic with SYS5) */
  unsigned int command = (blockNum << 8) | FLASH_WRITEBLK;
  flash->d_command = command;

  /* 4. Wait for I/O completion */
  int status = SYSCALL(WAITIO, FLASHINT, devNum, 0);
  /* TODO: do we need to mask status with `FLASH_STATUS_MASK` as for terminal
   * devices? */
  if (status != READY) {
    SYSCALL(VERHOGEN, (int)&supportDeviceSem[devNum], 0, 0);
    return -1; /* Error */
  }

  /* 5. Unlock device register */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[devNum], 0, 0);
  return 0;
}

/*
 * Function: allocateFrame
 * Purpose: Allocates a frame from the Swap Pool for paging. It first searches for an unoccupied
 *          frame; if none is available, it uses a FIFO (round-robin) algorithm as a fallback.
 * Parameters: None.
 * Returns:
 *    - The index of the allocated frame within the Swap Pool.
 */
HIDDEN int allocateFrame() {
  /* FIFO index as a fallback (not default) page replacement policy. Note that
   * this assignment is called once (the first time this function is called) */
  static int nextFrameIdx = 0;

  /* First search for an unoccupied frame */
  int frameIdx;
  for (frameIdx = 0; frameIdx < SWAP_POOL_SIZE; frameIdx++) {
    if (swapPoolTable[frameIdx].spte_asid == ASID_UNOCCUPIED) {
      break;
    }
  }
  /* If no free frame is found, fall back to FIFO (round-robin) */
  if (frameIdx == SWAP_POOL_SIZE) {
    frameIdx = nextFrameIdx;
    nextFrameIdx = (nextFrameIdx + 1) % SWAP_POOL_SIZE;
  }

  return frameIdx;
}

/*
 * Function: uTLB_ExceptionHandler
 * Purpose: Implements the TLB exception handler (Pager) for the Support Level. This routine:
 *          1. Retrieves the current U-proc's support structure.
 *          2. Determines the cause of the TLB exception.
 *          3. If the exception is a TLB-Modification, it invokes the program trap handler.
 *          4. Locks the Swap Pool and identifies the missing page from the faulting address.
 *          5. Allocates a frame, and if the frame is already occupied, swaps out the old page:
 *             - Updates the old process’s page table and TLB.
 *             - Writes the old page to the backing store.
 *          6. Reads the required page from the backing store into the allocated frame.
 *          7. Updates the Swap Pool table, the U-proc’s page table, and the TLB atomically.
 *          8. Unlocks the Swap Pool and restarts the faulting process.
 * Parameters: None.
 */
void uTLB_ExceptionHandler() {
  /* 1. Get Support Structure via SYS8 */
  support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  /* 2. Determine cause from sup_exceptState[0] */
  state_t *savedExcState = &sup->sup_exceptState[PGFAULTEXCEPT];
  unsigned int excCode = CAUSE_EXCCODE(savedExcState->s_cause);

  /* 3. Check for TLB-Modification (treat as trap) */
  if (excCode == EXC_TLBMOD) {
    programTrapHandler(sup);
  }

  /* 4. Lock Swap Pool */
  SYSCALL(PASSEREN, (int)&swapPoolSem, 0, 0);

  /* 5. Get missing page number (p) from EntryHi */
  unsigned int vpn = (savedExcState->s_entryHI & VPN_MASK) >> VPN_SHIFT;
  int pageIdx = vpn % MAXPAGES;

  /* 6. Pick a frame (i) */
  int frameIdx = allocateFrame();

  /* 7 & 8. Check if frame i is occupied */
  if (swapPoolTable[frameIdx].spte_asid != ASID_UNOCCUPIED) {
    int oldAsid = swapPoolTable[frameIdx].spte_asid;
    unsigned int oldVpn = swapPoolTable[frameIdx].spte_vpn;
    pte_t *oldPte = swapPoolTable[frameIdx].spte_pte;

    /* 8.(a) Update old process's Page Table (V=0) */
    unsigned int status = getSTATUS();
    setSTATUS(status & ~STATUS_IEC); /* Disable interrupts */
    oldPte->pte_entryLO &= ~PTE_VALID;

    /* 8.(b). Update TLB if cached - Atomic with 8.(a) */
    setENTRYHI(oldPte->pte_entryHI);
    TLBP(); /* Probe TLB */
    if (!(getINDEX() & TLB_PRESENT)) {
      /* P=0: Match found */
      setENTRYLO(oldPte->pte_entryLO);
      TLBWI(); /* Update TLB atomically */
    }
    setSTATUS(status); /* Reenable interrupts */

    /*
     * Why update Page Table/TLB before writing to backing store?
     *
     * If we wrote to flash first, then an interrupt (e.g., another Pager) could
     * run and see the old Page Table entry (V=1) still pointing to this frame.
     * It might reuse or overwrite the frame before the write completes, leading
     * to data corruption in the backing store. Updating Page Table (V=0) and
     * TLB first ensures the frame is marked invalid and uncached, preventing
     * access during the write. Order matters for data integrity.
     */

    /* 8.(c). Write to old process's backing store */
    memaddr frameAddr = swapPool + (frameIdx * PAGESIZE);
    int oldPageIdx = oldVpn % MAXPAGES;
    if (writeFlashPage(oldAsid, oldPageIdx, frameAddr) < 0) {
      SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
      programTrapHandler(sup); /* I/O error as trap */
    }
  }

  /* 9. Read current process's page p into frame i */
  memaddr frameAddr = swapPool + (frameIdx * PAGESIZE);
  if (readFlashPage(sup->sup_asid, pageIdx, frameAddr) < 0) {
    SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
    programTrapHandler(sup); /* I/O error as trap */
  }

  /* 10. Update Swap Pool table */
  swapPoolTable[frameIdx].spte_asid = sup->sup_asid;
  swapPoolTable[frameIdx].spte_vpn = vpn;
  swapPoolTable[frameIdx].spte_pte = &sup->sup_privatePgTbl[pageIdx];

  /* 11. Update Page Table (PFN and V=1) */
  pte_t *pte = &sup->sup_privatePgTbl[pageIdx];
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC); /* Disable interrupts */
  pte->pte_entryLO = (frameAddr & PFN_MASK) | PTE_DIRTY | PTE_VALID;

  /* 12. Update TLB (atomic with 11) */
  setENTRYHI(pte->pte_entryHI);
  TLBP();
  setENTRYLO(pte->pte_entryLO);
  if (!(getINDEX() & TLB_PRESENT)) {
    /* P=0: Match found */
    TLBWI();
  } else {
    /* P=1: No match, add new entry */
    TLBWR(); /* Random slot */
  }
  setSTATUS(status); /* Reenable interrupts */

  /*
   * Why read from backing store before updating Page Table/TLB?
   *
   * If we updated the Page Table (V=1) and TLB first, an interrupt could occur
   * before the read completes, allowing the process to access the frame. Since
   * the frame hasn't been loaded from flash yet, it'd access stale or garbage
   * data, causing incorrect execution. Reading first ensures the frame has
   * valid data before it's marked present and cached. Order prevents data
   * races.
   *
   * Why must Page Table and TLB updates be atomic?
   *
   * If an interrupt occurs between updating the Page Table (e.g., V=1) and the
   * TLB, another Pager or the process itself could see an inconsistent state:
   * the Page Table says the page is valid, but the TLB might still have an old
   * entry (V=0) or none at all. This could trigger spurious faults or access
   * wrong frames. Disabling interrupts ensures both updates happen as a single,
   * uninterruptible unit, maintaining consistency.
   */

  /* 13. Unlock Swap Pool */
  SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);

  /* 14. Restart process */
  switchContext(savedExcState);
}
