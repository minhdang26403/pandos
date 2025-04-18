/**
 * @file sysSupport.c
 * @author Dang Truong, Loc Pham
 * @brief Implements the Support Level's exception handling services, including
 * the general exception handler (Section 4.6), the SYSCALL exception handler
 * (Section 4.7), and the Program Trap exception handler (Section 4.8). These
 * handlers dispatch system calls and manage process termination and I/O
 * operations for U-procs.
 * @date 2025-04-17
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/sysSupport.h"

#include "../h/const.h"
#include "../h/deviceSupportDMA.h"
#include "../h/initProc.h"
#include "../h/initial.h"
#include "../h/scheduler.h"
#include "../h/supportAlloc.h"
#include "../h/types.h"
#include "../h/vmSupport.h"
#include "umps3/umps/libumps.h"

HIDDEN void syscallHandler(support_t *sup);
void programTrapHandler(support_t *sup);

/**
 * @brief Support Level's general exception dispatcher.
 *
 * Retrieves the current U-proc's support structure and inspects the exception
 * code. Dispatches to:
 * - `syscallHandler()` if the exception is a SYSCALL.
 * - `programTrapHandler()` for all other causes (including TLB modifications).
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

/**
 * @brief Terminate the current U-proc and release all its resources.
 *
 * - Acquires the swap pool mutex.
 * - Frees all physical frames allocated to the process (based on ASID).
 * - Signals the test process via the master semaphore.
 * - Returns the support structure to the free list.
 * - Invokes TERMINATEPROCESS syscall to kill the process.
 *
 * @param sup Pointer to the support structure of the U-proc to be terminated.
 */
HIDDEN void sysTerminate(support_t *sup) {
  /* Free frames occupied by this U-proc */
  releaseFrames(sup->sup_asid);

  /* Signal termination to test */
  SYSCALL(VERHOGEN, (int)&masterSemaphore, 0, 0);

  /* Return the support structure to the free list */
  supportDeallocate(sup);

  /* Terminate the process */
  SYSCALL(TERMINATEPROCESS, 0, 0, 0);
}

/**
 * @brief Implement GETTOD syscall for U-procs.
 *
 * Stores the current system time (microseconds) in `s_v0` of the exception
 * state. Then restores context to resume execution.
 *
 * @param excState Pointer to the saved exception state of the current U-proc.
 */
HIDDEN void sysGetTOD(state_t *excState) {
  STCK(excState->s_v0);
  switchContext(excState);
}

/**
 * @brief Implement WRITEPRINTER syscall for U-procs.
 *
 * - Validates that the string (s_a1, length s_a2) lies entirely within KUSEG.
 * - Sends each character to the printer device and waits for acknowledgment.
 * - Sets `s_v0` to the number of characters printed or a negative error code.
 *
 * @param excState Pointer to the saved exception state of the current U-proc.
 * @param sup Pointer to the support structure of the current U-proc.
 */
HIDDEN void sysWriteToPrinter(state_t *excState, support_t *sup) {
  memaddr virtAddr = excState->s_a1;
  /* if len < 0, then len will be a very large number due to overflow */
  unsigned int len = excState->s_a2;
  int devNum = sup->sup_asid - 1; /* ASID 1-8 -> 0-7 */
  int devIdx = (PRNTINT - DISKINT) * DEVPERINT + devNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *printer = &busRegArea->devreg[devIdx];

  /* Validate inputs: entire string must be in KUSEG
   * Note that if (virtAddr + len - 1) >= MAXADDR, then the number will be
   * wrapped around and smaller than KUSEG */
  if (!isValidAddr(virtAddr) || len > PRINTER_MAXLEN ||
      !isValidAddr(virtAddr + len - 1)) {
    programTrapHandler(sup);
  }

  SYSCALL(PASSEREN, (int)&supportDeviceSem[devIdx], 0, 0);

  /* Send each character up to len */
  char *s = (char *)virtAddr;
  int devStatus = READY; /* Default for len = 0 */
  unsigned i = 0;
  while (i < len && devStatus == READY) {
    /* Prepare data */
    printer->d_data0 = s[i];

    /* Disable interrupts to ensure writing the COMMAND field and executing SYS5
     * (WAITIO) happens atomically */
    unsigned int procStatus = getSTATUS();
    setSTATUS(procStatus & ~STATUS_IEC);
    printer->d_command = PRINTCHR;
    devStatus = SYSCALL(WAITIO, PRNTINT, devNum, 0);
    setSTATUS(procStatus); /* Reenable interrupts */

    i++;
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

/**
 * @brief Implement WRITETERMINAL syscall for U-procs.
 *
 * - Validates the input string buffer in KUSEG.
 * - Sends each character to the terminal transmitter.
 * - Waits for acknowledgment per character.
 * - Returns number of characters written or negative error code in `s_v0`.
 *
 * @param excState Pointer to the saved exception state of the current U-proc.
 * @param sup Pointer to the support structure of the current U-proc.
 */
HIDDEN void sysWriteToTerminal(state_t *excState, support_t *sup) {
  memaddr virtAddr = excState->s_a1;
  unsigned len = excState->s_a2;
  int devNum = sup->sup_asid - 1; /* ASID 1-8 -> 0-7 */
  int devIdx = (TERMINT - DISKINT) * DEVPERINT + devNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *terminal = &busRegArea->devreg[devIdx];

  /* Validate inputs: entire string must be in KUSEG */
  if (!isValidAddr(virtAddr) || len > TERMINAL_MAXLEN ||
      !isValidAddr(virtAddr + len - 1)) {
    programTrapHandler(sup);
  }

  SYSCALL(PASSEREN, (int)&supportDeviceSem[devIdx], 0, 0);

  /* Send each character up to len */
  char *s = (char *)virtAddr;
  int devStatus = CHAR_TRANSMITTED; /* Default for len = 0 */
  unsigned i = 0;
  while (i < len && (devStatus & TERMINT_STATUS_MASK) == CHAR_TRANSMITTED) {
    /* Disable interrupts to ensure writing the COMMAND field and executing SYS5
     * (WAITIO) happens atomically */
    unsigned int procStatus = getSTATUS();
    setSTATUS(procStatus & ~STATUS_IEC);
    terminal->t_transm_command = TRANSMITCHAR | (s[i] << BYTELEN);
    devStatus = SYSCALL(WAITIO, TERMINT, devNum, FALSE);
    setSTATUS(procStatus); /* Reenable interrupts */

    i++;
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

/**
 * @brief Implement READTERMINAL syscall for U-procs.
 *
 * - Repeatedly reads characters from the terminal receiver until a newline  or
 * error occurs.
 * - Validates that each character is written to a valid KUSEG address.
 * - Returns the number of characters read or a negative error code in `s_v0`.
 *
 * @param excState Pointer to the saved exception state of the current U-proc.
 * @param sup Pointer to the support structure of the current U-proc.
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
  int done = FALSE;

  while (!done) {
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
        done = TRUE;
      }
    } else {
      done = TRUE;
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

/**
 * @brief Dispatch SYSCALL exceptions (syscalls 9–13 and I/O) for U-procs.
 *
 * - Increments the program counter to skip the SYSCALL instruction.
 * - Handles system calls: TERMINATE, GETTOD, WRITEPRINTER, WRITETERMINAL,
 *   READTERMINAL, DISKREAD/WRITE, FLASHREAD/WRITE.
 * - If the syscall number is outside the valid range, invokes program trap
 * handler.
 *
 * @param sup Pointer to the support structure of the current U-proc.
 */
HIDDEN void syscallHandler(support_t *sup) {
  state_t *excState = &sup->sup_exceptState[GENERALEXCEPT];
  int syscallNum = excState->s_a0;

  if (syscallNum >= TERMINATE && syscallNum <= FLASHREAD) {
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
      case DISKWRITE:
        sysDiskWrite(excState, sup);
      case DISKREAD:
        sysDiskRead(excState, sup);
      case FLASHWRITE:
        sysFlashWrite(excState, sup);
      case FLASHREAD:
        sysFlashRead(excState, sup);
      default:
        break;
    }
  } else {
    programTrapHandler(sup);
  }
}

/**
 * @brief Handle all program traps by terminating the current U-proc.
 *
 * This includes illegal memory access, invalid syscalls, or failed validations.
 *
 * @param sup Pointer to the support structure of the current U-proc.
 */
void programTrapHandler(support_t *sup) { sysTerminate(sup); }
