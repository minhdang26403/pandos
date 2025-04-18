/**
 * @file asl.c
 * @author Dang Truong, Loc Pham
 * @brief Active Semaphore List (ASL) Management
 *
 * This module manages the Active Semaphore List (ASL), which keeps track of
 * active semaphores and their associated process queues. It provides operations
 * to insert, remove, and query blocked processes.
 *
 * Invariant: active semaphore should have some processes associated with it
 * (process queue is non-empty). If the process queue is empty, the semaphore
 * should be deleted from the ASL. Note that there are two dummy semaphores
 * which stay on the ASL although they have empty queues.
 *
 * ASL is implemented as a sorted singly linked list.
 *
 * @date 2025-04-17
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/asl.h"

#include "../h/const.h"
#include "../h/pcb.h"
#include "../h/types.h"

/* Semaphore descriptor type */
typedef struct semd_t {
  struct semd_t *s_next; /* next element on the ASL  */
  int *s_semAdd;         /* pointer to the semaphore */
  pcb_t *s_procQ;        /* tail pointer to a process queue */
} semd_t;

/* Global pointer to the head of the active semaphore list */
HIDDEN semd_t *semd_h;

/* Global pointer to the head of the free semaphore list */
HIDDEN semd_t *semdFree_h;

/**
 * @brief Initialize a semaphore descriptor to default values.
 *
 * @param semd Pointer to the semaphore descriptor to initialize.
 */
HIDDEN void initSemd(semd_t *semd) {
  semd->s_next = NULL;
  semd->s_semAdd = NULL;
  semd->s_procQ = mkEmptyProcQ();
}

/**
 * @brief Return a semaphore descriptor to the free list.
 *
 * Inserts the descriptor at the head of the semdFree list.
 *
 * @param semd Pointer to the semaphore descriptor to free.
 */
HIDDEN void freeSemd(semd_t *semd) {
  /* put the semaphore descriptor to the head of the free list */
  semd->s_next = semdFree_h;
  semdFree_h = semd;
}

/**
 * @brief Allocate a semaphore descriptor from the free list.
 *
 * Initializes its fields before returning.
 *
 * @return Pointer to a free descriptor, or NULL if none available.
 */
HIDDEN semd_t *allocSemd() {
  if (semdFree_h == NULL) {
    /* the semdFree list is empty */
    return NULL;
  }

  /* remove an element from the semdFree list */
  semd_t *semd = semdFree_h;
  semdFree_h = semdFree_h->s_next;
  initSemd(semd);

  return semd;
}

/**
 * @brief Find the ASL node whose next node is the first one
 * with s_semAdd >= semAdd.
 *
 * Used for searching and inserting into the sorted ASL.
 *
 * @param semAdd Semaphore address to locate.
 * @return Pointer to the predecessor ASL node.
 */
HIDDEN semd_t *findPrevSemd(int *semAdd) {
  semd_t *semd_prev, *semd_cur;
  semd_prev = semd_h;
  semd_cur = semd_h->s_next;

  /* traverse ASL to find the predecessor of semAdd */
  while (semd_cur->s_semAdd != (int *)MAXINT && semd_cur->s_semAdd < semAdd) {
    semd_prev = semd_cur;
    semd_cur = semd_cur->s_next;
  }

  return semd_prev;
}

/**
 * @brief Remove a semaphore descriptor from the ASL if its queue is empty.
 *
 * Frees the descriptor and unlinks it from the ASL.
 * Used after removing the last process from a semaphore queue.
 *
 * @param semd_prev Predecessor of the semaphore in the ASL.
 * @param semd Pointer to the semaphore descriptor to potentially remove.
 */
HIDDEN void tryFreeSemd(semd_t *semd_prev, semd_t *semd) {
  if (emptyProcQ(semd->s_procQ)) {
    /* remove the semaphore descriptor from the ASL if the queue is empty */
    semd_prev->s_next = semd->s_next;
    freeSemd(semd);
  }
}

/**
 * @brief Block a process on a semaphore.
 *
 * Inserts the PCB at the tail of the queue for the given semaphore.
 * If the semaphore is not active, allocate a new descriptor from
 * semdFree and insert it into the ASL.
 *
 * @param semAdd Address of the semaphore the process is blocked on.
 * @param p Pointer to the PCB to block.
 * @return TRUE if a new descriptor was needed but semdFree was empty;
 *         FALSE otherwise.
 */
int insertBlocked(int *semAdd, pcb_PTR p) {
  semd_t *semd_prev = findPrevSemd(semAdd);
  semd_t *semd = semd_prev->s_next;

  if (semd->s_semAdd != semAdd) {
    /* the semaphore is currently not active */
    semd = allocSemd();
    if (semd == NULL) {
      /* run out of semaphore descriptors to allocate */
      return TRUE;
    }

    /* insert the semaphore descriptor to the ASL and init its fields */
    semd->s_next = semd_prev->s_next;
    semd_prev->s_next = semd;
    semd->s_semAdd = semAdd;
    semd->s_procQ = mkEmptyProcQ();
  }

  insertProcQ(&semd->s_procQ, p);
  p->p_semAdd = semAdd;

  return FALSE;
}

/**
 * @brief Remove the head process from the semaphore's queue.
 *
 * Finds the ASL entry for the given semaphore and removes the
 * first process in its queue. Frees the semaphore if the queue
 * becomes empty.
 *
 * @param semAdd Address of the semaphore.
 * @return Pointer to the removed PCB, or NULL if the semaphore is not found.
 */
pcb_PTR removeBlocked(int *semAdd) {
  semd_t *semd_prev = findPrevSemd(semAdd);
  semd_t *semd = semd_prev->s_next;

  if (semd->s_semAdd != semAdd) {
    /* semAdd is not found on the ASL */
    return NULL;
  }

  pcb_PTR p = removeProcQ(&semd->s_procQ);
  p->p_semAdd = NULL;
  tryFreeSemd(semd_prev, semd);

  return p;
}

/**
 * @brief Remove a specific process from its semaphore's queue.
 *
 * Does not reset p->p_semAdd.
 * Frees the semaphore descriptor if its queue becomes empty.
 *
 * @param p Pointer to the PCB to remove.
 * @return Pointer to p if found and removed, or NULL if p was not in the queue.
 */
pcb_PTR outBlocked(pcb_PTR p) {
  semd_t *semd_prev = findPrevSemd(p->p_semAdd);
  semd_t *semd = semd_prev->s_next;

  if (semd->s_semAdd != p->p_semAdd) {
    /* p's semaphore is not found on the ASL */
    return NULL;
  }

  pcb_PTR out_p = outProcQ(&semd->s_procQ, p);
  tryFreeSemd(semd_prev, semd);

  /* out_p may be NULL if pcb pointed by p does not exist in the queue */
  return out_p;
}

/**
 * @brief Get process at the head of a semaphore's queue without removing it.
 *
 * @param semAdd Address of the semaphore.
 * @return Pointer to the head PCB, or NULL if the semaphore is not in the ASL.
 */
pcb_PTR headBlocked(int *semAdd) {
  semd_t *semd_prev = findPrevSemd(semAdd);
  semd_t *semd = semd_prev->s_next;

  if (semd->s_semAdd != semAdd) {
    /* semAdd is not found on the ASL */
    return NULL;
  }

  return headProcQ(semd->s_procQ);
}

/**
 * @brief Initialize the Active Semaphore List and free list.
 *
 * Sets up two dummy semaphore descriptors to anchor the ASL
 * and initializes the free list with MAXPROC unused descriptors.
 */
void initASL() {
  /* add two dummy semaphores to support ASL traversal */
  static semd_t semdTable[MAXPROC + 2];

  /* init the active semaphore list with two dummy semaphores */
  semdTable[0].s_next = &semdTable[1];
  semdTable[0].s_semAdd = (int *)0;
  semdTable[0].s_procQ = mkEmptyProcQ();
  semdTable[1].s_next = NULL;
  semdTable[1].s_semAdd = (int *)MAXINT;
  semdTable[1].s_procQ = mkEmptyProcQ();
  semd_h = semdTable;

  /* initialize the list of unused semaphores */
  semdFree_h = NULL;
  int i;
  for (i = 2; i < MAXPROC + 2; i++) {
    freeSemd(&semdTable[i]);
  }
}
