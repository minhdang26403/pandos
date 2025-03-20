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
#include "../h/types.h"

/* Swap Pool Entry structure */
typedef struct spte_t {
  int spte_asid;       /* ASID of the process that owns the page */
  int spte_vpn;        /* Virtual page number of the page */
  int spte_isOccupied; /* Flag indicating if the frame is occupied */
} spte_t;

/* Module-wide variables */
static spte_t swapPoolTable[SWAP_POOL_SIZE]; /* Swap Pool table */
static int swapPoolSem;                      /* Swap Pool semaphore */

/*
 * initSwapStructs
 * Initialize the Swap Pool data structures
 */
void initSwapStructs() {
  int i;

  /* Initialize Swap Pool table entries */
  for (i = 0; i < SWAP_POOL_SIZE; i++) {
    swapPoolTable[i].spte_asid = -1; /* Invalid ASID */
    swapPoolTable[i].spte_vpn = 0;
    swapPoolTable[i].spte_isOccupied = FALSE;
  }

  /* Initialize Swap Pool semaphore to 1 (mutex) */
  swapPoolSem = 1;
}
