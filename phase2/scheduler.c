#include "../h/initial.h"
#include "umps3/umps/libumps.h"


void scheduler() {
  pcb_PTR p = removeProcQ(&readyQueue);
  if (p == NULL) {
    /* The Ready Queue is empty */
    
    if (procCnt == 0) {
      HALT();
    } else if (procCnt > 0 && softBlockCnt > 0) {
      WAIT();
    } else if (procCnt > 0 && softBlockCnt == 0) {
      PANIC();
    } else {
      /* This case should never happen! */
      PANIC();
    }

  }
  currentProc = p;
  /* Load 5 milliseconds on the PLT */
  setTIMER(5);
  LDST(&p->p_s);
}