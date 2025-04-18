/**
 * @file pcb.c
 * @author Dang Truong, Loc Pham
 * @brief Process Control Block (PCB) Management
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
 * @date 2025-04-18
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/pcb.h"

#include "../h/const.h"
#include "../h/types.h"

/* Global pointer to the head of the free PCB list */
HIDDEN pcb_t *pcbFree_h;

/**
 * @brief Reset all fields in a PCB to default values (zero or NULL).
 *
 * This ensures no residual values persist when a PCB is reused.
 *
 * @param p Pointer to the PCB to initialize.
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

/**
 * @brief Return a PCB to the free list.
 *
 * Inserts the PCB at the head of the pcbFree list.
 *
 * @param p Pointer to the PCB to free.
 */
void freePcb(pcb_PTR p) {
  /* put the pcb to the head of the pcbFree list */
  p->p_next = pcbFree_h;
  pcbFree_h = p;
}

/**
 * @brief Allocate a PCB from the free list.
 *
 * Initializes all fields before returning the PCB.
 * Returns NULL if no free PCBs are available.
 *
 * @return Pointer to the allocated PCB or NULL.
 */
pcb_PTR allocPcb() {
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

/**
 * @brief Populate the free list with all PCBs.
 *
 * Called once at system startup. Initializes MAXPROC static PCBs.
 */
void initPcbs() {
  /* allocate storage for pcbs as static storage */
  static pcb_t pcbFree[MAXPROC];

  /* create a singly linked list of pcbs */
  pcbFree_h = NULL;
  int i;
  for (i = 0; i < MAXPROC; i++) {
    freePcb(&pcbFree[i]);
  }
}

/**
 * @brief Create an empty process queue.
 *
 * @return NULL to represent an empty queue.
 */
pcb_PTR mkEmptyProcQ() { return NULL; }

/**
 * @brief Check if a process queue is empty.
 *
 * @param tp Tail pointer of the queue.
 * @return TRUE if the queue is empty, FALSE otherwise.
 */
int emptyProcQ(pcb_PTR tp) { return tp == NULL; }

/**
 * @brief Insert a PCB into a process queue.
 *
 * Inserts at the tail. Updates the tail pointer if needed.
 *
 * @param tp Pointer to the queue's tail pointer.
 * @param p Pointer to the PCB to insert.
 */
void insertProcQ(pcb_PTR *tp, pcb_PTR p) {
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

/**
 * @brief Remove the head of a process queue.
 *
 * Updates the tail pointer if the queue becomes empty.
 *
 * @param tp Pointer to the queue's tail pointer.
 * @return Pointer to the removed PCB, or NULL if the queue is empty.
 */
pcb_PTR removeProcQ(pcb_PTR *tp) { return outProcQ(tp, headProcQ(*tp)); }

/**
 * @brief Remove a specific PCB from a process queue.
 *
 * Safe even if the PCB is not at the head or tail.
 * Updates the tail pointer if the removed PCB was the tail.
 *
 * @param tp Pointer to the queue's tail pointer.
 * @param p Pointer to the PCB to remove.
 * @return Pointer to p if found and removed, NULL otherwise.
 */
pcb_PTR outProcQ(pcb_PTR *tp, pcb_PTR p) {
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

/**
 * @brief Return the head of a process queue without removing it.
 *
 * @param tp Tail pointer of the queue.
 * @return Pointer to the head PCB, or NULL if the queue is empty.
 */
pcb_PTR headProcQ(pcb_PTR tp) {
  if (emptyProcQ(tp)) {
    return NULL;
  }
  return tp->p_next;
}

/**
 * @brief Check if a PCB has any children.
 *
 * @param p Pointer to the PCB to check.
 * @return TRUE if the PCB has no children, FALSE otherwise.
 */
int emptyChild(pcb_PTR p) { return p->p_child == NULL; }

/**
 * @brief Make a PCB a child of a given parent.
 *
 * Adds the PCB to the front of the parent's child list.
 * Updates sibling and parent pointers.
 *
 * @param prnt Pointer to the parent PCB.
 * @param p Pointer to the child PCB.
 */
void insertChild(pcb_PTR prnt, pcb_PTR p) {
  p->p_next_sib = prnt->p_child;
  if (prnt->p_child != NULL) {
    /* p is not the first child of this parent */
    prnt->p_child->p_prev_sib = p;
  }
  p->p_prev_sib = NULL;
  p->p_prnt = prnt;
  prnt->p_child = p;
}

/**
 * @brief Remove the first child of a PCB.
 *
 * Updates the parent's child pointer.
 *
 * @param p Pointer to the parent PCB.
 * @return Pointer to the removed child, or NULL if no children exist.
 */
pcb_PTR removeChild(pcb_PTR p) {
  if (p->p_child == NULL) {
    /* the pcb has no children */
    return NULL;
  }

  return outChild(p->p_child);
}

/**
 * @brief Remove a PCB from its parent's child list.
 *
 * Safe to call even if the PCB is not the first child.
 * Updates sibling and parent pointers accordingly.
 *
 * @param p Pointer to the PCB to detach.
 * @return Pointer to p if it had a parent, NULL otherwise.
 */
pcb_PTR outChild(pcb_PTR p) {
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
