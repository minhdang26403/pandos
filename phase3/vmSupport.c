/**
 * @file vmSupport.c
 * @author Dang Truong, Loc Pham
 * @brief Implements the Support Level's virtual memory support routines,
 * including the TLB exception handler (Pager) and the functions for reading
 * from and writing to flash devices. This module also manages the Swap Pool
 * data structures used for paging.
 * @date 2025-04-17
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/vmSupport.h"

#include "../h/const.h"
#include "../h/deviceSupportDMA.h"
#include "../h/exceptions.h"
#include "../h/initProc.h"
#include "../h/initial.h"
#include "../h/scheduler.h"
#include "../h/sysSupport.h"
#include "../h/types.h"
#include "umps3/umps/libumps.h"

/* Module-wide variables */
HIDDEN memaddr swapPool; /* RAM frames set aside to support virtual memory */
spte_t swapPoolTable[SWAP_POOL_SIZE]; /* Swap Pool table */
int swapPoolSem;                      /* Swap Pool semaphore: mutex */

/**
 * @brief Initialize the Swap Pool data structures.
 *
 * - Sets the base address for swap pool frames.
 * - Marks all entries in the swap pool table as unoccupied.
 * - Initializes the swap pool semaphore to 1 (for mutual exclusion).
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

/**
 * @brief Free all swap pool frames owned by the given U-proc.
 *
 * Iterates through the swap pool table and invalidates any frame associated
 * with the given ASID. Ensures mutual exclusion by acquiring and releasing the
 * swap pool semaphore.
 *
 * @param asid The Address Space Identifier (ASID) of the process whose frames
 * are being freed.
 */
void releaseFrames(int asid) {
  SYSCALL(PASSEREN, (int)&swapPoolSem, 0, 0);

  int i;
  for (i = 0; i < SWAP_POOL_SIZE; i++) {
    if (swapPoolTable[i].spte_asid == asid) {
      swapPoolTable[i].spte_asid = ASID_UNOCCUPIED;
      swapPoolTable[i].spte_vpn = 0;
      swapPoolTable[i].spte_pte = NULL;
    }
  }

  SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
}

/**
 * @brief Check if a virtual address lies within the U-proc's user segment
 * (KUSEG).
 *
 * @param addr Virtual address to validate.
 * @return 1 if the address is valid (>= KUSEG); 0 otherwise.
 */
int isValidAddr(memaddr addr) { return addr >= KUSEG; }

/**
 * @brief Translate the virtual page number (vpn) to the page table entry's
 * index. The way this function translates depends on whether the vpn is a
 * shared or private vpn.
 *
 * @param vpn the virtual page number to translate
 * @return the page table entry's index
 */
HIDDEN int vpnToPageIndex(unsigned int vpn) {
  return IS_SHARED_VPN(vpn) ? (vpn - VPN_KUSEGSHARE_BASE) : (vpn % MAXPAGES);
}

/**
 * @brief Select a frame from the swap pool to load a virtual page.
 *
 * First attempts to find an unoccupied frame. If none are free, applies a FIFO
 * (round-robin) replacement policy using a static index.
 *
 * @return Index of the chosen frame within the swap pool.
 */
HIDDEN int chooseFrame() {
  /* FIFO index as a fallback (not default) page replacement policy. Note that
   * this assignment is called once (the first time this function is called) */
  static int nextFrameIdx = 0;

  /* First search for an unoccupied frame */
  int frameIdx = 0;
  int found = FALSE;
  while (frameIdx < SWAP_POOL_SIZE && !found) {
    if (swapPoolTable[frameIdx].spte_asid == ASID_UNOCCUPIED) {
      found = TRUE;
    } else {
      frameIdx++;
    }
  }

  /* If no free frame is found, fall back to FIFO (round-robin) */
  if (!found) {
    frameIdx = nextFrameIdx;
    nextFrameIdx = (nextFrameIdx + 1) % SWAP_POOL_SIZE;
  }

  return frameIdx;
}

/**
 * @brief Handle TLB refill exception (Phase 2 version).
 *
 * Extracts the VPN from the exception state, finds the corresponding PTE in the
 * process's private page table, and writes it into the TLB. If the process
 * lacks a support structure, it is terminated. Execution resumes from the
 * faulting instruction.
 *
 * @return This function does not return; control is transferred via
 * switchContext or termination.
 */
void uTLB_RefillHandler() {
  /* Get saved exception state from BIOS Data Page */
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;

  /* Extract VPN: mask then shift */
  unsigned int entryHI = savedExcState->s_entryHI;
  unsigned int vpn = (entryHI & VPN_MASK) >> VPN_SHIFT;

  /* Get Page Table entry */
  support_t *sup = currentProc->p_supportStruct;
  if (sup == NULL) {
    /* Consistent with Phase 2 behavior (pass up or die) */
    sysTerminateProc(savedExcState);
  }

  /* Select correct page table entry (private or shared) */
  int pageIdx = vpnToPageIndex(vpn);
  pte_t *pte = IS_SHARED_VPN(vpn) ? &globalPgTbl[pageIdx]
                                  : &sup->sup_privatePgTbl[pageIdx];

  /* Write to TLB */
  setENTRYHI(pte->pte_entryHI);
  setENTRYLO(pte->pte_entryLO);
  TLBWR();

  /* Return control to the process to retry the instruction */
  switchContext(savedExcState);
}

/**
 * @brief TLB exception handler (Pager) for the Support Level. The handler
 * ensures TLB and page table updates are atomic and correctly ordered to
 * prevent data races, stale access, or inconsistency across interrupts.
 */
void uTLB_ExceptionHandler() {
  /* 1. Get Support Structure via SYS8 */
  support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  /* 2. Determine cause from sup_exceptState[PGFAULTEXCEPT] */
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
  /* Compute page index within either the private or shared page table */
  int pageIdx = vpnToPageIndex(vpn);

  /* Another U-proc may have already loaded the shared page. If the global page
   * table entry is now valid, there's no need to reload it.
   */
  if (IS_SHARED_VPN(vpn) && (globalPgTbl[pageIdx].pte_entryLO & PTE_VALID)) {
    SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
    switchContext(savedExcState);
  }

  /* 6. Pick a frame (i) */
  int frameIdx = chooseFrame();

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
    int oldPageIdx = vpnToPageIndex(oldVpn);
    int sectorNum = IS_SHARED_VPN(oldVpn)
                        ? KUSEG_BASE_SECTOR + oldPageIdx
                        : (oldAsid - 1) * MAXPAGES + oldPageIdx;

    if (diskOperation(BACKING_DISK, sectorNum, frameAddr, DISK_WRITEBLK) < 0) {
      SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
      programTrapHandler(sup); /* I/O error as trap */
    }
  }

  /* 9. Read current process's page p into frame i */
  memaddr frameAddr = swapPool + (frameIdx * PAGESIZE);
  int sectorNum = IS_SHARED_VPN(vpn) ? KUSEG_BASE_SECTOR + pageIdx
                                     : (sup->sup_asid - 1) * MAXPAGES + pageIdx;

  if (diskOperation(BACKING_DISK, sectorNum, frameAddr, DISK_READBLK) < 0) {
    SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
    programTrapHandler(sup); /* I/O error as trap */
  }

  /* 10. Update Swap Pool table */
  int asid;
  pte_t *pte;
  if (IS_SHARED_VPN(vpn)) {
    asid = 0;
    pte = &globalPgTbl[pageIdx];
  } else {
    asid = sup->sup_asid;
    pte = &sup->sup_privatePgTbl[pageIdx];
  }

  swapPoolTable[frameIdx].spte_asid = asid;
  swapPoolTable[frameIdx].spte_vpn = vpn;
  swapPoolTable[frameIdx].spte_pte = pte;

  /* 11. Update Page Table (PFN and V=1) */
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
