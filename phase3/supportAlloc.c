/************************** SUPPORTALLOC.C ******************************
 *
 * Purpose: Implements a free-list allocator for support_t structures used in the
 *          Support Level. A free-list is maintained as a stack (array of pointers)
 *          to support_t structures. This module provides routines to allocate a
 *          support structure from the free list, return one to the free list, and
 *          initialize the free list with a statically allocated array.
 *
 * Written by Dang Truong, Loc Pham
 *
 ***********************************************************************/

#include "../h/supportAlloc.h"

/*
 * supportFreeList: Array of pointers representing the free list of support_t structures.
 * supportFreeListTop: Index of the top element of the free list (-1 indicates the list is empty).
 *
 * Note: We choose not to do the Phase 1 way, where we had a global pointer to the head of the 
 * free PCB list. In this case, we would have to add a new next pointer field in support_t
 * struct, which doesn't make sense.
 */
HIDDEN support_t *supportFreeList[MAX_UPROCS];
HIDDEN int supportFreeListTop;


/*
 * Function: supportAlloc
 * Purpose: Allocates and returns a pointer to a support_t structure from the free list.
 *          If the free list is empty, NULL is returned.
 * Parameters: None.
 * Returns:
 *    - Pointer to a support_t structure if available.
 *    - NULL if no support structures are available.
 */
support_t *supportAlloc() {
  if (supportFreeListTop < 0) {
    return NULL;
  }
  return supportFreeList[supportFreeListTop--];
}


/*
 * Function: supportDeallocate
 * Purpose: Returns a support_t structure back to the free list.
 * Parameters:
 *    - sup: Pointer to the support_t structure to be deallocated.
 * Returns: None.
 */
void supportDeallocate(support_t *sup) {
  supportFreeList[++supportFreeListTop] = sup;
}

/*
 * Function: initSupportFreeList
 * Purpose: Initializes the free list for support_t structures by pushing each element
 *          from a statically allocated array into the free list.
 * Parameters: None.
 * Returns: None.
 */
void initSupportFreeList() {
  /* Storage for U-procs' support structures */
  static support_t uProcSupport[MAX_UPROCS];
  supportFreeListTop = -1;

  int i;
  for (i = 0; i < MAX_UPROCS; i++) {
    supportDeallocate(&uProcSupport[i]);
  }
}
