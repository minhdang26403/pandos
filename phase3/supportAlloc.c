/**
 * @file supportAlloc.c
 * @author Dang Truong, Loc Pham
 * @brief Implements a free-list allocator for support_t structures used in the
 * Support Level. A free-list is maintained as a stack (array of pointers) to
 * support_t structures. This module provides routines to allocate a support
 * structure from the free list, return one to the free list, and initialize the
 * free list with a statically allocated array.
 * @date 2025-04-17
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/supportAlloc.h"

/* Stack of available support_t structures (used as a free list). */
HIDDEN support_t *supportFreeList[MAX_UPROCS];

/* Index of the top of the supportFreeList stack. -1 indicates empty. */
HIDDEN int supportFreeListTop;

/**
 * @brief Allocate a support_t structure from the free list.
 *
 * Implements a stack-based allocator. Returns the top support structure
 * from the free list and updates the stack index.
 *
 * @return Pointer to a support_t structure if available; NULL if the free list
 * is empty.
 */
support_t *supportAlloc() {
  if (supportFreeListTop < 0) {
    return NULL;
  }
  return supportFreeList[supportFreeListTop--];
}

/**
 * @brief Return a support_t structure to the free list.
 *
 * Pushes the given support structure back onto the stack-based free list.
 *
 * @param sup Pointer to the support_t structure to deallocate.
 */
void supportDeallocate(support_t *sup) {
  supportFreeList[++supportFreeListTop] = sup;
}

/**
 * @brief Initialize the support_t structure free list.
 *
 * Prepares a statically allocated array of support_t structures and populates
 * the stack-based free list with them. Called once at system startup by the
 * Support Level.
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
