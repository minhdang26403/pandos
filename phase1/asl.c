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

HIDDEN semd_t *semd_h, *semdFree_h;

HIDDEN void initSemd(semd_t *semd) {
  semd->s_next = NULL;
  semd->s_semAdd = NULL;
  semd->s_procQ = mkEmptyProcQ();
}

HIDDEN void freeSemd(semd_t *semd) {
  semd->s_next = semdFree_h;
  semdFree_h = semd;
}

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

HIDDEN void tryFreeSemd(semd_t *semd_prev, semd_t *semd) {
  if (emptyProcQ(semd->s_procQ)) {
    /* remove the semaphore descriptor from the ASL if the queue is empty */
    semd_prev->s_next = semd->s_next;
    freeSemd(semd);
  } 
}

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

pcb_PTR headBlocked (int *semAdd) {
  semd_t *semd_prev = findPrevSemd(semAdd);
  semd_t *semd = semd_prev->s_next;

  if (semd->s_semAdd != semAdd) {
    /* semAdd is not found on the ASL */
    return NULL;
  }

  return headProcQ(semd->s_procQ);
}

void initASL () {
  /* add two dummy semaphores to support ASL traversal */
  static semd_t semdTable[MAXPROC + 2];

  /* init the active semaphore list with two dummy semaphores */
  semdTable[0].s_next = &semdTable[1];
  semdTable[0].s_semAdd = 0;
  semdTable[0].s_procQ = mkEmptyProcQ();
  semdTable[1].s_next = NULL;
  semdTable[1].s_semAdd = (int *)MAXINT;
  semdTable[1].s_procQ = mkEmptyProcQ();
  semd_h = semdTable;

  /* initialize the list of unused semaphores */
  int i;
  for (i = 2; i < MAXPROC + 1; i++) {
    initSemd(&semdTable[i]);
    semdTable[i].s_next = &semdTable[i + 1];
  }
  initSemd(&semdTable[MAXPROC + 1]);
  
  /* the first two semaphore descriptors were used as dummy descriptors */
  semdFree_h = &semdTable[2];
}
