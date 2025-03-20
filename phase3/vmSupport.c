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
#include "../h/scheduler.h"
#include "../h/types.h"
#include "umps3/umps/libumps.h"

/* Module-wide variables */
static memaddr swapPool;

/* Swap Pool Entry structure */
typedef struct spte_t {
  int spte_asid; /* ASID (1-8) of the process that owns the page, -1 if free */
  unsigned int spte_vpn; /* Virtual Page Number */
  pte_t *spte_pte;       /* Pointer to Page Table entry */
} spte_t;

static spte_t swapPoolTable[SWAP_POOL_SIZE]; /* Swap Pool table */
static int swapPoolSem;                      /* Swap Pool semaphore: mutex */

/* Map ASID to flash device (1-8 -> 0-7) */
static int flashDev[UPROCMAX] = {0, 1, 2, 3, 4, 5, 6, 7};

/*
 * initSwapStructs
 * Initialize the Swap Pool data structures
 */
void initSwapStructs() {
  /* Swap Pool: 16 RAM frames set aside to support virtual memory. */
  swapPool = SWAP_POOL_BASE;

  int i;
  /* Initialize Swap Pool table entries */
  for (i = 0; i < SWAP_POOL_SIZE; i++) {
    swapPoolTable[i].spte_asid = ASID_UNOCCUPIED; /* Invalid ASID */
    swapPoolTable[i].spte_vpn = 0;
    swapPoolTable[i].spte_pte = NULL;
  }

  /* Initialize Swap Pool semaphore to 1 (mutex) */
  swapPoolSem = 1;
}

/* Read 4 KB page from flash to RAM (Section 4.5.1 TBD) */
static int readFlashPage(int asid, int pageIdx, memaddr dest) {
  int devNum = flashDev[asid - 1];
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flashReg =
      &busRegArea->devreg[DEVPERINT * (FLASHINT - DISKINT) + devNum];

  flashReg->d_data0 = pageIdx * PAGESIZE;
  flashReg->d_command = READBLK;

  /* Stub: Wait for completion (needs SYS5 later) */
  while ((flashReg->d_status & 0xFF) == BUSY);
  if (flashReg->d_status != READY) return -1; /* Error */
  copyState((state_t *)dest, (state_t *)(flashReg->d_data1));
  return 0;
}

/* Write 4 KB page from RAM to flash (Section 4.5.1 TBD) */
static int writeFlashPage(int asid, int pageIdx, memaddr src) {
  int devNum = flashDev[asid - 1];
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flashReg =
      &busRegArea->devreg[DEVPERINT * (FLASHINT - DISKINT) + devNum];

  flashReg->d_data0 = pageIdx * PAGESIZE;
  flashReg->d_data1 = src;
  flashReg->d_command = WRITEBLK | (1 << 20); /* Write 4 KB */

  /* Stub: Wait for completion (needs SYS5 later) */
  while ((flashReg->d_status & 0xFF) == BUSY);
  if (flashReg->d_status != READY) return -1; /* Error */
  return 0;
}

/* TLB exception handler (Pager) */
void pager() {
  /* 1. Get Support Structure via SYS8 */
  support_t *sup = SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  /* 2. Determine cause from sup_exceptState[0] */
  state_t *savedExcState = &sup->sup_exceptState[0];
  unsigned int cause = CAUSE_EXCCODE(savedExcState->s_cause);

  /* 3. Check for TLB-Modification (treat as trap) */
  if (CAUSE_EXCCODE(cause) == EXC_TLBMOD) {
    /* TODO: Section 4.8 - Pass to Program Trap handler */
    SYSCALL(TERMINATEPROCESS, 0, 0, 0); /* Temporary */
  }

  /* 4. Lock Swap Pool */
  SYSCALL(PASSEREN, (int)&swapPoolSem, 0, 0);

  /* 5. Get missing page number (p) from EntryHi */
  unsigned int vpn = (savedExcState->s_entryHI & VPN_MASK) >> VPN_SHIFT;
  int pageIdx = (vpn == VPN_STACK) ? MAXPAGES - 1 : vpn - VPN_TEXT_BASE;

  /* 6. Pick a frame (i) - Simple first-free for now (Section 4.5.4 TBD) */
  int frameIdx;
  for (frameIdx = 0; frameIdx < SWAP_POOL_SIZE; frameIdx++) {
    if (swapPoolTable[frameIdx].spte_asid == ASID_UNOCCUPIED) break;
  }
  if (frameIdx == SWAP_POOL_SIZE) {
    /* TODO: Section 4.5.4 - Page replacement algorithm */
    PANIC(); /* Temporary */
  }

  /* 7 & 8. Check if frame i is occupied */
  if (swapPoolTable[frameIdx].spte_asid != ASID_UNOCCUPIED) {
    int oldAsid = swapPoolTable[frameIdx].spte_asid;
    unsigned int oldVpn = swapPoolTable[frameIdx].spte_vpn;
    pte_t *oldPte = swapPoolTable[frameIdx].spte_pte;

    /* 8.(a) Update old process's Page Table (V=0) */
    oldPte->pte_entryLO &= ~PTE_VALID;

    /* 8.(b). Update TLB if cached (Section 4.5.3 TBD) */
    setENTRYHI(oldPte->pte_entryHI);
    TLBP();                            /* Probe TLB */
    if (!(getINDEX() & TLB_PRESENT)) { /* Match found (P=0) */
      setENTRYLO(oldPte->pte_entryLO);
      TLBWI(); /* Update TLB atomically */
    }

    /* 8.(c). Write to old process's backing store */
    memaddr frameAddr = swapPool + (frameIdx * PAGESIZE);
    int oldPageIdx = (oldVpn == VPN_STACK) ? MAXPAGES - 1 : oldVpn - VPN_TEXT_BASE;
    if (writeFlashPage(oldAsid, oldPageIdx, frameAddr) < 0) {
      /* TODO: Section 4.8 - Program Trap */
      SYSCALL(TERMINATEPROCESS, 0, 0, 0); /* Temporary */
    }
  }

  /* 9. Read current process's page p into frame i */
  memaddr frameAddr = swapPool + (frameIdx * PAGESIZE);
  if (readFlashPage(sup->sup_asid, pageIdx, frameAddr) < 0) {
    /* TODO: Section 4.8 - Program Trap */
    SYSCALL(TERMINATEPROCESS, 0, 0, 0); /* Temporary */
  }

  /* 10. Update Swap Pool table */
  swapPoolTable[frameIdx].spte_asid = sup->sup_asid;
  swapPoolTable[frameIdx].spte_vpn = vpn;
  swapPoolTable[frameIdx].spte_pte = &sup->sup_pageTable[pageIdx];

  /* 11. Update Page Table (PFN and V=1) */
  pte_t *pte = &sup->sup_pageTable[pageIdx];
  pte->pte_entryLO = ZERO_MASK | (frameAddr & PFN_MASK) | PTE_DIRTY | PTE_VALID;

  /* 12. Update TLB (atomic with 11) */
  setEntryHI(pte->pte_entryHI);
  setEntryLO(pte->pte_entryLO);
  TLBWR(); /* Random slot */

  /* 13. Unlock Swap Pool */
  SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);

  /* 14. Restart process */
  switchContext(&savedExcState);
}
