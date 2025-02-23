
#include "initial.h"
#include "pcb.h"
#include "scheduler.h"
#include "umps3/umps/libumps.h"

HIDDEN void sysCreateProc(state_t *savedExcState) {
  pcb_PTR p = allocPcb();

  if (p == NULL) {
    /* No more free pcb's */
    savedExcState->s_v0 = -1;
  } else {
    /* Successfully create a new process */
    state_t *statep = (state_t *)savedExcState->s_a1;
    support_t *supportp = (support_t *)savedExcState->s_a2;

    p->p_s = *statep;
    p->p_time = 0;
    p->p_semAdd = NULL;
    p->p_supportStruct = supportp;
    insertProcQ(&readyQueue, p);
    insertChild(currentProc, p);
    procCnt++;
    savedExcState->s_v0 = 0;
  }

  switchContext(savedExcState);
}

HIDDEN void terminateProcHelper(pcb_PTR p) {
  if (p == NULL) {
    return;
  }

  pcb_PTR child;
  /* Recursively terminate all child processes first */
  while ((child = removeChild(p))) {
    terminateProcHelper(child);
  }

  if (p == currentProc) {
    /* p is the current process, so remove p as a child of its parent */
    outChild(currentProc);
    currentProc = NULL;
  } else {
    /* p may be waiting on the ready queue or blocked on the ASL */
    pcb_PTR removedProc = outProcQ(&readyQueue, p);
    if (removedProc == NULL) {
      /* p is blocked on the ASL since we can't find p on the ready queue */
      outBlocked(p);
      softBlockCnt--;
    }
  }

  freePcb(p);
  procCnt--;
}

HIDDEN void sysTerminateProc(state_t *savedExcState) {
  terminateProcHelper(currentProc);
  /* Call the scheduler to find another process to run without pushing the
   * currently running process to the queue again */
  scheduler();
}

HIDDEN void waitOnSem(int *semaddr, state_t *savedExcState) {
  currentProc->p_s = *savedExcState;
  insertBlocked(semaddr, currentProc);
  softBlockCnt++;
  scheduler();
}

/* SYS3 – Passeren (P operation on a semaphore) */
HIDDEN void sysPasseren(state_t *savedExcState) {
  int *sem = (int *)savedExcState->s_a1;

  if (sem == NULL) {
    /* Caller passed in an valid semaphore address */
    savedExcState->s_v0 = -1;
    /* Return to the current process with an error code */
    switchContext(savedExcState);
  }

  (*sem)--;
  if (*sem < 0) {
    /* TODO: "Update the accumulated CPU time for the Current Process" in
     * section "3.5.11 Blocking SYSCALLs" in pandos.pdf ? */
    waitOnSem(sem, savedExcState);
  }

  /* TODO: should we support return value for P & V operations */
  savedExcState->s_v0 = 0;
  switchContext(savedExcState);
}

/* SYS4 – Verhogen (V operation on a semaphore) */
HIDDEN void sysVerhogen(state_t *savedExcState) {
  int *sem = (int *)savedExcState->s_a1;

  if (sem == NULL) {
    /* Caller passed in an valid semaphore address */
    savedExcState->s_v0 = -1;
    /* Return to the current process with an error code */
    switchContext(savedExcState);
  }

  (*sem)++;
  if (*sem <= 0) {
    /* Unblock one process waiting on this semaphore, if any */
    pcb_PTR p = removeBlocked(sem);
    if (p != NULL) {
      softBlockCnt--;
      insertProcQ(&readyQueue, p);
    }
  }

  savedExcState->s_v0 = 0;
  switchContext(savedExcState);
}

/* SYS5 – Wait for IO Device (perform P on the appropriate device semaphore) */
HIDDEN void sysWaitIO(state_t *savedExcState) {
  int lineNum = savedExcState->s_a1; /* Interrupt line number */
  int devNum = savedExcState->s_a2;  /* Device number */
  int waitForTermRead =
      savedExcState->s_a3; /* TRUE if waiting for terminal read */

  /* The first three lines are for Inter-process interrupts, Processor Local
   * Timer, Interval Timer. We have two sets of semaphores (each set with 8
   * semaphores) for terminal transmission and terminal receipt. */
  int semIndex = (lineNum - 3 + waitForTermRead) * DEVPERINT + devNum;
  int *semaddr = &deviceSem[semIndex];
  (*semaddr)--;

  /* TODO: "Update the accumulated CPU time for the Current Process" in
   * section "3.5.11 Blocking SYSCALLs" in pandos.pdf ? */
  waitOnSem(semaddr, savedExcState); /* always block */
}

/* SYS6 – Get CPU Time (return the accumulated CPU time of the current process)
 */
HIDDEN void sysGetCPUTime(state_t *savedExcState) {
  savedExcState->s_v0 = currentProc->p_time;
  switchContext(savedExcState);
}

/* SYS7 – Wait For Clock (perform P on the pseudo-clock semaphore) */
HIDDEN void sysWaitForClock(state_t *savedExcState) {
  int *sem = &deviceSem[NUMDEVICES]; /* Pseudo–clock semaphore  */
  (*sem)--;

  /* TODO: "Update the accumulated CPU time for the Current Process" in
   * section "3.5.11 Blocking SYSCALLs" in pandos.pdf ? */
  waitOnSem(sem, savedExcState); /* always block */
}

/* SYS8 – Get Support Data (return pointer to the current process's support
 * structure) */
HIDDEN void sysGetSupportData(state_t *savedExcState) {
  savedExcState->s_v0 = (int)currentProc->p_supportStruct;
  switchContext(savedExcState);
}

/* Define the function pointer type for syscalls */
typedef void (*syscall_t)(state_t *);

/* Declare the syscall table, mapping syscall numbers (1-8) to functions */
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

HIDDEN void passUpOrDie(state_t *savedExcState, int exceptType) {
  support_t *supportStruct = currentProc->p_supportStruct;
  if (supportStruct == NULL) {
    /* Die */
    sysTerminateProc(savedExcState);
  } else {
    /* Pass up: Copy state and load new context */
    supportStruct->sup_exceptState[exceptType] = *savedExcState;
    loadContext(&supportStruct->sup_exceptContext[exceptType]);
  }
}

void generalExceptionHandler() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  unsigned int excCode = CAUSE_EXCCODE(savedExcState->s_cause);

  if (excCode == 0) {
    /* Interrupt exception: call our interrupt handler (section 3.6) */
    interruptHandler();
  } else if (excCode >= 1 && excCode <= 3) {
    /* TLB exception */
    passUpOrDie(savedExcState, PGFAULTEXCEPT);
  } else if ((excCode >= 4 && excCode <= 7) ||
             (excCode >= 9 && excCode <= 12)) {
    /* Program trap exception */
    passUpOrDie(savedExcState, GENERALEXCEPT);
  } else if (excCode == 8) {
    syscallHandler(savedExcState);
  } else {
    /* Unknown exception code */
    PANIC();
  }
}
