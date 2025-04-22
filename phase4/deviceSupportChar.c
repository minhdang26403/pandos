/**
 * @file deviceSupportChar.c
 * @author Dang Truong, Loc Pham
 * @brief Implements character-device syscalls (terminals & printers):
 *        SYS11-SYS13 (WRITEPRINTER, WRITETERMINAL, READTERMINAL).
 * @date 2025-04-21
 */


#include "../h/deviceSupportChar.h"

#include "../h/initProc.h"
#include "../h/scheduler.h"
#include "../h/sysSupport.h"
#include "../h/vmSupport.h"
#include "umps3/umps/libumps.h"

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
void sysWriteToPrinter(state_t *excState, support_t *sup) {
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
  int result = READY; /* Default for len = 0 */
  unsigned i = 0;
  while (i < len && result == READY) {
    /* Prepare data */
    printer->d_data0 = s[i];

    /* Disable interrupts to ensure writing the COMMAND field and executing SYS5
     * (WAITIO) happens atomically */
    unsigned int status = getSTATUS();
    setSTATUS(status & ~STATUS_IEC);
    printer->d_command = PRINTCHR;
    result = SYSCALL(WAITIO, PRNTINT, devNum, 0);
    setSTATUS(status); /* Reenable interrupts */

    i++;
  }

  if (result == READY) {
    /* Success: all chars sent */
    excState->s_v0 = len;
  } else {
    /* Error: negative status */
    excState->s_v0 = -result;
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
void sysWriteToTerminal(state_t *excState, support_t *sup) {
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
  int result = CHAR_TRANSMITTED; /* Default for len = 0 */
  unsigned i = 0;
  while (i < len && (result & TERMINT_STATUS_MASK) == CHAR_TRANSMITTED) {
    /* Disable interrupts to ensure writing the COMMAND field and executing SYS5
     * (WAITIO) happens atomically */
    unsigned int status = getSTATUS();
    setSTATUS(status & ~STATUS_IEC);
    terminal->t_transm_command = TRANSMITCHAR | (s[i] << BYTELEN);
    result = SYSCALL(WAITIO, TERMINT, devNum, FALSE);
    setSTATUS(status); /* Reenable interrupts */

    i++;
  }

  if ((result & TERMINT_STATUS_MASK) == CHAR_TRANSMITTED) {
    /* Success: all chars sent */
    excState->s_v0 = len;
  } else {
    /* Error: negative status */
    excState->s_v0 = -result;
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
void sysReadFromTerminal(state_t *excState, support_t *sup) {
  memaddr virtAddr = excState->s_a1;
  int devNum = sup->sup_asid - 1; /* ASID 1-8 -> 0-7 */
  int devIdx = (TERMINT - DISKINT) * DEVPERINT + devNum;
  int semIdx = devIdx + DEVPERINT;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *terminal = &busRegArea->devreg[devIdx];

  SYSCALL(PASSEREN, (int)&supportDeviceSem[semIdx], 0, 0);

  /* Read each character up to len */
  char *buffer = (char *)virtAddr;
  int result;
  int done = FALSE;

  while (!done) {
    /* Disable interrupts to ensure writing the COMMAND field and executing SYS5
     * (WAITIO) happens atomically */
    unsigned int status = getSTATUS();
    setSTATUS(status & ~STATUS_IEC);
    terminal->t_recv_command = RECEIVECHAR;
    result = SYSCALL(WAITIO, TERMINT, devNum, TRUE);
    setSTATUS(status); /* Reenable interrupts */

    if ((result & TERMINT_STATUS_MASK) == CHAR_RECEIVED) {
      char c = (result >> BYTELEN) & TERMINT_STATUS_MASK;
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

  if ((result & TERMINT_STATUS_MASK) == CHAR_RECEIVED) {
    /* Success: all chars read */
    excState->s_v0 = buffer - (char *)virtAddr;
  } else {
    /* Error: negative status */
    excState->s_v0 = -result;
  }

  SYSCALL(VERHOGEN, (int)&supportDeviceSem[semIdx], 0, 0);
  switchContext(excState);
}
