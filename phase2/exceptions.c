
#include "umps3/umps/libumps.h"
#include "../h/types.h"

#define EXCEPTION_CODE(cause)  (((cause) >> 2) & 0x1F)


void generalExceptionHandler() {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  unsigned int excCode = EXCEPTION_CODE(savedExcState->s_cause);

  if (excCode == 0) {
    /* TODO: Call devide interrupt handler (section 3.6) */
  } else if (excCode >= 1 && excCode <= 3) {
    /* TODO: Call TLB exception handler (section 3.7.3) */
  } else if ((excCode >= 4 && excCode <= 7) || (excCode >= 9 && excCode <= 12)) {
    /* TODO: Call Program Trap exception handler (section 3.7.2) */
  } else if (excCode == 8) {
    /* TODO: Call SYSCALL exception handler */
  } else {
    /* Unknown exception code */
    PANIC();
  }

  /*
   * Note: to determine if the current process was executing in kernel-mode 
   * or user-mode, examine the KU bit of the Status register.
   */
}
