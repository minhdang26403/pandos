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
#include "../h/types.h"
#include "../h/vmSupport.h"
#include "umps3/umps/libumps.h"

HIDDEN void syscallHandler(support_t *sup);
HIDDEN void programTrapHandler(support_t *sup);

/* Support Level General Exception Handler */
void supportExceptionHandler() {
  support_t *sup = (support_t *)SYSCALL(GETSUPPORTPTR, 0, 0, 0);

  /* Use PGFAULTEXCEPT for TLB traps, GENERALEXCEPT for others */
  int causeIdx =
      (CAUSE_EXCCODE(getCAUSE()) == EXC_TLBMOD) ? PGFAULTEXCEPT : GENERALEXCEPT;
  state_t *excState = &sup->sup_exceptState[causeIdx];
  unsigned int cause = CAUSE_EXCCODE(excState->s_cause);

  if (cause == EXC_SYSCALL) {
    int syscallNum = excState->s_a0;
    if (syscallNum >= 9) {
      syscallHandler(sup);
    } else {
      programTrapHandler(sup);
    }
  } else {
    /* All non-SYSCALL causes (including EXC_TLBMOD) are traps */
    programTrapHandler(sup);
  }
}

/* SYSCALL Exception Handler (Section 4.7) - Placeholder */
HIDDEN void syscallHandler(support_t *sup) {
  /* TODO: Section 4.7 - SYS9+ */
  state_t *excState = &sup->sup_exceptState[GENERALEXCEPT];
  excState->s_pc += WORDLEN;
  switchContext(excState);
}

/* Program Trap Exception Handler */
HIDDEN void programTrapHandler(support_t *sup) {
  /* Release Swap Pool semaphore if held */
  if (swapPoolSem == 0) { /* Assume process holds it if 0 */
    SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
  }

  /* Terminate process like SYS9 (Section 4.7.1 TBD) */
  SYSCALL(TERMINATEPROCESS, 0, 0, 0);
}
