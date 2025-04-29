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

#include "../h/alsl.h"
#include "../h/const.h"
#include "../h/delayDaemon.h"
#include "../h/deviceSupportChar.h"
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

extern void debug(int, int, int, int);

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

  if (syscallNum >= TERMINATE && syscallNum <= VSEMLOGICAL) {
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
      case DELAY:
        sysDelay(excState, sup);
      case PSEMLOGICAL:
        sysPasserenLogicalSem(excState, sup);
        debug(0, 0, 0, 0);
      case VSEMLOGICAL:
        sysVerhogenLogicalSem(excState, sup);
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
