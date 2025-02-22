
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

int sysPasseren() {}

int sysVerhogen() {}

int sysWaitIO() {}

int sysGetCPUTime() {}

int sysWaitForClock() {}

int sysGetSupportData() {}

/*Define the function pointer type for syscalls */
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
    /* Interrupt exception: call our interrupt handler (section 3.6) */
    interruptHandler();
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
