/************************** VMSUPPORT.C ******************************
 *
 * This module implements the TLB exception handler (The Pager). Since reading
 * and writing to each U-proc's flash device is limited to supporting paging,
 * this module should also contain the function(s) for reading and writing flash
 * devices.
 *
 * Written by Dang Truong
 */

/***************************************************************/

#include "../h/vmSupport.h"

#include "../h/const.h"
#include "../h/exceptions.h"
#include "../h/initProc.h"
#include "../h/initial.h"
#include "../h/scheduler.h"
#include "../h/sysSupport.h"
#include "../h/types.h"
#include "umps3/umps/libumps.h"

/* Global variables */
memaddr swapPool; /* RAM frames set aside to support virtual memory */
spte_t swapPoolTable[SWAP_POOL_SIZE]; /* Swap Pool table */
int swapPoolSem;                      /* Swap Pool semaphore: mutex */

/* Module-wide variables */
HIDDEN int nextFrameIdx = 0; /* FIFO index for page replacement (4.5.4) */

/*
 * initSwapStructs
 * Initialize the Swap Pool data structures
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

  swapPoolSem = 1;  /* Initialize Swap Pool semaphore to 1 (mutex) */
  nextFrameIdx = 0; /* Start at frame 0 */
}

/* Read 4 KB page from flash to RAM */
static int readFlashPage(int asid, int blockNum, memaddr dest) {
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

/* Write 4 KB page from RAM to flash */
static int writeFlashPage(int asid, int blockNum, memaddr src) {
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

/* TLB exception handler (Pager) */
void pager() {
  /* 1. Get Support Structure via SYS8 */
  support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  /* 2. Determine cause from sup_exceptState[0] */
  state_t *savedExcState = &sup->sup_exceptState[0];
  unsigned int excCode = CAUSE_EXCCODE(savedExcState->s_cause);

  /* 3. Check for TLB-Modification (treat as trap) */
  if (excCode == EXC_TLBMOD) {
    programTrapHandler(sup);
  }

  /* 4. Lock Swap Pool */
  SYSCALL(PASSEREN, (int)&swapPoolSem, 0, 0);

  /* 5. Get missing page number (p) from EntryHi */
  unsigned int vpn = (savedExcState->s_entryHI & VPN_MASK) >> VPN_SHIFT;
  int pageIdx = (vpn == VPN_STACK) ? MAXPAGES - 1 : vpn - VPN_TEXT_BASE;

  /* 6. Pick a frame (i) - FIFO (Section 4.5.4) */
  int frameIdx = nextFrameIdx;
  nextFrameIdx = (nextFrameIdx + 1) % SWAP_POOL_SIZE; /* Round-robin */

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
    int oldPageIdx =
        (oldVpn == VPN_STACK) ? MAXPAGES - 1 : oldVpn - VPN_TEXT_BASE;
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
  setEntryHI(pte->pte_entryHI);
  TLBP();
  if (!(getINDEX() & TLB_PRESENT)) {
    /* P=0: Match found */
    setENTRYLO(pte->pte_entryLO);
    TLBWI();
  } else {
    /* P=1: No match, add new entry */
    setENTRYLO(pte->pte_entryLO);
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
  switchContext(&savedExcState);
}
