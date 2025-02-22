
#include "initial.h"
#include "pcb.h"
#include "scheduler.h"
#include "umps3/umps/libumps.h"

int sysCreateProc() {
  pcb_PTR p = allocPcb();

  if (p == NULL) {
    /* No more free pcb's */
    return -1;
  }

  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  state_t *statep = (state_t *)savedExcState->s_a1;
  support_t *supportp = (support_t *)savedExcState->s_a2;

  p->p_s = *statep;
  p->p_time = 0;
  p->p_semAdd = NULL;
  p->p_supportStruct = supportp;
  insertProcQ(&readyQueue, p);
  insertChild(currentProc, p);
  procCnt++;

  return 0;
}

void terminateProcHelper(pcb_PTR p) {
  if (p == NULL) {
    return;
  }

  pcb_PTR child;
  while ((child = removeChild(p))) {
    terminateProcHelper(child);
  }

  outProcQ(&readyQueue, p);
  outBlocked(p);
  freePcb(p);
  procCnt--;
}

int sysTerminateProc() {
  terminateProcHelper(currentProc);
  currentProc = NULL;
  /* Call the scheduler to switch to another process without pushing the current
   * process to the queue again */
  scheduler();

  return 0;
}


/* SYS3 – Passeren (P operation on a semaphore) */
int sysPasseren() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  int *sem = (int *)savedExcState->s_a1;
  (*sem)--;
  if (*sem < 0) {
    /* TODO: "Update the accumulated CPU time for the Current Process" in section "3.5.11 Blocking SYSCALLs" in pandos.pdf ? */
    currentProc->p_s = *savedExcState;
    insertBlocked(sem, currentProc);
    scheduler();
  }
  return 0;
}

/* SYS4 – Verhogen (V operation on a semaphore) */
int sysVerhogen() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  int *sem = (int *) savedExcState->s_a1;
  (*sem)++;
  if (*sem <= 0) {
    /* Unblock one process waiting on this semaphore, if any */
    pcb_PTR p = removeBlocked(sem);
    if (p != NULL) {
      insertProcQ(&readyQueue, p);
    }
  }

  return 0;
}

/* SYS5 – Wait for IO Device (perform P on the appropriate device semaphore) */
int sysWaitIO() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  /* Interrupt line number */
  int line = savedExcState->s_a1;
  /* Device number */
  int devnum = savedExcState->s_a2;
  /* TRUE if waiting for terminal read */
  int waitForTermRead = savedExcState->s_a3;

  int semIndex;
  if (line != TERMINT) {
    /* For non-terminal devices, index = (line - 3 being offset)*DEVPERINT + devnum */
    semIndex = (line - 3) * DEVPERINT + devnum; 
  } else {
    /* For terminal devices, there are two semaphores per device. If waiting for a terminal read, use the receive semaphore; otherwise, the transmit semaphore. Terminals are indexed starting at 32 (since non-terminals take 32 entries) */
    semIndex = 32 + devnum * 2 + (waitForTermRead ? 0 : 1);
  }
  int semaddr = &deviceSem[semIndex];
  (*semaddr)--;
  if (*semaddr < 0) {
    /* TODO: "Update the accumulated CPU time for the Current Process" in section "3.5.11 Blocking SYSCALLs" in pandos.pdf ? */
    currentProc->p_s = *savedExcState;
    insertBlocked(semaddr, currentProc);
    scheduler();
  }
  return 0;
}


/* SYS6 – Get CPU Time (return the accumulated CPU time of the current process) */
int sysGetCPUTime() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  return currentProc->p_time;
}

/* SYS7 – Wait For Clock (perform P on the pseudo-clock semaphore) */
int sysWaitForClock() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  int *sem = &deviceSem[NUMDEVICES];  /* Pseudo–clock semaphore  */
  (*sem)--;
  if (*sem < 0) {
    /* TODO: "Update the accumulated CPU time for the Current Process" in section "3.5.11 Blocking SYSCALLs" in pandos.pdf ? */
    currentProc->p_s = *savedExcState;
    softBlockCnt++;  /* Clock waits are soft–blocked */
    insertBlocked(sem, currentProc);
    scheduler();
  }
  return 0;
}


/* SYS8 – Get Support Data (return pointer to the current process's support structure) */
int sysGetSupportData() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  return (int)currentProc->p_supportStruct;
}

/* Define the function pointer type for syscalls */
typedef int (*syscall_t)(void);

/* Declare the syscall table, mapping syscall numbers (1-8) to functions */
static syscall_t syscalls[] = {
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

void syscallHandler() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  int num = savedExcState->s_a0;

  if (num >= 1 && num <= 8) {
    savedExcState->s_v0 = syscalls[num]();
  } else {
    /* TODO:
     * - behavior for unknown system call?
     * - PANIC?
     */
    savedExcState->s_v0 = -1;
  }
}

void programTrapHandler() {}

void generalExceptionHandler() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  unsigned int excCode = CAUSE_EXCCODE(savedExcState->s_cause);

  if (excCode == 0) {
    /* TODO: Call devide interrupt handler (section 3.6) */
  } else if (excCode >= 1 && excCode <= 3) {
    /* TODO: Call TLB exception handler (section 3.7.3) */
  } else if ((excCode >= 4 && excCode <= 7) ||
             (excCode >= 9 && excCode <= 12)) {
    /* TODO: Call Program Trap exception handler (section 3.7.2) */
  } else if (excCode == 8) {
    if (savedExcState->s_status & STATUS_KUP) {
      savedExcState->s_pc += 4;
      /* Currently, the OS only supports SYS1-SYS8, which are only available to
       * processes executing in kernel-mode */
      syscallHandler();
    } else {
      /* TODO: set Cause.ExcCode to RI here */
      programTrapHandler();
    }
  } else {
    /* Unknown exception code */
    PANIC();
  }

  /*
   * Note: to determine if the current process was executing in kernel-mode
   * or user-mode, examine the KU bit of the Status register.
   */
}
