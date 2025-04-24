/**
 * @file delayDaemon.c
 * @author Dang Truong, Loc Pham
 * @brief Implements Phase 5 (Level 6) Delay Facility. Handles the
 * initialization of Active Delay List (ADL) and delay event descriptor free
 * list, delay system call (SYS18), and delay daemon for waking up U-procs after
 * requested sleep time.
 * @date 2025-04-24
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/delayDaemon.h"

#include "../h/const.h"
#include "../h/scheduler.h"
#include "../h/sysSupport.h"
#include "../h/types.h"
#include "umps3/umps/libumps.h"

/* Delay event descriptor node */
typedef struct delayd_t {
  struct delayd_t *d_next; /* next node in the ADL or free list */
  cpu_t d_wakeTime;        /* system time when U-proc should be woken */
  support_t *d_supStruct;  /* support structure of the sleeping U-proc */
} delayd_t;

/* The sleeping U-proc list, implemented as a single, linearly linked list of
 * delay event descriptor nodes sorted by wake up time */
HIDDEN delayd_t *delayd_h;

/* The free delay descriptor list, implemented as a NULL-terminated, single,
 * linearly linked list */
HIDDEN delayd_t *delaydFree_h;

/* Mutual exclusion semaphore for ADL access (initialized to 1) */
HIDDEN int adlMutex;

/* ==================== Local Function Declarations ==================== */
HIDDEN void delayDaemon();
HIDDEN void initDelayd(delayd_t *delayd);
HIDDEN void freeDelayd(delayd_t *delayd);
HIDDEN delayd_t *allocDelayd();
HIDDEN void insertDelayd(delayd_t *delayd);
HIDDEN delayd_t *removeDelaydHead();

/* ==================== Public Function Definitions ==================== */

/**
 * @brief Initialize the Active Delay List (ADL) and launch the Delay Daemon.
 *
 * Sets up the ADL with dummy head and tail nodes, populates the free list of
 * delay descriptors, initializes mutual exclusion, and creates the Delay
 * Daemon.
 */
void initADL() {
  /* Static storage for ADL: MAX_UPROCS nodes + 2 dummy nodes */
  static delayd_t delaydPool[MAX_UPROCS + 2];

  /* Initialize ADL dummy head and tail */
  delayd_h = &delaydPool[0];
  delayd_t *tail = &delaydPool[1];
  delayd_h->d_next = tail;
  delayd_h->d_wakeTime = (cpu_t)0;
  delayd_h->d_supStruct = NULL;
  tail->d_next = NULL;
  tail->d_wakeTime = (cpu_t)MAXINT;
  tail->d_supStruct = NULL;

  /* Populate the free list with remaining descriptors */
  delaydFree_h = NULL;
  int i;
  for (i = 2; i < MAX_UPROCS + 2; i++) {
    freeDelayd(&delaydPool[i]);
  }

  /* Initialize the mutual exclusion semaphore for ADL */
  adlMutex = 1;

  /* Prepare daemon process state */
  state_t daemonState;
  daemonState.s_pc = daemonState.s_t9 = (memaddr)delayDaemon;

  /* Assign stack to second-to-last RAM page (last is used by init()) */
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  memaddr RAMTOP = RAMSTART + busRegArea->ramsize;
  daemonState.s_sp = RAMTOP - PAGESIZE;

  /* Enable interrupts, timers, and set kernel mode */
  daemonState.s_status = STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;

  /* Use kernel ASID (0) */
  daemonState.s_entryHI = (0 << ASID_SHIFT);

  /* Launch Delay Daemon process */
  int status = SYSCALL(CREATEPROCESS, (int)&daemonState, (int)NULL, 0);

  /* Terminate if daemon creation fails */
  if (status == ERR) {
    SYSCALL(TERMINATEPROCESS, 0, 0, 0);
  }
}

/**
 * @brief Implements SYS18: Delay the calling U-proc for a specified number of
 * seconds.
 *
 * The U-proc is blocked on its private semaphore and scheduled to be woken
 * after `sleepTime` seconds by the Delay Daemon. Access to the shared Active
 * Delay List (ADL) is synchronized via `adlMutex`.
 *
 * @param excState Saved exception state containing syscall arguments
 * @param sup      Support structure of the requesting U-proc
 */
void sysDelay(state_t *excState, support_t *sup) {
  /* Extract requested delay duration (in seconds) */
  cpu_t sleepTime = excState->s_a1;

  if (sleepTime < 0) {
    /* Reject negative sleep durations */
    programTrapHandler(sup);
  }

  /* Obtain mutual exclusion over the ADL */
  SYSCALL(PASSEREN, (int)&adlMutex, 0, 0);

  /* Try to get a delay descriptor node from the free list */
  delayd_t *delayd = allocDelayd();
  if (delayd == NULL) {
    SYSCALL(VERHOGEN, (int)&adlMutex, 0, 0);
    programTrapHandler(sup);
  }

  /* Compute wake-up time (in microseconds since boot) */
  cpu_t currentTime;
  STCK(currentTime);
  delayd->d_wakeTime = currentTime + sleepTime * SECOND;

  /* Associate this descriptor with the calling U-proc and insert it into ADL */
  delayd->d_supStruct = sup;
  insertDelayd(delayd);

  /* Atomically release ADL mutex and block U-proc on its private semaphore */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC); /* Disable interrupts */
  SYSCALL(VERHOGEN, (int)&adlMutex, 0, 0);
  SYSCALL(PASSEREN, (int)&sup->sup_privateSem, 0, 0);
  setSTATUS(status); /* Reenable interrupts */

  /* Control resumes here after wake-up; return to U-proc */
  switchContext(excState);
}

/* ==================== Delay Daemon ==================== */

/**
 * @brief Daemon process to manage delayed U-procs.
 *
 * This kernel-mode process wakes up every 100ms via SYS7 and checks the Active
 * Delay List (ADL) for expired timers. U-procs whose delay has expired are
 * unblocked by performing a SYS4 (VERHOGEN) on their private semaphores.
 */
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
        /* Should never happen */
        PANIC();
      }

      /* Wake the sleeping U-proc */
      SYSCALL(VERHOGEN, (int)&delayd->d_supStruct->sup_privateSem, 0, 0);

      /* Recycle the descriptor */
      freeDelayd(delayd);
    }

    /* 4. Release mutual exclusion over the ADL */
    SYSCALL(VERHOGEN, (int)&adlMutex, 0, 0);
  }
}

/* ==================== ADL Node Management Helpers ==================== */

/**
 * @brief Initialize a delay descriptor to default values.
 *
 * @param delayd Pointer to the delay descriptor node.
 */
HIDDEN void initDelayd(delayd_t *delayd) {
  delayd->d_next = NULL;
  delayd->d_wakeTime = 0;
  delayd->d_supStruct = NULL;
}

/**
 * @brief Return a delay descriptor to the free list by pushing the given
 * descriptor node to the head of the delaydFree list.
 *
 * @param delayd Pointer to the descriptor being returned.
 */
HIDDEN void freeDelayd(delayd_t *delayd) {
  /* Add the descriptor to the head of the free list */
  delayd->d_next = delaydFree_h;
  delaydFree_h = delayd;
}

/**
 * @brief Allocate a delay descriptor from the free list by removing the first
 * element from the delaydFree list, initializing it before use.
 *
 * @return Pointer to a reusable delay descriptor node, or NULL if none
 * available.
 */
HIDDEN delayd_t *allocDelayd() {
  if (delaydFree_h == NULL) {
    /* No available delay descriptors */
    return NULL;
  }

  delayd_t *delayd = delaydFree_h;
  delaydFree_h = delaydFree_h->d_next;
  initDelayd(delayd);

  return delayd;
}

/**
 * @brief Insert a delay descriptor into the Active Delay List (ADL).
 *
 * The ADL is maintained in ascending order of wake-up time. This function finds
 * the correct position and inserts the node so the list remains sorted.
 *
 * Assumes delayd_h is a dummy head node always present at the head of the ADL.
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
 * @brief Remove and return the first real delay descriptor in the ADL.
 *
 * This removes the node immediately after the dummy head if it exists. If the
 * ADL contains only the dummy tail, NULL is returned.
 *
 * The caller is responsible for freeing the node (e.g., returning to free
 * list).
 *
 * @return Pointer to the first delay descriptor node, or NULL if none exist.
 */
HIDDEN delayd_t *removeDelaydHead() {
  if (delayd_h->d_next->d_wakeTime == MAXINT) {
    /* List is empty (only dummy head and dummy tail remain) */
    return NULL;
  }

  delayd_t *head = delayd_h->d_next;
  delayd_h->d_next = head->d_next;

  return head;
}
