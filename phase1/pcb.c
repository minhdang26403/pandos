#include "../h/const.h"
#include "../h/pcb.h"
#include "../h/types.h"

HIDDEN pcb_t *pcbFree_h;


HIDDEN void initPcb(pcb_PTR p) {
  /* TODO: update this function if new fields are added to pcb_t */
  p->p_next = NULL;
  p->p_prev = NULL;
  p->p_prnt = NULL;
  p->p_child = NULL;
  p->p_sib = NULL;

  p->p_s.s_entryHI = 0;
  p->p_s.s_cause = 0;
  p->p_s.s_status = 0;
  p->p_s.s_pc = 0;
  int i;
  for (i = 0; i < STATEREGNUM; i++) {
    p->p_s.s_reg[i] = 0;
  }

  p->p_time = 0;
  p->p_semAdd = NULL;
  /* TODO: zero-init p->p_supportStruct */
}

void freePcb (pcb_PTR p) {
  p->p_next = pcbFree_h;
  pcbFree_h = p;
}

pcb_PTR allocPcb () {
  if (pcbFree_h == NULL) {
    /* the pcbFree list is empty */
    return NULL;
  }
  
  /* remove an element from the pcbFree list */
  pcb_PTR p = pcbFree_h;
  pcbFree_h = pcbFree_h->p_next;
  initPcb(p);
  
  return p;
}

void initPcbs () {
  /* allocate storage for pcbs as static storage */
  static pcb_t pcbFree[MAXPROC];

  /* create a singly linked list of pcbs */
  int i;
  for (i = 0; i < MAXPROC - 1; i++) {
    initPcb(&pcbFree[i]);
    pcbFree[i].p_next = &pcbFree[i + 1];
  }
  initPcb(&pcbFree[MAXPROC - 1]);

  pcbFree_h = pcbFree;
}

HIDDEN int entryExists(pcb_PTR tp, pcb_PTR p) {
  if (tp == NULL) {
    /* the queue is empty */
    return FALSE;
  }

  /* check if the pcb exists in the queue starting from the head */
  pcb_PTR hp = headProcQ(tp);
  pcb_PTR cur = hp;
  do {
    if (cur == p) {
      return TRUE;
    }
    cur = cur->p_next;
  } while (cur != hp);

  /* the pcb is not in the queue */
  return FALSE;
}

pcb_PTR mkEmptyProcQ () {
  return NULL;
}

int emptyProcQ (pcb_PTR tp) {
  return tp == NULL;
}

void insertProcQ (pcb_PTR *tp, pcb_PTR p) {
  if (emptyProcQ(*tp)) {
    p->p_next = p;
    p->p_prev = p;
    *tp = p;
    return;
  }

  /* connect the pcb to the head */
  pcb_PTR hp = headProcQ(*tp);
  p->p_next = hp;
  hp->p_prev = p;

  /* connect the pcb to the tail and update the tail pointer */
  (*tp)->p_next = p;
  p->p_prev = *tp;
  *tp = p;
}

pcb_PTR removeProcQ (pcb_PTR *tp) {
  return outProcQ(tp, headProcQ(*tp));
}

pcb_PTR outProcQ (pcb_PTR *tp, pcb_PTR p) {
  if (!entryExists(*tp, p)) {
    return NULL;
  }

  if (*tp == p && p->p_next == p) {
    /* the queue has only one element */
    *tp = NULL;
    return p;
  }

  /* remove the pcb from the queue */
  p->p_prev->p_next = p->p_next;
  p->p_next->p_prev = p->p_prev;

  if (*tp == p) {
    /* update the tail pointer if removing the tail */
    *tp = p->p_prev;
  }

  return p;
}

pcb_PTR headProcQ (pcb_PTR tp) {
  if (emptyProcQ(tp)) {
    return NULL;
  }
  return tp->p_next;
}

int emptyChild (pcb_PTR p) {
  return p->p_child == NULL;
}

void insertChild (pcb_PTR prnt, pcb_PTR p) {
  p->p_sib = prnt->p_child;
  p->p_prnt = prnt;
  prnt->p_child = p;
}

pcb_PTR removeChild (pcb_PTR p) {
  if (p->p_child == NULL) {
    /* the pcb has no children */
    return NULL;
  }

  pcb_PTR child = p->p_child;
  p->p_child = child->p_sib;
  return child;
}

pcb_PTR outChild (pcb_PTR p) {
  pcb_PTR prnt = p->p_prnt;
  if (prnt == NULL) {
    /* the pcb has no parent */
    return NULL;
  }

  if (prnt->p_child == p) {
    /* p is the first child */
    prnt->p_child = p->p_sib;
    return p;
  }

  /* find the previous sibling of this child */
  pcb_PTR prev_sib = prnt->p_child;
  while (prev_sib->p_sib != p) {
    prev_sib = prev_sib->p_sib;
  }
  prev_sib->p_sib = p->p_sib;

  return p;
}
