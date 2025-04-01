/************************** EXCEPTIONS.C ******************************
 *
 * The Exception Handling Module.
 *
 * Description:
 *   This module implements the Exception Handling routines for Phase 2. It
 * handles system call exceptions, program trap exceptions, TLB exceptions, and
 * device interrupts. The module defines handlers for various system call
 * services (SYS1 through SYS8), as well as helper functions for process
 * termination, state copying, process blocking on a semaphore, and exception
 * pass-up.
 *
 * Written by Dang Truong, Loc Pham
 */

/***************************************************************/

#include "../h/exceptions.h"

#include "../h/asl.h"
#include "../h/initial.h"
#include "../h/interrupts.h"
#include "../h/pcb.h"
#include "../h/scheduler.h"
#include "umps3/umps/libumps.h"

/**
 * Function: copyState
 * -------------------
 * Purpose:
 *   Copies the contents of one state_t structure to another. This includes
 *   copying scalar fields and all register values.
 *
 * Parameters:
 *   dest - Pointer to the destination state_t structure.
 *   src  - Pointer to the source state_t structure.
 *
 * Returns:
 *   None.
 */
void copyState(state_t *dest, state_t *src) {
  /* Copy scalar fields */
  dest->s_entryHI = src->s_entryHI;
  dest->s_cause = src->s_cause;
  dest->s_status = src->s_status;
  dest->s_pc = src->s_pc;

  /* Copy the register array */
  int i;
  for (i = 0; i < STATEREGNUM; i++) {
    dest->s_reg[i] = src->s_reg[i];
  }
}

/**
 * Function: sysCreateProc
 * -------------------------
 * Purpose:
 *   Implements the SYS1 service to create a new process. This function
 * allocates a new PCB, initializes its processor state using the state provided
 * in savedExcState->s_a1, sets its support structure pointer from
 * savedExcState->s_a2, and inserts the new process into the ready queue. It
 * returns 0 in s_v0 upon success or -1 if no PCB is available.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state of the calling
 * process.
 *
 * Returns:
 *   This function does not return normally; it calls switchContext to transfer
 * control.
 */
HIDDEN void sysCreateProc(state_t *savedExcState) {
  pcb_PTR p = allocPcb();

  if (p == NULL) {
    /* No more free pcb's */
    savedExcState->s_v0 = -1;
  } else {
    /* Successfully create a new process */
    state_t *statep = (state_t *)savedExcState->s_a1;
    support_t *supportp = (support_t *)savedExcState->s_a2;

    /* Initialize all fields of the new process */
    copyState(&p->p_s, statep);
    p->p_time = 0;
    p->p_semAdd = NULL;
    p->p_supportStruct = supportp;

    /* Make this process alive */
    insertProcQ(&readyQueue, p);
    insertChild(currentProc, p);
    procCnt++;
    savedExcState->s_v0 = 0;
  }

  switchContext(savedExcState);
}

/**
 * Function: terminateProcHelper
 * -----------------------------
 * Purpose:
 *   Recursively terminates the process tree rooted at the given PCB. It removes
 * each process from its parent's child list, the ready queue, or the blocked
 * list, adjusts semaphore counts as needed, frees the PCB, and decrements the
 * process count.
 *
 * Parameters:
 *   p - Pointer to the PCB of the process to terminate.
 *
 * Returns:
 *   None.
 */
HIDDEN void terminateProcHelper(pcb_PTR p) {
  if (p == NULL) {
    return;
  }

  /* Recursively terminate all child processes first */
  pcb_PTR child;
  while ((child = removeChild(p)) != NULL) {
    terminateProcHelper(child);
  }

  if (p == currentProc) {
    /* p is the current process, so remove p as a child of its parent */
    outChild(currentProc);
    currentProc = NULL;
  } else if (p->p_semAdd != NULL) {
    /* p is blocked on the ASL */
    int *sem = p->p_semAdd;
    outBlocked(p);
    if (sem >= deviceSem && sem < deviceSem + NUMDEVICES) {
      /* Device semaphore will be adjusted in device interrupt handler */
      softBlockCnt--;
    } else {
      /* Adjust non-device semaphore */
      (*sem)++;
    }
  } else {
    /* p is on the ready queue */
    outProcQ(&readyQueue, p);
  }

  /* Clean up the process */
  freePcb(p);
  procCnt--;
}

/**
 * Function: sysTerminateProc
 * --------------------------
 * Purpose:
 *   Implements the SYS2 service to terminate the current process and all its
 * progeny. It calls terminateProcHelper on the current process and then invokes
 * the scheduler to dispatch the next process.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state of the calling
 * process.
 *
 * Returns:
 *   This function does not return normally; control is transferred via
 * scheduler.
 */
HIDDEN void sysTerminateProc(state_t *savedExcState) {
  terminateProcHelper(currentProc);
  /* Call the scheduler to find another process to run without pushing the
   * currently running process to the queue again */
  scheduler();
}

/**
 * Function: waitOnSem
 * -------------------
 * Purpose:
 *   Helper function to block the current process on a semaphore. It saves the
 * current process's state, updates its accumulated CPU time, puts it into the
 * blocked list for the semaphore, and then calls the scheduler.
 *
 * Parameters:
 *   sem           - Pointer to the semaphore on which the process will wait.
 *   savedExcState - Pointer to the saved exception state of the calling
 * process.
 *
 * Returns:
 *   This function does not return normally; control is transferred via
 * scheduler.
 */
HIDDEN void waitOnSem(int *sem, state_t *savedExcState) {
  /* Save the exception state */
  copyState(&currentProc->p_s, savedExcState);
  /* Update the accumulated CPU time for the Current Process */
  cpu_t now;
  STCK(now);
  currentProc->p_time += now - quantumStartTime;
  /* Block the current process and find another process to run */
  insertBlocked(sem, currentProc);
  currentProc = NULL;
  scheduler();
}

/**
 * Function: sysPasseren
 * ---------------------
 * Purpose:
 *   Implements the SYS3 service (Passeren/P operation) by decrementing the
 * semaphore specified in savedExcState->s_a1. If the semaphore value becomes
 * negative, the current process is blocked on the semaphore.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state of the calling
 * process.
 *
 * Returns:
 *   This function does not return normally; it transfers control via
 * switchContext.
 */
HIDDEN void sysPasseren(state_t *savedExcState) {
  int *sem = (int *)savedExcState->s_a1;
  (*sem)--;
  if (*sem < 0) {
    waitOnSem(sem, savedExcState);
  }

  switchContext(savedExcState);
}

/**
 * Function: sysVerhogen
 * ---------------------
 * Purpose:
 *   Implements the SYS4 service (Verhogen/V operation) by incrementing the
 * semaphore specified in savedExcState->s_a1. If the semaphore value is less
 * than or equal to zero, a blocked process is unblocked and moved to the ready
 * queue.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state of the calling
 * process.
 *
 * Returns:
 *   This function does not return normally; it transfers control via
 * switchContext.
 */
HIDDEN void sysVerhogen(state_t *savedExcState) {
  int *sem = (int *)savedExcState->s_a1;
  (*sem)++;
  if (*sem <= 0) {
    /* Unblock one process waiting on this semaphore, if any */
    pcb_PTR p = removeBlocked(sem);
    if (p != NULL) {
      insertProcQ(&readyQueue, p);
    }
  }

  switchContext(savedExcState);
}

/**
 * Function: sysWaitIO
 * -------------------
 * Purpose:
 *   Implements the SYS5 service to wait for an I/O device. It calculates the
 * appropriate semaphore index based on the interrupt line number, device
 * number, and whether the operation is a terminal read or write. The function
 * then decrements the device semaphore, increments the soft-block count, and
 * blocks the current process.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state of the calling
 * process.
 *
 * Returns:
 *   This function does not return normally; it transfers control via waitOnSem.
 */
HIDDEN void sysWaitIO(state_t *savedExcState) {
  int lineNum = savedExcState->s_a1; /* Interrupt line number (3-7) */
  int devNum = savedExcState->s_a2;  /* Device number (0-7) */
  int waitForTermRead =
      savedExcState->s_a3; /* 1 if waiting for terminal read, 0 for write */

  /* Calculate semaphore index:
    - Lines 3-6: 0-31 (non-terminals)
    - Line 7 (TERMINT): 32-39 (write), 40-47 (read)
    - Offset from DISKINT (line 3), adjust by waitForTermRead for terminal reads
  */
  int semIdx = (lineNum - DISKINT + waitForTermRead) * DEVPERINT + devNum;
  int *sem = &deviceSem[semIdx];
  (*sem)--;
  softBlockCnt++;                /* Process now waiting for I/O */
  waitOnSem(sem, savedExcState); /* Always block */
}

/**
 * Function: sysGetCPUTime
 * -----------------------
 * Purpose:
 *   Implements the SYS6 service to obtain the total CPU time consumed by the
 * current process. The function adds the accumulated CPU time (p_time) with the
 * elapsed time in the current quantum and returns the result in
 * savedExcState->s_v0.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state of the calling
 * process.
 *
 * Returns:
 *   This function does not return normally; it transfers control via
 * switchContext.
 */
HIDDEN void sysGetCPUTime(state_t *savedExcState) {
  cpu_t now;
  STCK(now);
  cpu_t elapsed = now - quantumStartTime;
  savedExcState->s_v0 = currentProc->p_time + elapsed;
  switchContext(savedExcState);
}

/**
 * Function: sysWaitForClock
 * -------------------------
 * Purpose:
 *   Implements the SYS7 service to wait for the pseudo-clock. It decrements the
 * pseudo-clock semaphore, increments the soft-block count, and blocks the
 * current process.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state of the calling
 * process.
 *
 * Returns:
 *   This function does not return normally; it transfers control via waitOnSem.
 */
HIDDEN void sysWaitForClock(state_t *savedExcState) {
  int *sem = &deviceSem[PSEUDOCLOCK]; /* Pseudo–clock semaphore  */
  (*sem)--;
  softBlockCnt++;                /* Process now waiting for pseudo-clock */
  waitOnSem(sem, savedExcState); /* Always block */
}

/**
 * Function: sysGetSupportData
 * ---------------------------
 * Purpose:
 *   Implements the SYS8 service to retrieve the support structure pointer of
 * the current process. The pointer is returned in savedExcState->s_v0.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state of the calling
 * process.
 *
 * Returns:
 *   This function does not return normally; it transfers control via
 * switchContext.
 */
HIDDEN void sysGetSupportData(state_t *savedExcState) {
  savedExcState->s_v0 = (int)currentProc->p_supportStruct;
  switchContext(savedExcState);
}

/**
 * Function: passUpOrDie
 * ---------------------
 * Purpose:
 *   Determines whether an exception should be passed up to the Support Level or
 * cause the current process to terminate. If the current process has a non-NULL
 * support structure, its saved exception state is copied into that structure
 * and a new context is loaded. Otherwise, the process is terminated.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state.
 *   exceptType    - Exception type indicator (PGFAULTEXCEPT or GENERALEXCEPT).
 *
 * Returns:
 *   This function does not return normally; control is transferred via
 * loadContext or sysTerminateProc.
 */
HIDDEN void passUpOrDie(state_t *savedExcState, int exceptType) {
  support_t *supportStruct = currentProc->p_supportStruct;
  if (supportStruct == NULL) {
    /* Die */
    sysTerminateProc(savedExcState);
  } else {
    /* Pass up: Copy state and load new context */
    copyState(&supportStruct->sup_exceptState[exceptType], savedExcState);
    loadContext(&supportStruct->sup_exceptContext[exceptType]);
  }
}

/* Define the function pointer type for syscalls */
typedef void (*syscall_t)(state_t *);

/*
 * The syscall table maps syscall numbers (1-8) to the corresponding service
 * handler functions.
 */
HIDDEN syscall_t syscalls[] = {
    NULL,             /* Index 0 (Unused, since syscalls are 1-based) */
    sysCreateProc,    /* SYS 1 */
    sysTerminateProc, /* SYS 2 */
    sysPasseren,      /* SYS 3 */
    sysVerhogen,      /* SYS 4 */
    sysWaitIO,        /* SYS 5 */
    sysGetCPUTime,    /* SYS 6 */
    sysWaitForClock,  /* SYS 7 */
    sysGetSupportData /* SYS 8 */
};

/**
 * Function: syscallHandler
 * ------------------------
 * Purpose:
 *   Dispatches the system call based on the syscall number contained in
 * savedExcState->s_a0. If the syscall is invoked in user mode, it simulates a
 * program trap. Otherwise, it adjusts the program counter and invokes the
 * appropriate syscall service.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state of the calling
 * process.
 *
 * Returns:
 *   This function does not return normally; control is transferred via the
 * invoked syscall handler.
 */
HIDDEN void syscallHandler(state_t *savedExcState) {
  int num = savedExcState->s_a0;

  if (num >= 1 && num <= 8) {
    if (savedExcState->s_status & STATUS_KUP) {
      /* If previous mode was user mode (KUP = 1), simulate a program trap */
      savedExcState->s_cause =
          (savedExcState->s_cause & ~EXCCODE_MASK) | RI_EXCCODE;
      passUpOrDie(savedExcState, GENERALEXCEPT);
    } else {
      /* If previous mode was kernel mode (KUP = 0), handle the syscall */

      savedExcState->s_pc += 4; /* control of the current process should be
                                   returned to the next instruction */
      syscalls[num](savedExcState);
    }
  } else {
    passUpOrDie(savedExcState, GENERALEXCEPT);
  }
}

/**
 * Function: generalExceptionHandler
 * ---------------------------------
 * Purpose:
 *   Serves as the main exception handler. It reads the exception code from the
 * saved exception state and dispatches control to the appropriate handler:
 *     - Interrupts are forwarded to interruptHandler.
 *     - TLB exceptions are passed up using the PGFAULTEXCEPT index.
 *     - Program trap exceptions are passed up using the GENERALEXCEPT index.
 *     - Syscall exceptions invoke syscallHandler.
 *     - Unknown exception codes result in a PANIC.
 *
 * Parameters:
 *   None.
 *
 * Returns:
 *   This function does not return normally; control is transferred via the
 * appropriate exception handling mechanism.
 */
void generalExceptionHandler() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  unsigned int excCode = CAUSE_EXCCODE(savedExcState->s_cause);

  if (excCode == 0) {
    /* Interrupt exception */
    interruptHandler(savedExcState);
  } else if (excCode >= 1 && excCode <= 3) {
    /* TLB exception */
    passUpOrDie(savedExcState, PGFAULTEXCEPT);
  } else if ((excCode >= 4 && excCode <= 7) ||
             (excCode >= 9 && excCode <= 12)) {
    /* Program trap exception */
    passUpOrDie(savedExcState, GENERALEXCEPT);
  } else if (excCode == 8) {
    /* Syscall */
    syscallHandler(savedExcState);
  } else {
    /* Unknown exception code */
    PANIC();
  }
}

/* TLB-Refill handler: Level 3, kernel-mode, interrupts disabled */
void uTLB_RefillHandler() {
  /* Get saved exception state from BIOS Data Page */
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;

  /* Extract VPN: mask then shift */
  unsigned int entryHI = savedExcState->s_entryHI;
  unsigned int vpn = (entryHI & VPN_MASK) >> VPN_SHIFT;

  /* Determine the page table index for the missing entry */
  unsigned int pageIdx;
  if (vpn == VPN_STACK) {
    pageIdx = STACKPAGE; /* Stack page (31) */
  } else if (vpn >= VPN_TEXT_BASE && vpn < VPN_TEXT_BASE + TEXT_PAGE_COUNT) {
    pageIdx = vpn - VPN_TEXT_BASE; /* .text/.data pages (0-30) */
  } else {
    /* If the VPN is out of the expected range, panic */
    PANIC();
  }

  /* Get Page Table entry */
  support_t *sup = currentProc->p_supportStruct;
  if (sup == NULL) {
    /* Consistent with Phase 2 behavior (pass up or die) */
    sysTerminateProc(savedExcState);
  }
  pte_t *pte = &sup->sup_privatePgTbl[pageIdx];

  /* Write to TLB */
  setENTRYHI(pte->pte_entryHI);
  setENTRYLO(pte->pte_entryLO);
  TLBWR();

  /* Return control to the process to retry the instruction */
  switchContext(savedExcState);
}
