/**
 * @file alsl.c
 * @author Dang Truong
 * @brief Implement the Active Logical Semaphore List (ALSL)
 * @date 2025-04-23
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/alsl.h"

#include "../h/scheduler.h"
#include "../h/sysSupport.h"
#include "umps3/umps/libumps.h"

/* Logical smepahore descriptor node */
typedef struct logicalSemd_t {
  struct logicalSemd_t *ls_next;
  struct logicalSemd_t *ls_prev;
  int *ls_semAddr;
  support_t *ls_supStruct;
} logicalSemd_t;

/* Head of the unused logical semaphore descriptor list, implemented as a
 * NULL-terminated single linearly linked list. */
HIDDEN logicalSemd_t *logicalSemdFree_h;

/* Tail of the active logical semaphore descriptor list, implemented as a
 * double, circularly linked list. */
HIDDEN logicalSemd_t *blockedUprocs;

/* The semaphore that protects both the free list and active list */
HIDDEN int ALSL_Semaphore;

/*====================Local function declarations====================*/
HIDDEN void initLogicalSemd();
HIDDEN logicalSemd_t *allocLogicalSemd();
HIDDEN void freeLogicalSemd(logicalSemd_t *semd);

HIDDEN int emptyLogicalSemdList();
HIDDEN logicalSemd_t *headLogicalSemdList();

HIDDEN logicalSemd_t *searchLogicalSemd(int *semAddr);
HIDDEN void insertLogicalSemd(logicalSemd_t *semd);
HIDDEN void removeLogicalSemd(logicalSemd_t *semd);

/*====================Global function definitions====================*/
/**
 * @brief
 *
 * @param excState The saved exception state.
 * @param sup The support structure of the calling U-proc.
 */
void sysPasserenLogicalSem(state_t *excState, support_t *sup) {
  int *semAddr = (int *)excState->s_a1;

  /* 1. Check whether the semaphore address is in the KUSEGSHARE region */
  if ((memaddr)semAddr < KUSEGSHARE_BASE ||
      (memaddr)semAddr >= KUSEGSHARE_BASE + KUSEGSHARE_PAGES * PAGESIZE) {
    programTrapHandler(sup);
  }

  /* 2. Decrement the semaphore and return control to the U-proc if the
   * semaphore value is non-negative */
  (*semAddr)--;
  if (*semAddr >= 0) {
    switchContext(excState);
  }

  /* 3. Obtain mutual exclusion over the ALSL */
  SYSCALL(PASSEREN, (int)&ALSL_Semaphore, 0, 0);

  /* 4. Allocate a logical semaphore descriptor node from the list */
  logicalSemd_t *logicalSemd = allocLogicalSemd();
  if (logicalSemd == NULL) {
    /* Run out of semaphore descriptors */
    SYSCALL(VERHOGEN, (unsigned int)&ALSL_Semaphore, 0, 0);
    programTrapHandler(sup);
  }
  /* Populate semaphore descriptor node and enqueue it to the ALSL */
  logicalSemd->ls_semAddr = semAddr;
  logicalSemd->ls_supStruct = sup;
  insertLogicalSemd(logicalSemd);

  /* 5. Release mutual exclusion over the ALSL and P on the U-proc's private
   * semaphore atomically */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC); /* Disable interrupts */
  SYSCALL(VERHOGEN, (unsigned int)&ALSL_Semaphore, 0, 0);
  SYSCALL(PASSEREN, (unsigned int)&sup->sup_privateSem, 0, 0);
  setSTATUS(status); /* Reenable interrupts */

  /* 6. Return control to the U-proc */
  switchContext(excState);
}

/**
 * @brief
 *
 * @param excState The saved exception state.
 * @param sup The support structure of the calling U-proc.
 */
void sysVerhogenLogicalSem(state_t *excState, support_t *sup) {
  int *semAddr = (int *)excState->s_a1;

  /* 1. Check whether the semaphore address is in the KUSEGSHARE region */
  if ((memaddr)semAddr < KUSEGSHARE_BASE ||
      (memaddr)semAddr >= KUSEGSHARE_BASE + KUSEGSHARE_PAGES * PAGESIZE) {
    programTrapHandler(sup);
  }

  /* 2. Increment the semaphore and return control to the U-proc if the
   * semaphore value is > 0 */
  (*semAddr)++;
  if (*semAddr > 0) {
    switchContext(excState);
  }

  /* 3. Obtain mutual exclusion over the ALSL */
  SYSCALL(PASSEREN, (int)&ALSL_Semaphore, 0, 0);

  /* 4. Search the ALSL for a matching semaphore address */
  logicalSemd_t *logicalSemd = searchLogicalSemd(semAddr);

  /* 5. If no matching node is found */
  if (logicalSemd == NULL) {
    SYSCALL(VERHOGEN, (int)&ALSL_Semaphore, 0, 0);
    switchContext(excState);
  } else {
    /* 6. Matching node found: Deallocate it and V the private semaphore */
    support_t *blockedSup = logicalSemd->ls_supStruct;
    removeLogicalSemd(logicalSemd);
    freeLogicalSemd(logicalSemd);

    /* Wake up the blocked process */
    SYSCALL(VERHOGEN, (int)&blockedSup->sup_privateSem, 0, 0);

    /* 7. Release mutual exclusion over the ALSL */
    SYSCALL(VERHOGEN, (int)&ALSL_Semaphore, 0, 0);

    /* 8. Return control to the calling U-proc */
    switchContext(excState);
  }
}

void initALSL() {
  /* Allocate storage for logical semaphore descriptor nodes */
  static logicalSemd_t logicalSemds[MAX_UPROCS];

  logicalSemdFree_h = NULL;
  int i;
  for (i = 0; i < MAX_UPROCS; i++) {
    freeLogicalSemd(&logicalSemds[i]);
  }

  /* Initially, no logical semaphore descriptor nodes are in the active list */
  blockedUprocs = NULL;

  /* Mutual exclusion semaphore should be initialized to one */
  ALSL_Semaphore = 1;
}

/*====================Local function definitions====================*/

HIDDEN void initLogicalSemd(logicalSemd_t *semd) {
  semd->ls_next = semd->ls_prev = NULL;
  semd->ls_semAddr = NULL;
  semd->ls_supStruct = NULL;
}

HIDDEN logicalSemd_t *allocLogicalSemd() {
  if (logicalSemdFree_h == NULL) {
    /* The logicalSemdFree list is empty */
    return NULL;
  }

  /* Remove an element from the logicalSemdFree list */
  logicalSemd_t *semd = logicalSemdFree_h;
  logicalSemdFree_h = logicalSemdFree_h->ls_next;
  initLogicalSemd(semd);

  return semd;
}

HIDDEN void freeLogicalSemd(logicalSemd_t *semd) {
  /* Put the logical semaphore descriptor to the head of the free list */
  semd->ls_next = logicalSemdFree_h;
  logicalSemdFree_h = semd;
}

HIDDEN int emptyLogicalSemdList() { return blockedUprocs == NULL; }

HIDDEN logicalSemd_t *headLogicalSemdList() {
  if (emptyLogicalSemdList()) {
    return NULL;
  }

  return blockedUprocs->ls_next;
}

HIDDEN void insertLogicalSemd(logicalSemd_t *semd) {
  if (emptyLogicalSemdList()) {
    semd->ls_next = semd->ls_prev = semd;
    blockedUprocs = semd;
    return;
  }

  /* Connect the semaphore descriptor node to the head */
  logicalSemd_t *head = headLogicalSemdList();
  semd->ls_next = head;
  head->ls_prev = semd;

  /* Connect the semaphore descriptor node to the tail and update the tail
   * pointer */
  blockedUprocs->ls_next = semd;
  semd->ls_prev = blockedUprocs;
  blockedUprocs = semd;
}

HIDDEN void removeLogicalSemd(logicalSemd_t *semd) {
  if (emptyLogicalSemdList()) {
    return;
  }

  logicalSemd_t *prev = semd->ls_prev;
  logicalSemd_t *next = semd->ls_next;

  if (prev == semd && next == semd) {
    /* Remove the only node in the list */
    blockedUprocs = NULL;
  } else {
    /* Remove from a list with at least two nodes */
    prev->ls_next = next;
    next->ls_prev = prev;

    if (semd == blockedUprocs) {
      /* If remove the tail node, update tail pointer */
      blockedUprocs = prev;
    }
  }
}

/**
 * Searches the ALSL for the first (oldest) node matching the semaphore address
 */
HIDDEN logicalSemd_t *searchLogicalSemd(int *semAddr) {
  if (emptyLogicalSemdList()) {
    return NULL;
  }

  logicalSemd_t *cur = headLogicalSemdList();
  logicalSemd_t *found = NULL;

  /* Iterate through the circular list and stop if there's a match */
  do {
    if (cur->ls_semAddr == semAddr) {
      found = cur;
    }
    cur = cur->ls_next;
  } while (found == NULL && cur != headLogicalSemdList());

  return found;
}
