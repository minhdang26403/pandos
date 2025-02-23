#include "../h/initial.h"
#include "umps3/umps/libumps.h"

void switchContext(state_t *state) {
  /* LDST is a critical and dangerous instruction, so every call to context
   * switch should be performed through this function */
  LDST(state);
}

/* Centralized wrapper for LDCXT to switch to a new context */
void loadContext(context_t *context) {
  /* LDCXT is a critical kernel-mode operation that atomically updates SP,
   * Status, and PC */
  LDCXT(context->c_stackPtr, context->c_status, context->c_pc);
}

void scheduler() {
  pcb_PTR p = removeProcQ(&readyQueue);
  if (p == NULL) {
    /* The Ready Queue is empty */

    if (procCnt == 0) {
      /* Job well done */
      HALT();
    } else if (procCnt > 0 && softBlockCnt > 0) {
      /* Enable global interrupts and disable the PLT */
      unsigned int currentStatus = getSTATUS();
      currentStatus |= STATUS_IEC;
      currentStatus &= ~STATUS_TE;
      setSTATUS(currentStatus);
      /* Waiting for a device interrupt to occur */
      WAIT();
    } else if (procCnt > 0 && softBlockCnt == 0) {
      /* TODO: take an appropriate deadlock detected action? */
      /* Deadlock */
      PANIC();
    } else {
      /* Abnormal case should never happen! */
      PANIC();
    }
  }

  currentProc = p;
  setTIMER(QUANTUM); /* Each process gets a time slice of 5 miliseconds */
  switchContext(&p->p_s);
}
