/************************** INITPROC.C ******************************
 *
 * This module implements `test` and exports the Support Level's global
 * variables (e.g. device semaphores (4.9), and optionally a masterSemaphore)
 *
 * Written by Dang Truong
 */

/***************************************************************/

#include "../h/initProc.h"

#include "../h/vmSupport.h"

int masterSemaphore;

/* The instantiator process. Note that the Nucleus (Level 3/Phase 2) has an
 * external reference to this functino */
void test() { initSwapStructs(); }

/* Helper function to initialize a Page Table for a U-proc */
void initPageTable(support_t *supportStruct, int asid) {
  int i;

  /* Set ASID for the process */
  supportStruct->sup_asid = asid;

  /* Initialize the first 31 entries (text and data pages) */
  for (i = 0; i < 31; i++) {
    /*
    For a 32-bit EntryHi, the format is:
    Bits 31-12 (highest 20 bits): VPN
    Bits 11-6 (next 6 bits): ASID
    Bits 5-0 (lowest 6 bits): Unused
    */
    supportStruct->sup_pageTable[i].pte_entryHI =
        ZERO_MASK | ((0x80000 + i) << 12) | (asid << 6);
    supportStruct->sup_pageTable[i].pte_entryLO = ZERO_MASK | PTE_DIRTY;
  }

  /* Initialize the stack page (entry 31) */
  supportStruct->sup_pageTable[31].pte_entryHI =
      ZERO_MASK | (0xBFFFF << 12) | (asid << 6);
  supportStruct->sup_pageTable[31].pte_entryLO = ZERO_MASK | PTE_DIRTY;
}
