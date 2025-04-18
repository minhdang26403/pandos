/**
 * @file exceptions.c
 * @author Dang Truong, Loc Pham
 * @brief This module implements the Exception Handling routines for Phase 2. It
 * handles system call exceptions, program trap exceptions, TLB exceptions, and
 * device interrupts. The module defines handlers for various system call
 * services (SYS1 through SYS8), as well as helper functions for process
 * termination, state copying, process blocking on a semaphore, and exception
 * pass-up.
 * @date 2025-04-17
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/exceptions.h"

#include "../h/asl.h"
#include "../h/initial.h"
#include "../h/interrupts.h"
#include "../h/pcb.h"
#include "../h/scheduler.h"
#include "umps3/umps/libumps.h"

/**
 * @brief Copy the contents of one processor state to another.
 *
 * Copies scalar fields (EntryHI, Cause, Status, PC) and all general-purpose
 * registers.
 *
 * @param dest Pointer to the destination state.
 * @param src Pointer to the source state.
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
 * @brief SYS1: Create a new child process.
 *
 * Allocates a new PCB, initializes its state from the given pointer (in s_a1),
 * sets its support structure pointer from s_a2, and inserts it into the ready
 * queue. Sets s_v0 to 0 on success or -1 if no free PCBs are available.
 *
 * @param savedExcState The saved exception state of the calling process.
 */
HIDDEN void sysCreateProc(state_t *savedExcState) {
  pcb_PTR p = allocPcb();

  if (p == NULL) {
    /* No more free pcb's */
    savedExcState->s_v0 = ERR;
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
    savedExcState->s_v0 = OK;
  }

  switchContext(savedExcState);
}

/**
 * @brief Recursively terminate a process and all its descendants.
 *
 * Detaches the process from its parent, removes it from queues, adjusts
 * semaphore values (including softBlockCnt for device semaphores), frees its
 * PCB, and decrements procCnt.
 *
 * @param p Pointer to the root of the process tree to terminate.
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
 * @brief SYS2: Terminate the current process and its children.
 *
 * Recursively kills the process subtree rooted at the current process, then
 * invokes the scheduler.
 *
 * @param savedExcState The saved exception state of the calling process.
 * @return This function does not return; control is transferred to the
 * scheduler.
 */
HIDDEN void sysTerminateProc(state_t *savedExcState) {
  terminateProcHelper(currentProc);
  /* Call the scheduler to find another process to run without pushing the
   * currently running process to the queue again */
  scheduler();
}

/**
 * @brief Block the current process on a semaphore.
 *
 * Saves the current process's exception state, updates its CPU time, inserts it
 * into the blocked list for the given semaphore, and dispatches the scheduler.
 *
 * @param sem Pointer to the semaphore on which to block.
 * @param savedExcState The saved exception state of the current process.
 * @return This function does not return; control is transferred to the
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
 * @brief SYS3: Perform a Passeren (P) operation on a semaphore.
 *
 * Decrements the semaphore in s_a1. If the value becomes negative, the current
 * process is blocked on that semaphore.
 *
 * @param savedExcState The saved exception state of the calling process.
 * @return This function does not return; control is transferred via
 * switchContext or waitOnSem.
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
 * @brief SYS4: Perform a Verhogen (V) operation on a semaphore.
 *
 * Increments the semaphore in s_a1. If the new value is less than or equal to
 * 0, one process waiting on the semaphore is unblocked and moved to the ready
 * queue.
 *
 * @param savedExcState The saved exception state of the calling process.
 * @return This function does not return; control is transferred via
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
 * @brief SYS5: Wait for an I/O device to complete.
 *
 * Computes the appropriate device semaphore based on the interrupt line (s_a1),
 * device number (s_a2), and terminal direction (s_a3). Performs a P operation
 * on the computed device semaphore, increments softBlockCnt, and blocks the
 * process.
 *
 * @param savedExcState The saved exception state of the calling process.
 * @return This function does not return; control is transferred via waitOnSem.
 */
HIDDEN void sysWaitIO(state_t *savedExcState) {
  int lineNum = savedExcState->s_a1; /* Interrupt line number (3-7) */
  int devNum = savedExcState->s_a2;  /* Device number (0-7) */
  int waitForTermRead =
      savedExcState->s_a3; /* 1 if waiting for terminal read, 0 for write */

  /**
   * Calculate semaphore index:
   * - Lines 3-6: 0-31 (non-terminals)
   * - Line 7 (TERMINT): 32-39 (write), 40-47 (read)
   * - Offset from DISKINT (line 3), adjust by waitForTermRead for terminal
   * reads
   */
  int semIdx = (lineNum - DISKINT + waitForTermRead) * DEVPERINT + devNum;
  int *sem = &deviceSem[semIdx];
  (*sem)--;
  softBlockCnt++;                /* Process now waiting for I/O */
  waitOnSem(sem, savedExcState); /* Always block */
}

/**
 * @brief SYS6: Return total CPU time used by the current process.
 *
 * Computes the sum of the process's accumulated CPU time (p_time) and the
 * elapsed time in the current quantum. The result is returned in s_v0.
 *
 * @param savedExcState The saved exception state of the calling process.
 * @return This function does not return; control is transferred via
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
 * @brief SYS7: Wait on the pseudo-clock semaphore.
 *
 * Decrements the pseudo-clock semaphore, increments softBlockCnt, and blocks
 * the process.
 *
 * @param savedExcState The saved exception state of the calling process.
 * @return This function does not return; control is transferred via waitOnSem.
 */
HIDDEN void sysWaitForClock(state_t *savedExcState) {
  int *sem = &deviceSem[PSEUDOCLOCK]; /* Pseudo–clock semaphore  */
  (*sem)--;
  softBlockCnt++;                /* Process now waiting for pseudo-clock */
  waitOnSem(sem, savedExcState); /* Always block */
}

/**
 * @brief SYS8: Return the support structure pointer for the current process.
 *
 * Sets s_v0 to the address of the current process's support structure.
 *
 * @param savedExcState The saved exception state of the calling process.
 * @return This function does not return; control is transferred via
 * switchContext.
 */
HIDDEN void sysGetSupportData(state_t *savedExcState) {
  savedExcState->s_v0 = (int)currentProc->p_supportStruct;
  switchContext(savedExcState);
}

/**
 * @brief Pass the exception to the support-level handler, or terminate the
 * process.
 *
 * If the process has a valid support structure, its exception state is copied
 * into the appropriate slot, and the corresponding context is loaded.
 * Otherwise, the process is terminated.
 *
 * @param savedExcState The saved exception state of the current process.
 * @param exceptType Either PGFAULTEXCEPT or GENERALEXCEPT.
 * @return This function does not return; control is transferred via loadContext
 * or sysTerminateProc.
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
 * @brief Handle system calls invoked via syscall exception.
 *
 * If the syscall number in s_a0 is between 1 and 8 and the process is in kernel
 * mode, the corresponding service is invoked. If the process is in user mode,
 * or the syscall number is invalid, the syscall is treated as a program trap
 * and passed up or terminates.
 *
 * @param savedExcState The saved exception state of the calling process.
 * @return This function does not return; control is transferred via syscall or
 * exception handler.
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
      savedExcState->s_pc += WORDLEN; /* control of the current process should
                                         be returned to the next instruction */
      syscalls[num](savedExcState);
    }
  } else {
    passUpOrDie(savedExcState, GENERALEXCEPT);
  }
}

/**
 * @brief Top-level handler for all exceptions.
 *
 * Reads the exception code and dispatches control to:
 * - interruptHandler for device interrupts
 * - passUpOrDie(PGFAULTEXCEPT) for TLB exceptions
 * - passUpOrDie(GENERALEXCEPT) for program traps
 * - syscallHandler for syscall exceptions
 * Calls PANIC() for unknown exception codes.
 *
 * @return This function does not return; control is transferred to appropriate
 * handlers.
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

/**
 * @brief Handle TLB refill exception (Phase 2 version).
 *
 * Extracts the VPN from the exception state, finds the corresponding PTE in the
 * process's private page table, and writes it into the TLB. If the process
 * lacks a support structure, it is terminated. Execution resumes from the
 * faulting instruction.
 *
 * @return This function does not return; control is transferred via
 * switchContext or termination.
 */
void uTLB_RefillHandler() {
  /* Get saved exception state from BIOS Data Page */
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;

  /* Extract VPN: mask then shift */
  unsigned int entryHI = savedExcState->s_entryHI;
  unsigned int vpn = (entryHI & VPN_MASK) >> VPN_SHIFT;

  /* Determine the page table index for the missing entry */
  unsigned int pageIdx = vpn % MAXPAGES;

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
