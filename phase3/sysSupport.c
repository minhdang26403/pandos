/************************** SYSSUPPORT.C ******************************
 *
 * Purpose: Implements the Support Level's exception handling services,
 *          including the general exception handler (Section 4.6), the SYSCALL
 *          exception handler (Section 4.7), and the Program Trap exception
 *          handler (Section 4.8). These handlers dispatch system calls and
 *          manage process termination and I/O operations for U-procs.
 *
 * Written by Dang Truong, Loc Pham
 *
 ***************************************************************/

#include "../h/sysSupport.h"

#include "../h/const.h"
#include "../h/initProc.h"
#include "../h/initial.h"
#include "../h/scheduler.h"
#include "../h/types.h"
#include "../h/vmSupport.h"
#include "../h/supportAlloc.h"
#include "umps3/umps/libumps.h"

HIDDEN void syscallHandler(support_t *sup);
void programTrapHandler(support_t *sup);

/*
 * Function: isValidAddr
 * Purpose: Validate that a given memory address is within the U-proc's logical
 *          address space (KUSEG). Returns non-zero if valid; zero otherwise.
 * Parameters:
 *    - addr: The memory address to validate.
 */
HIDDEN int isValidAddr(memaddr addr) {
  return addr >= KUSEG && addr <= MAXADDR;
}

/*
 * Function: supportExceptionHandler
 * Purpose: Acts as the Support Level's General Exception Handler. It retrieves the
 *          current support structure, examines the exception cause, and dispatches
 *          the exception to the appropriate handler. SYSCALL exceptions are handled
 *          by syscallHandler, while all other exceptions (including TLB modifications)
 *          are treated as program traps.
 * Parameters: None.
 */
void supportExceptionHandler() {
  support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);
  unsigned int excCode =
      CAUSE_EXCCODE(sup->sup_exceptState[GENERALEXCEPT].s_cause);

  if (excCode == EXC_SYSCALL) {
    syscallHandler(sup);
  } else {
    /* All non-SYSCALL causes (including EXC_TLBMOD) are traps */
    programTrapHandler(sup);
  }
}

/*
 * Function: sysTerminate
 * Purpose: Terminates the current U-proc by releasing its resources. This includes
 *          obtaining mutual exclusion on the Swap Pool, freeing frames allocated to
 *          the process, signaling termination to the instantiator (test process), and
 *          returning the support structure to the free list before finally invoking the
 *          TERMINATEPROCESS syscall.
 * Parameters:
 *    - sup: Pointer to the support structure of the U-proc to be terminated.
 */
HIDDEN void sysTerminate(support_t *sup) {
  /* Gain a mutual exclusion on Swap Pool Table */
  SYSCALL(PASSEREN, (int)&swapPoolSem, 0, 0);

  /* Free frames occupied by this U-proc */
  int asid = sup->sup_asid;
  int i;
  for (i = 0; i < SWAP_POOL_SIZE; i++) {
    if (swapPoolTable[i].spte_asid == asid) {
      swapPoolTable[i].spte_asid = -1;
      swapPoolTable[i].spte_vpn = 0;
      swapPoolTable[i].spte_pte = NULL;
    }
  }

  SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);

  /* Signal termination to test */
  SYSCALL(VERHOGEN, (int)&masterSemaphore, 0, 0);

  /* Return the support structure to the free list */
  supportDeallocate(sup);

  /* Terminate the process */
  SYSCALL(TERMINATEPROCESS, 0, 0, 0);
}

/*
 * Function: sysGetTOD
 * Purpose: Implements the GETTOD service by storing the system's time-of-day in the
 *          v0 register of the exception state and switching context back to the U-proc.
 * Parameters:
 *    - excState: Pointer to the state_t structure (exception state) for the current U-proc.
 */
HIDDEN void sysGetTOD(state_t *excState) {
  STCK(excState->s_v0);
  switchContext(excState);
}

/*
 * Function: sysWriteToPrinter
 * Purpose: Implements the WRITEPRINTER service. This function validates that the entire
 *          string to be printed lies within the U-proc's logical address space, then sends
 *          each character to the printer device. The function waits for the printer to be
 *          ready and returns the number of characters transmitted, or a negative error code.
 * Parameters:
 *    - excState: Pointer to the state_t structure containing the exception state for the U-proc.
 *    - sup:      Pointer to the support structure for the U-proc.
 */
HIDDEN void sysWriteToPrinter(state_t *excState, support_t *sup) {
  memaddr virtAddr = excState->s_a1;
  int len = excState->s_a2;
  int devNum = sup->sup_asid - 1; /* ASID 1-8 -> 0-7 */
  int devIdx = (PRNTINT - DISKINT) * DEVPERINT + devNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *printer = &busRegArea->devreg[devIdx];

  /* Validate inputs: entire string must be in KUSEG */
  if (!isValidAddr(virtAddr) || len < 0 || len > PRINTER_MAXLEN ||
      (len > 0 && !isValidAddr(virtAddr + len - 1))) {
    programTrapHandler(sup);
  }

  SYSCALL(PASSEREN, (int)&supportDeviceSem[devIdx], 0, 0);

  /* Send each character up to len */
  char *s = (char *)virtAddr;
  int devStatus = READY; /* Default for len = 0 */
  int i;
  for (i = 0; i < len; i++) {
    /* Prepare data */
    printer->d_data0 = s[i];

    /* Disable interrupts to ensure writing the COMMAND field and executing SYS5
     * (WAITIO) happens atomically */
    unsigned int procStatus = getSTATUS();
    setSTATUS(procStatus & ~STATUS_IEC);
    printer->d_command = PRINTCHR;
    devStatus = SYSCALL(WAITIO, PRNTINT, devNum, 0);
    setSTATUS(procStatus); /* Reenable interrupts */

    if (devStatus != READY) {
      break;
    }
  }

  if (devStatus == READY) {
    /* Success: all chars sent */
    excState->s_v0 = len;
  } else {
    /* Error: negative status */
    excState->s_v0 = -devStatus;
  }

  SYSCALL(VERHOGEN, (int)&supportDeviceSem[devIdx], 0, 0);
  switchContext(excState);
}

/*
 * Function: sysWriteToTerminal
 * Purpose: Implements the WRITETERMINAL service. This function validates that the string to be
 *          written is entirely within the U-proc's logical space, sends each character to the
 *          terminal device, and returns the number of characters transmitted or an error code.
 * Parameters:
 *    - excState: Pointer to the state_t structure containing the U-proc's exception state.
 *    - sup:      Pointer to the support structure for the U-proc.
 */
HIDDEN void sysWriteToTerminal(state_t *excState, support_t *sup) {
  memaddr virtAddr = excState->s_a1;
  int len = excState->s_a2;
  int devNum = sup->sup_asid - 1; /* ASID 1-8 -> 0-7 */
  int devIdx = (TERMINT - DISKINT) * DEVPERINT + devNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *terminal = &busRegArea->devreg[devIdx];

  /* Validate inputs: entire string must be in KUSEG */
  if (!isValidAddr(virtAddr) || len < 0 || len > TERMINAL_MAXLEN ||
      (len > 0 && !isValidAddr(virtAddr + len - 1))) {
    programTrapHandler(sup);
  }

  SYSCALL(PASSEREN, (int)&supportDeviceSem[devIdx], 0, 0);

  /* Send each character up to len */
  char *s = (char *)virtAddr;
  int devStatus = CHAR_TRANSMITTED; /* Default for len = 0 */
  int i;
  for (i = 0; i < len; i++) {
    /* Disable interrupts to ensure writing the COMMAND field and executing SYS5
     * (WAITIO) happens atomically */
    unsigned int procStatus = getSTATUS();
    setSTATUS(procStatus & ~STATUS_IEC);
    terminal->t_transm_command = TRANSMITCHAR | (s[i] << BYTELEN);
    devStatus = SYSCALL(WAITIO, TERMINT, devNum, FALSE);
    setSTATUS(procStatus); /* Reenable interrupts */

    if ((devStatus & TERMINT_STATUS_MASK) != CHAR_TRANSMITTED) {
      break;
    }
  }

  if ((devStatus & TERMINT_STATUS_MASK) == CHAR_TRANSMITTED) {
    /* Success: all chars sent */
    excState->s_v0 = len;
  } else {
    /* Error: negative status */
    excState->s_v0 = -devStatus;
  }

  SYSCALL(VERHOGEN, (int)&supportDeviceSem[devIdx], 0, 0);
  switchContext(excState);
}

/*
 * Function: sysReadFromTerminal
 * Purpose: Implements the READTERMINAL service. This function reads characters from the terminal
 *          device into a user buffer until a newline is encountered. It validates that the buffer
 *          addresses fall within KUSEG and returns the number of characters read or an error code.
 * Parameters:
 *    - excState: Pointer to the state_t structure containing the U-proc's exception state.
 *    - sup:      Pointer to the support structure for the U-proc.
 */
HIDDEN void sysReadFromTerminal(state_t *excState, support_t *sup) {
  memaddr virtAddr = excState->s_a1;
  int devNum = sup->sup_asid - 1; /* ASID 1-8 -> 0-7 */
  int devIdx = (TERMINT - DISKINT) * DEVPERINT + devNum;
  int semIdx = devIdx + DEVPERINT;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *terminal = &busRegArea->devreg[devIdx];

  SYSCALL(PASSEREN, (int)&supportDeviceSem[semIdx], 0, 0);

  /* Read each character up to len */
  char *buffer = (char *)virtAddr;
  int devStatus;

  while (TRUE) {
    /* Disable interrupts to ensure writing the COMMAND field and executing SYS5
     * (WAITIO) happens atomically */
    unsigned int procStatus = getSTATUS();
    setSTATUS(procStatus & ~STATUS_IEC);
    terminal->t_recv_command = RECEIVECHAR;
    devStatus = SYSCALL(WAITIO, TERMINT, devNum, TRUE);
    setSTATUS(procStatus); /* Reenable interrupts */

    if ((devStatus & TERMINT_STATUS_MASK) == CHAR_RECEIVED) {
      char c = (devStatus >> BYTELEN) & TERMINT_STATUS_MASK;
      /* Validate each buffer address before writing */
      if (!isValidAddr((memaddr)buffer)) {
        SYSCALL(VERHOGEN, (int)&supportDeviceSem[semIdx], 0, 0);
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

  if ((devStatus & TERMINT_STATUS_MASK) == CHAR_RECEIVED) {
    /* Success: all chars read */
    excState->s_v0 = buffer - (char *)virtAddr;
  } else {
    /* Error: negative status */
    excState->s_v0 = -devStatus;
  }

  SYSCALL(VERHOGEN, (int)&supportDeviceSem[semIdx], 0, 0);
  switchContext(excState);
}

/*
 * Function: syscallHandler
 * Purpose: Dispatches SYSCALL exceptions (for syscall numbers 9 through 13) for the Support
 *          Level. It increments the program counter to skip the SYSCALL instruction and then
 *          calls the appropriate service routine (TERMINATE, GETTOD, WRITEPRINTER, WRITETERMINAL,
 *          or READTERMINAL). If the syscall number is not within the valid range, it calls
 *          programTrapHandler.
 * Parameters:
 *    - sup: Pointer to the support structure for the current U-proc.
 */
HIDDEN void syscallHandler(support_t *sup) {
  state_t *excState = &sup->sup_exceptState[GENERALEXCEPT];
  int syscallNum = excState->s_a0;

  if (syscallNum >= 9 && syscallNum <= 13) {
    excState->s_pc += WORDLEN; /* control of the current process should be
                                  returned to the next instruction */
    switch (syscallNum) {
      case TERMINATE:
        sysTerminate(sup);
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

/*
 * Function: programTrapHandler
 * Purpose: Implements the Program Trap exception handler by invoking sysTerminate.
 *          This function handles any program traps or invalid operations by terminating the
 *          current U-proc.
 * Parameters:
 *    - sup: Pointer to the support structure for the U-proc encountering the trap.
 */
void programTrapHandler(support_t *sup) { sysTerminate(sup); }
