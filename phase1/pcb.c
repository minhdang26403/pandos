/*********************************** pcb.c ************************************
*
* Process Control Block (PCB) Management
* 
* This module manages the allocation, deallocation, and organization of Process
* Control Blocks (PCBs). It supports PCB queue management, tree-based
* parent-child relationships, and general PCB operations.
* 
* The free PCB list is implemented as a singly linked list. The process queue
* is implemented as a doubly-linked list. The process tree is implemented as
* a tree of parent pointing to the first child, with this child and its
* siblings being connected through a doubly-linked list.
* 
* Written by Dang Truong, Loc Pham
*******************************************************************************/

#include "../h/const.h"
#include "../h/pcb.h"
#include "../h/types.h"

/* Global pointer to the head of the free PCB list */
HIDDEN pcb_t *pcbFree_h;


/*
 * Initialize a PCB by setting all fields to default values.
 */
HIDDEN void initPcb(pcb_PTR p) {
  /* TODO: update this function if new fields are added to pcb_t */
  /* process queue fields */
  p->p_next = NULL;
  p->p_prev = NULL;

  /* process tree fields */
  p->p_prnt = NULL;
  p->p_child = NULL;
  p->p_next_sib = NULL;
  p->p_prev_sib = NULL;

  /* process status information */
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

  /* support layer information */
  p->p_supportStruct = NULL;
}

/*
 * Insert the element pointed to by p onto the pcbFree list.
 */
void freePcb (pcb_PTR p) {
  /* put the pcb to the head of the pcbFree list */
  p->p_next = pcbFree_h;
  pcbFree_h = p;
}

/* 
 * Return NULL if the pcbFree list is empty.
 * Otherwise, remove an element from the pcbFree list, provide initial values for
 * ALL of the pcbs fields (i.e. NULL and/or 0) and then return a pointer to the
 * removed element.
 * pcbs get reused, so it is important that no previous value persist in a pcb
 * when it gets reallocated.
*/
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

/*
 * Initialize the pcbFree list to contain all the elements of the static array of
 * MAXPROC pcbs.
 * This method will be called only once during data structure initialization.
 */
void initPcbs () {
  /* allocate storage for pcbs as static storage */
  static pcb_t pcbFree[MAXPROC];

  /* create a singly linked list of pcbs */
  pcbFree_h = NULL;
  int i;
  for (i = 0; i < MAXPROC; i++) {
    freePcb(&pcbFree[i]);
  }
}

/*
 * This method is used to initialize a variable to be tail pointer to a process
 * queue.
 * Return a pointer to the tail of an empty process queue; i.e. NULL.
 */
pcb_PTR mkEmptyProcQ () {
  return NULL;
}

/*
 * Return TRUE if the queue whose tail is pointed to by tp is empty.
 * Return FALSE otherwise.
 */
int emptyProcQ (pcb_PTR tp) {
  return tp == NULL;
}

/*
 * Insert the pcb pointed to by p into the process queue whose tail pointer is
 * pointed to by tp.
 * Note the double indirection through tp to allow for the possible updating of
 * the tail pointer as well.
 */
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

/*
 * Remove the first (i.e. head) element from the process queue whose tail pointer
 * is pointed to by tp.
 * Return NULL if the process queue was initially empty; otherwise return the
 * pointer to the removed element.
 * Update the process queue’s tail pointer if necessary.
 */
pcb_PTR removeProcQ (pcb_PTR *tp) {
  return outProcQ(tp, headProcQ(*tp));
}

/*
 * Remove the pcb pointed to by p from the process queue whose tail pointer is
 * pointed to by tp.
 * Update the process queue's tail pointer if necessary. If the desired entry is
 * not in the indicated queue, return NULL; otherwise, return p.
 * Note that p can point to any element of the process queue.
 */
pcb_PTR outProcQ (pcb_PTR *tp, pcb_PTR p) {
  if (emptyProcQ(*tp)) {
    return NULL;
  }

  /* check if the pcb exists in the queue starting from the head */
  pcb_PTR hp = headProcQ(*tp);
  pcb_PTR cur = hp;
  do {
    if (cur == p) {
      /* we found the pcb */
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

    cur = cur->p_next;
  } while (cur != hp);

  return NULL;
}

/*
 * Return a pointer to the first pcb from the process queue whose tail is pointed
 * to by tp.
 * Do not remove this pcbfrom the process queue. Return NULL if the process queue
 * is empty.
 */
pcb_PTR headProcQ (pcb_PTR tp) {
  if (emptyProcQ(tp)) {
    return NULL;
  }
  return tp->p_next;
}

/*
 * Return TRUE if the pcb pointed to by p has no children. Return FALSE otherwise.
 */
int emptyChild (pcb_PTR p) {
  return p->p_child == NULL;
}

/*
 * Make the pcb pointed to by p a child of the pcb pointed to by prnt.
 */
void insertChild (pcb_PTR prnt, pcb_PTR p) {
  p->p_next_sib = prnt->p_child;
  if (prnt->p_child != NULL) {
    /* p is not the first child of this parent */
    prnt->p_child->p_prev_sib = p;  
  }
  p->p_prev_sib = NULL;
  p->p_prnt = prnt;
  prnt->p_child = p;
}

/*
 * Make the first child of the pcb pointed to by p no longer a child of p.
 * Return NULL if initially there were no children of p.
 * Otherwise, return a pointer to this removed first child pcb.
 */
pcb_PTR removeChild (pcb_PTR p) {
  if (p->p_child == NULL) {
    /* the pcb has no children */
    return NULL;
  }

  return outChild(p->p_child);
}

/*
 * Make the pcb pointed to by p no longer the child of its parent.
 * If the pcb pointed to by p has no parent, return NULL; otherwise, return p.
 * Note that the element pointed to by p need not be the first child of its parent.
 */
pcb_PTR outChild (pcb_PTR p) {
  pcb_PTR prnt = p->p_prnt;
  if (prnt == NULL) {
    /* the pcb has no parent */
    return NULL;
  }

  p->p_prnt = NULL;
  if (prnt->p_child == p) {
    /* p is the first child */
    prnt->p_child = p->p_next_sib;
    if (p->p_next_sib != NULL) {
      /* p has a sibling */
      p->p_next_sib->p_prev_sib = NULL;  
    }
    return p;
  }

  /* get the prev sibling of p */
  pcb_PTR prev_sib = p->p_prev_sib;
  prev_sib->p_next_sib = p->p_next_sib;
  if (p->p_next_sib != NULL) {
    /* p is not the last child */
    p->p_next_sib->p_prev_sib = prev_sib;  
  }

  return p;
}
