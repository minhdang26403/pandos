#include "../h/supportAlloc.h"

/*
 * A free-list implemented as a stack (array of pointers).
 * supportFreeListTop is the index of the top element (-1 means empty).
 *
 * We choose not to do the Phase 1 way, where we had a global pointer to the
 * head of the free PCB list. In this case, we would have to add a new next
 * pointer field in support_t struct, which doesn't make sense.
 */
HIDDEN support_t *supportFreeList[MAX_UPROCS];
HIDDEN int supportFreeListTop;


/*
 * Return a pointer to a Support Structure from the free list.
 * Returns NULL if none are available.
 */
support_t *supportAlloc() {
  if (supportFreeListTop < 0) {
    return NULL;
  }
  return supportFreeList[supportFreeListTop--];
}


/*
 * Return a Support Structure to the free list.
 */
void supportDeallocate(support_t *sup) {
  supportFreeList[++supportFreeListTop] = sup;
}

/*
 * Initialize the free list by pushing each element of uProcSupport
 * into the free list.
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
