/************************** SYSSUPPORT.C ******************************
 *
 * This module implements the Support Level's:
 *  - general exception handler (section 4.6)
 *  - SYSCALL exception handler (section 4.7)
 *  - Program Trap exception handler (section 4.8)
 *
 * Written by Dang Truong
 */

/***************************************************************/

#include "../h/sysSupport.h"

#include "../h/const.h"
#include "../h/initial.h"
#include "../h/types.h"
#include "../h/vmSupport.h"
#include "umps3/umps/libumps.h"

HIDDEN void syscallHandler(support_t *sup);
HIDDEN void programTrapHandler(support_t *sup);

/* Validate virtual address is in U-proc's logical space (KUSEG) */
HIDDEN int isValidAddr(support_t *sup, memaddr addr) {
  return (addr >= KUSEG && addr < (KUSEG + (MAXPAGES * PAGESIZE)));
}

/* Support Level General Exception Handler */
void supportExceptionHandler() {
  support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  /* Use PGFAULTEXCEPT for TLB traps, GENERALEXCEPT for others */
  int causeIdx =
      (CAUSE_EXCCODE(getCAUSE()) == EXC_TLBMOD) ? PGFAULTEXCEPT : GENERALEXCEPT;
  state_t *excState = &sup->sup_exceptState[causeIdx];
  unsigned int cause = CAUSE_EXCCODE(excState->s_cause);

  if (cause == EXC_SYSCALL) {
    syscallHandler(sup);
  } else {
    /* All non-SYSCALL causes (including EXC_TLBMOD) are traps */
    programTrapHandler(sup);
  }
}

HIDDEN void sysTerminate() { SYSCALL(TERMINATEPROCESS, 0, 0, 0); }

HIDDEN void sysGetTOD(state_t *excState) {
  STCK(excState->s_v0);
  switchContext(excState);
}

HIDDEN void sysWriteToPrinter(state_t *excState, support_t *sup) {
  memaddr virtAddr = excState->s_a1;
  int len = excState->s_a2;
  int devNum = sup->sup_asid - 1; /* ASID 1-8 -> 0-7 */
  int devIdx = (PRNTINT - DISKINT) * DEVPERINT + devNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *printer = &busRegArea->devreg[devIdx];

  /* Validate inputs */
  if (!isValidAddr(sup, virtAddr) || len < 0 || len > PRINTER_MAXLEN) {
    programTrapHandler(sup);
  }

  SYSCALL(PASSEREN, (int)&deviceSem[devIdx], 0, 0);

  /* Send each character up to len */
  char *s = (char *)virtAddr;
  int i;
  for (i = 0; i < len; i++) {
    printer->d_data0 = s[i]; /* Low byte gets char */
    printer->d_command = PRINTCHR;

    int status = SYSCALL(WAITIO, PRNTINT, devNum, 0);
    if (status == READY) {
      printer->d_command = ACK; /* Acknowledge interrupt */
    } else {
      excState->s_v0 = -status; /* Error: negative status */
      break;
    }
  }

  if (i == len) {
    /* Success: all chars sent */
    excState->s_v0 = len;
  }
  SYSCALL(VERHOGEN, (int)&deviceSem[devIdx], 0, 0);
  switchContext(excState);
}

HIDDEN void sysWriteToTerminal(state_t *excState, support_t *sup) {}

HIDDEN void sysReadFromTerminal(state_t *excState, support_t *sup) {}

/* SYSCALL Exception Handler (Section 4.7) - Placeholder */
HIDDEN void syscallHandler(support_t *sup) {
  /* TODO: Section 4.7 - SYS9+ */
  state_t *excState = &sup->sup_exceptState[GENERALEXCEPT];
  int syscallNum = excState->s_a0;

  if (syscallNum >= 9 && syscallNum <= 13) {
    excState->s_pc += WORDLEN; /* control of the current process should be
                                  returned to the next instruction */
    switch (syscallNum) {
      case TERMINATE:
        sysTerminate();
        break;
      case GETTOD:
        sysGetTOD(excState);
        break;
      case WRITEPRINTER:
        sysWriteToPrinter(excState, sup);
        break;
      case WRITETERMINAL:
        sysWriteToTerminal(excState, sup);
        break;
      case READTERMINAL:
        sysReadFromTerminal(excState, sup);
        break;
      default:
        break;
    }
  } else {
    programTrapHandler(sup);
  }
}

/* Program Trap Exception Handler */
HIDDEN void programTrapHandler(support_t *sup) {
  /* Release Swap Pool semaphore if held */
  if (swapPoolSem == 0) {
    SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
  }

  SYSCALL(TERMINATEPROCESS, 0, 0, 0);
}
