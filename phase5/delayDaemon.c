#include "../h/delayDaemon.h"

#include "../h/const.h"
#include "../h/types.h"
#include "umps3/umps/libumps.h"

/* Delay event descriptor type */
typedef struct delayd_t {
    struct delayd_t *d_next;     /* next element on the ADL */
    int             d_wakeTime;  /* time of day (microseconds) to wake up */
    support_t       *d_supStruct;/* pointer to the support structure of sleeping U-proc */
} delayd_t;

/* Global pointer to the head of the active delay list */
HIDDEN delayd_t *delayd_h;

/* Global pointer to the head of the free delay descriptor list */
HIDDEN delayd_t *delaydFree_h;

/* Mutual exclusion semaphore for accessing the ADL */
HIDDEN int adlMutex;

void delayDaemon();

/**
 * Initialize a delay descriptor to default values
 */
HIDDEN void initDelayd(delayd_t *d) {
    d->d_next = NULL;
    d->d_wakeTime = 0;
    d->d_supStruct = NULL;
}

/**
 * Return a delay descriptor back to the free list
 */
HIDDEN void freeDelayd(delayd_t *d) {
    /* Add the descriptor to the head of the free list */
    d->d_next = delaydFree_h;
    delaydFree_h = d;
}

/**
 * Allocate a delay descriptor from the free list
 */
HIDDEN delayd_t *allocDelayd() {
    if (delaydFree_h == NULL) {
        /* The delaydFree list is empty */
        return NULL;
    }

    /* Remove an element from the delaydFree list */
    delayd_t *d = delaydFree_h;
    delaydFree_h = delaydFree_h->d_next;
    initDelayd(d);

    return d;
}

/**
 * Insert a delay descriptor node into the ADL, sorted by wake time
 * Find the correct position based on d_wakeTime and inserts the node
 */
HIDDEN void insertDelayd(delayd_t *d) {
    delayd_t *prev = delayd_h;
    delayd_t *curr = delayd_h->d_next;

    /* Traverse ADL to find the correct insertion point */
    while (curr->d_wakeTime != MAXINT && curr->d_wakeTime < d->d_wakeTime) {
        prev = curr;
        curr = curr->d_next;
    }

    /* Insert d between prev and curr */
    d->d_next = curr;
    prev->d_next = d;
}

/**
 * Remove and return the head node from the ADL (the one after the dummy head). Does not free the node
 */
HIDDEN delayd_t *removeDelaydHead() {
    /* List is empty (only head and tail dummies) */
    if (delayd_h->d_next->d_wakeTime == MAXINT) {
        return NULL;
    }

    delayd_t *head = delayd_h->d_next;
    delayd_h->d_next = head->d_next;
    head->d_next = NULL; /* Cut off the removed node */

    return head;
}

/**
 * Get the head node from the ADL without removing it
 */
HIDDEN delayd_t *headDelayd() {
    /* List is empty (only head and tail dummies) */
    if (delayd_h->d_next->d_wakeTime == MAXINT) {
        return NULL;
    }
    return delayd_h->d_next;
}


void initADL() {
    /* Add two dummy semaphores to support ADL traversal */
    static delayd_t delaydTable[MAX_UPROCS + 2];

    /* Initialize the active delay list with two dummy semaphores */
    delayd_h = &delaydTable[0];
    delayd_t *tail_dummy = &delaydTable[1];
    delayd_h->d_next = tail_dummy;
    delayd_h->d_wakeTime = 0;
    delayd_h->d_supStruct = NULL;
    tail_dummy->d_next = NULL;
    tail_dummy->d_wakeTime = MAXINT;
    tail_dummy->d_supStruct = NULL;

    /* Initialize the list of unused delay event descriptors */
    delaydFree_h = NULL;
    int i;
    for (i = 2; i < MAX_UPROCS + 2; i++) {
        freeDelayd(&delaydTable[i]);
    }

    /* Initialize the mutual exclusion semaphore for accessing the ADL */
    adlMutex = 1;

    /* Prepare the daemon state */
    state_t daemonState;
    
    /* Initialize daemon state with default zeros */
    for (i = 0; i < STATEREGNUM; i++) {
        daemonState.s_reg[i] = 0;
    }
    daemonState.s_entryHI = 0;
    daemonState.s_cause = 0;
    daemonState.s_status = 0;
    daemonState.s_pc = 0;


    /* Change specific values in daemon state */
    /* Set PC to the start of the Delay Daemon function */
    daemonState.s_pc = (memaddr)delayDaemon;
    /* This register must get the same value as PC whenever PC is assigned a new value */
    daemonState.s_t9 = (memaddr)delayDaemon;

    /* Set Stack Pointer to the second-to-last frame in RAM. Last frame is stack page for test() process */
    devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
    memaddr ramTop = RAMSTART + busRegArea->ramsize;
    daemonState.s_sp = ramTop - PAGESIZE;

    /* Enable interrupts and processor Local Timer and turn kernel-mode on */
    daemonState.s_status = ZERO_MASK | STATUS_IEP | STATUS_IM_ALL_ON | STATUS_TE;

    /* Set ASID to kernel ASID 0 */
    daemonState.s_entryHI = (0 << ASID_SHIFT);

    /* Launch the daemon process using SYS1 */
    int status = SYSCALL(CREATEPROCESS, (int)&daemonState, (int)NULL, 0);

    /* Error creating the daemon process, terminate the current process */
    if (status != OK) {
        SYSCALL(TERMINATEPROCESS, 0, 0, 0);
      }
}


void delayDaemon() {
    cpu_t currentTime;
    delayd_t *expiredNode;
    support_t *supStruct;
    int *privateSem;

    while (TRUE) {
        /* 1. Wait for the next pseudo-clock tick (100ms) */
        SYSCALL(WAITCLOCK, 0, 0, 0);

        /* 2. Obtain mutual exclusion over the ADL */
        SYSCALL(PASSEREN, (int)&adlMutex, 0, 0);

        /* 3. Process the ADL */
        STCK(currentTime); /* Get current time */
        expiredNode = headDelayd();
        
        /* 4. Wake up all U-procs whose time has expired */
        while (expiredNode != NULL && expiredNode->d_wakeTime <= currentTime) {
            expiredNode = removeDelaydHead();

            /* Perform a V operation on the private semaphore of U-proc first before freeing the node */
            supStruct = expiredNode->d_supStruct;
            if (supStruct != NULL) {
                privateSem = &(supStruct->sup_privSem);
                SYSCALL(VERHOGEN, (int)privateSem, 0, 0);
            }

            freeDelayd(expiredNode);
            expiredNode = headDelayd(); /* New head */
        }

        /* 5. Release mutual exclusion over the ADL */
        SYSCALL(VERHOGEN, (int)&adlMutex, 0, 0);
    }
}

int sysDelay(support_t *sup) {
    /* TODO */
}
