
#include "../h/const.h"
#include "../h/types.h"
#include "../h/vmSupport.h"

/*
 * initSwapStructs
 * Initialize the Swap Pool data structures
 */
void initSwapStructs() {
    int i;
    
    /* Initialize Swap Pool table entries */
    for (i = 0; i < SWAPPOOLSIZE; i++) {
        swapPoolTable[i].spte_asid = -1; /* Invalid ASID */
        swapPoolTable[i].spte_vpn = 0;
        swapPoolTable[i].spte_isOccupied = FALSE;
    }
    
    /* Initialize Swap Pool semaphore to 1 (mutex) */
    swapPoolSem = 1;
}

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
        supportStruct->sup_pageTable[i].pte_entryHI = ((0x80000 + i) << 12) | (asid << 6);
        supportStruct->sup_pageTable[i].pte_entryLO = INTIAL_ENTRY_LO;
    }
    
    /* Initialize the stack page (entry 31) */
    supportStruct->sup_pageTable[31].pte_entryHI = (0xBFFFF << 12) | (asid << 6);
    supportStruct->sup_pageTable[31].pte_entryLO = INTIAL_ENTRY_LO;
}