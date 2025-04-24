#include "../h/delayDaemon.h"

#include "../h/const.h"
#include "../h/types.h"
#include "umps3/umps/libumps.h"

/* Delay event descriptor type */
typedef struct delayd_t {
  struct delayd_t *d_next; /* next delay descriptor node on the ADL */
  cpu_t d_wakeTime;        /* time of day (microseconds) to wake up */
  support_t *d_supStruct;  /* the support structure of the sleeping U-proc */
} delayd_t;

/* Head of the sleeping U-proc list, implemented as a sorted, NULL-terminated,
 * single, linearly linked list of delay event descriptor nodes */
HIDDEN delayd_t *delayd_h;

/* Head of the free delay descriptor list, implemented as a NULL-terminated,
 * single, linearly linked list */
HIDDEN delayd_t *delaydFree_h;

/* Mutual exclusion semaphore for accessing the ADL */
HIDDEN int adlMutex;

/**
 * @brief Initialize a delay descriptor to default values.
 *
 * @param delayd Pointer to the delay descriptor to initialize.
 */
HIDDEN void initDelayd(delayd_t *delayd) {
  delayd->d_next = NULL;
  delayd->d_wakeTime = 0;
  delayd->d_supStruct = NULL;
}

/**
 * @brief Return a delay descriptor back to the free list.
 *
 * @param delayd Pointer to the delay descriptor to free.
 */
HIDDEN void freeDelayd(delayd_t *delayd) {
  /* Add the descriptor to the head of the free list */
  delayd->d_next = delaydFree_h;
  delaydFree_h = delayd;
}

/**
 * @brief Allocate a delay descriptor from the free list.
 */
HIDDEN delayd_t *allocDelayd() {
  if (delaydFree_h == NULL) {
    /* The delaydFree list is empty */
    return NULL;
  }

  /* Remove an element from the delaydFree list */
  delayd_t *delayd = delaydFree_h;
  delaydFree_h = delaydFree_h->d_next;
  initDelayd(delayd);

  return delayd;
}

/**
 * @brief Insert a delay descriptor node into the ADL, sorted by wake time. Find
 * the correct position based on d_wakeTime and inserts the node.
 *
 * @param delayd Pointer to the delay descriptor to insert.
 */
HIDDEN void insertDelayd(delayd_t *delayd) {
  delayd_t *prev = delayd_h;
  delayd_t *curr = delayd_h->d_next;

  /* Traverse the ADL to find the correct insertion point */
  while (curr->d_wakeTime < delayd->d_wakeTime) {
    prev = curr;
    curr = curr->d_next;
  }

  /* Insert delayd between prev and curr */
  delayd->d_next = curr;
  prev->d_next = delayd;
}

/**
 * @brief Remove and return the delay descriptor node at the head of the ADL
 * (the one after the dummy head). Does not free the node.
 *
 * @return The pointer to the first delay descriptor node.
 */
HIDDEN delayd_t *removeDelaydHead() {
  /* List is empty (only head and tail dummy nodes) */
  if (delayd_h->d_next->d_wakeTime == MAXINT) {
    return NULL;
  }

  delayd_t *head = delayd_h->d_next;
  delayd_h->d_next = head->d_next;

  return head;
}

HIDDEN void delayDaemon() {
  cpu_t currentTime;

  while (TRUE) {
    /* 1. Wait for the next pseudo-clock tick (100ms) */
    SYSCALL(WAITCLOCK, 0, 0, 0);

    /* 2. Obtain mutual exclusion over the ADL */
    SYSCALL(PASSEREN, (int)&adlMutex, 0, 0);

    /* 3. Wake up all U-procs whose wake up time has passed */
    STCK(currentTime);
    while (delayd_h->d_next->d_wakeTime <= currentTime) {
      delayd_t *delayd = removeDelaydHead();
      if (delayd == NULL) {
        /* This can only happen if the current time is MAXINT, but we assume
         * this is not the case */
        PANIC();
      }
      SYSCALL(VERHOGEN, (int)&delayd->d_supStruct->sup_privateSem, 0, 0);
      freeDelayd(delayd);
    }

    /* 4. Release mutual exclusion over the ADL */
    SYSCALL(VERHOGEN, (int)&adlMutex, 0, 0);
  }
}

void initADL() {
  /* Add two dummy delay descriptor nodes to support ADL traversal */
  static delayd_t delaydList[MAX_UPROCS + 2];

  /* Initialize the active delay list with two dummy nodes */
  delayd_h = &delaydList[0];
  delayd_t *tail = &delaydList[1];
  delayd_h->d_next = tail;
  delayd_h->d_wakeTime = (cpu_t)0;
  delayd_h->d_supStruct = NULL;
  tail->d_next = NULL;
  tail->d_wakeTime = (cpu_t)MAXINT;
  tail->d_supStruct = NULL;

  /* Initialize the list of unused delay event descriptors */
  delaydFree_h = NULL;
  int i;
  for (i = 2; i < MAX_UPROCS + 2; i++) {
    freeDelayd(&delaydList[i]);
  }

  /* Initialize the mutual exclusion semaphore for accessing the ADL */
  adlMutex = 1;

  /* Prepare the daemon state */
  state_t daemonState;

  /* Set PC (and s_t9) to the start of the Delay Daemon function */
  daemonState.s_pc = daemonState.s_t9 = (memaddr)delayDaemon;

  /* Set SP to the second-to-last frame in RAM. Last frame is stack
   * page for test() process */
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  memaddr RAMTOP = RAMSTART + busRegArea->ramsize;
  /* TODO: may need to adjust this stack page */
  daemonState.s_sp = RAMTOP - PAGESIZE;

  /* Enable interrupts and processor Local Timer and turn kernel-mode on */
  daemonState.s_status = STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;

  /* Set ASID to kernel ASID 0 */
  daemonState.s_entryHI = (0 << ASID_SHIFT);

  /* Launch the daemon process */
  int status = SYSCALL(CREATEPROCESS, (int)&daemonState, (int)NULL, 0);

  /* Error creating the daemon process, terminate the current process */
  if (status == ERR) {
    SYSCALL(TERMINATEPROCESS, 0, 0, 0);
  }
}

void sysDelay(state_t *excState, support_t *sup) { /* TODO */ }
