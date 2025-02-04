/*********************************** asl.c ************************************
*
* Active Semaphore List (ASL) Management
* 
* This module manages the Active Semaphore List (ASL), which keeps track of
* active semaphores and their associated process queues. It provides operations
* to insert, remove, and query blocked processes.
* 
* Invariant: active semaphore should have some processes associated with it
* (process queue is non-empty).
* If the process queue is empty, the semaphore should be deleted from the ASL.
* Note that there are two dummy semaphores which stay on the ASL although they
* have empty queues.
* 
* ASL is implemented as a sorted singly linked list.
* 
* Written by Dang Truong, Loc Pham
*******************************************************************************/


#include "../h/const.h"
#include "../h/types.h"
#include "../h/pcb.h"

/* semaphore descriptor type */
typedef struct semd_t {
  struct semd_t *s_next;    /* next element on the ASL  */
  int           *s_semAdd;  /* pointer to the semaphore */
  pcb_t         *s_procQ;   /* tail pointer to a        */
                            /* process queue            */
} semd_t;

/* Global pointer to the head of the active semaphore list */
HIDDEN semd_t *semd_h;

/* Global pointer to the head of the free semaphore list */
HIDDEN semd_t *semdFree_h;

/*
 * Initialize a semaphore descriptor by setting its fields to default values.
 */
HIDDEN void initSemd(semd_t *semd) {
  semd->s_next = NULL;
  semd->s_semAdd = NULL;
  semd->s_procQ = mkEmptyProcQ();
}

/*
 * Return a semaphore descriptor to the free list.
 */
HIDDEN void freeSemd(semd_t *semd) {
  /* put the semaphore descriptor to the head of the free list */
  semd->s_next = semdFree_h;
  semdFree_h = semd;
}

/*
 * Allocate a semaphore descriptor from the free list.
 * Returns NULL if no descriptors are available.
 */
HIDDEN semd_t* allocSemd() {
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

/*
 * Find the predecessor of the given semaphore address in the ASL.
 */
HIDDEN semd_t* findPrevSemd(int *semAdd) {
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

/*
 * Remove a semaphore descriptor from the ASL if its queue is empty.
 */
HIDDEN void tryFreeSemd(semd_t *semd_prev, semd_t *semd) {
  if (emptyProcQ(semd->s_procQ)) {
    /* remove the semaphore descriptor from the ASL if the queue is empty */
    semd_prev->s_next = semd->s_next;
    freeSemd(semd);
  } 
}

/* 
 * Insert the pcb pointed to by p at the tail of the process queue associated
 * with the semaphore whose physical address is semAdd and set the semaphore
 * address of p to semAdd.
 * If the semaphore is currently not active (i.e. there is no descriptor for it
 * in the ASL), allocate a new descriptor from the semdFree list, insert it in
 * the ASL (at the appropriate position), initialize all of the fields (i.e. set 
 * s_semAdd to semAdd, and s_procq to mkEmptyProcQ()), and proceed as above.
 * If a new semaphore descriptor needs to be allocated and the semdFree list is
 * empty, return TRUE. In all other cases return FALSE.
 */
int insertBlocked (int *semAdd, pcb_PTR p) {
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

/*
 * Search the ASL for a descriptor of this semaphore.
 * If none is found, return NULL; otherwise, remove the first (i.e. head) pcb
 * from the process queue of the found semaphore descriptor, set that pcb's
 * address to NULL, and return a pointer to it.
 * If the process queue for this semaphore becomes empty (emptyProcQ(s_procq)
 * is TRUE), remove the semaphore descriptor from the ASL and return it to the
 * semdFree list.
 */
pcb_PTR removeBlocked (int *semAdd) {
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

/*
 * Remove the pcb pointed to by p from the process queue associated with p's
 * semaphore (p->p_semAdd) on the ASL.
 * If pcb pointed to by p does not appear in the process queue associated with
 * p's semaphore, which is an error condition, return NULL; otherwise, return p.
 * Unlike in removeBlocked do NOT (re)set p's semaphore address (p_semAdd) to NULL.
 */
pcb_PTR outBlocked (pcb_PTR p) {
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

/* 
 * Return a pointer to the pcb that is at the head of the process queue
 * associated with the semaphore semAdd.
 * Return NULL if semAdd is not found on the ASL or if the process queue
 * associated with semAdd is empty.
 */
pcb_PTR headBlocked (int *semAdd) {
  semd_t *semd_prev = findPrevSemd(semAdd);
  semd_t *semd = semd_prev->s_next;

  if (semd->s_semAdd != semAdd) {
    /* semAdd is not found on the ASL */
    return NULL;
  }

  return headProcQ(semd->s_procQ);
}

/*
 * Initialize the semdFree list to contain all the elements of the array static
 * semd_t semdTable[MAXPROC].
 * This method will be only called once during data structure initialization.
 */
void initASL () {
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
