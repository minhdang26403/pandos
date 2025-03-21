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
  unsigned int cause = getCAUSE();
  unsigned int excCode = CAUSE_EXCCODE(cause);
  int causeIdx = (excCode == EXC_TLBMOD) ? PGFAULTEXCEPT : GENERALEXCEPT;
  state_t *excState = &sup->sup_exceptState[causeIdx];

  if (excCode == EXC_SYSCALL) {
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

  /* Validate inputs: entire string must be in KUSEG */
  if (!isValidAddr(sup, virtAddr) || len < 0 || len > PRINTER_MAXLEN ||
      (len > 0 && !isValidAddr(sup, virtAddr + len - 1))) {
    programTrapHandler(sup);
  }

  SYSCALL(PASSEREN, (int)&deviceSem[devIdx], 0, 0);

  /* Send each character up to len */
  char *s = (char *)virtAddr;
  int status = READY; /* Default for len = 0 */
  int i;
  for (i = 0; i < len; i++) {
    printer->d_data0 = s[i]; /* Low byte gets char */
    printer->d_command = PRINTCHR;

    status = SYSCALL(WAITIO, PRNTINT, devNum, 0);
    if (status != READY) {
      break;
    }
  }

  if (status == READY) {
    /* Success: all chars sent */
    excState->s_v0 = len;
  } else {
    /* Error: negative status */
    excState->s_v0 = -status;
  }

  SYSCALL(VERHOGEN, (int)&deviceSem[devIdx], 0, 0);
  switchContext(excState);
}

HIDDEN void sysWriteToTerminal(state_t *excState, support_t *sup) {
  memaddr virtAddr = excState->s_a1;
  int len = excState->s_a2;
  int devNum = sup->sup_asid - 1; /* ASID 1-8 -> 0-7 */
  int devIdx = (TERMINT - DISKINT) * DEVPERINT + devNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *terminal = &busRegArea->devreg[devIdx];

  /* Validate inputs: entire string must be in KUSEG */
  if (!isValidAddr(sup, virtAddr) || len < 0 || len > TERMINAL_MAXLEN ||
      (len > 0 && !isValidAddr(sup, virtAddr + len - 1))) {
    programTrapHandler(sup);
  }

  SYSCALL(PASSEREN, (int)&deviceSem[devIdx], 0, 0);

  /* Send each character up to len */
  char *s = (char *)virtAddr;
  int status = CHAR_TRANSMITTED; /* Default for len = 0 */
  int i;
  for (i = 0; i < len; i++) {
    terminal->t_transm_command = TRANSMITCHAR | (s[i] << BYTELEN);
    status = SYSCALL(WAITIO, TERMINT, devNum, FALSE);
    if ((status & TERMINT_STATUS_MASK) != CHAR_TRANSMITTED) {
      break;
    }
  }

  if ((status & TERMINT_STATUS_MASK) == CHAR_TRANSMITTED) {
    /* Success: all chars sent */
    excState->s_v0 = len;
  } else {
    /* Error: negative status */
    excState->s_v0 = -status;
  }

  SYSCALL(VERHOGEN, (int)&deviceSem[devIdx], 0, 0);
  switchContext(excState);
}

HIDDEN void sysReadFromTerminal(state_t *excState, support_t *sup) {
  memaddr virtAddr = excState->s_a1;
  int devNum = sup->sup_asid - 1; /* ASID 1-8 -> 0-7 */
  int devIdx = (TERMINT - DISKINT) * DEVPERINT + devNum + DEVPERINT;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *terminal = &busRegArea->devreg[devIdx];

  SYSCALL(PASSEREN, (int)&deviceSem[devIdx], 0, 0);

  /* Read each character up to len */
  char *buffer = (char *)virtAddr;
  int status;

  while (TRUE) {
    terminal->t_recv_command = RECEIVECHAR;
    status = SYSCALL(WAITIO, TERMINT, devNum, TRUE);
    if ((status & TERMINT_STATUS_MASK) == CHAR_RECEIVED) {
      char c = (status >> BYTELEN) & TERMINT_STATUS_MASK;
      /* Validate each buffer address before writing */
      if (!isValidAddr(sup, (memaddr)buffer)) {
        programTrapHandler(sup); /* Buffer overflow */
      }
      *buffer = c;
      buffer++;
      if (c == '\n') {
        break;
      }
    } else {
      break;
    }
  }

  if ((status & TERMINT_STATUS_MASK) == CHAR_RECEIVED) {
    /* Success: all chars read */
    excState->s_v0 = buffer - (char *)virtAddr;
  } else {
    /* Error: negative status */
    excState->s_v0 = -status;
  }

  SYSCALL(VERHOGEN, (int)&deviceSem[devIdx], 0, 0);
  switchContext(excState);
}

/* System Call Exception Handler */
HIDDEN void syscallHandler(support_t *sup) {
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
