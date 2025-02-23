#include "asl.h"
#include "initial.h"
#include "scheduler.h"
#include "types.h"

HIDDEN void handleDeviceInterrupt(state_t *savedExcState, int lineNum,
                                  int devNum) {
  /* Get the device's device register */
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *devreg = &busRegArea->devreg[(lineNum - 3) * DEVPERINT + devNum];

  /* Acknowledge the outstanding interrupt */
  devreg->d_command = ACK;

  /* Save off the status code from the device's device register */
  unsigned int statusCode;
  int waitForTermRead = 0;

  if (lineNum != TERMINT) {
    statusCode = devreg->d_status;
  } else {
    if (devreg->t_transm_status == READY) {
      /* Prioritize terminal transmission */
      statusCode = devreg->t_transm_status;
    } else {
      /* If terminal transmission is not ready, then terminal receipt must be
       * ready */
      statusCode = devreg->t_recv_status;
      waitForTermRead = 1;
    }
  }

  /* The first three lines are for Inter-process interrupts, Processor Local
   * Timer, Interval Timer. We have two sets of semaphores (each set with 8
   * semaphores) for terminal transmission and terminal receipt. */
  int semIndex = (lineNum - 3 + waitForTermRead) * DEVPERINT + devNum;

  /* Perform a V operation on the Nucleus maintained semaphore associated with
   * this (sub)device */
  int *semaddr = &deviceSem[semIndex];
  (*semaddr)++;
  pcb_PTR p = removeBlocked(semaddr);
  if (p != NULL) {
    p->p_s.s_v0 = statusCode;
    softBlockCnt--;
    insertProcQ(&readyQueue, p);
  }

  switchContext(savedExcState);
}

HIDDEN void handlePLT(state_t *savedExcState) {
  /* Acknowledge the interrupt by reloading the timer with a 5ms time slice */
  setTIMER(QUANTUM);
  /* Copy the saved exception state into the current process's pcb */
  currentProc->p_s = *savedExcState;
  /* Update the accumulated CPU time (we have a 5ms slice) */
  currentProc->p_time += QUANTUM;
  /* Enqueue the current process back into the ready queue */
  insertProcQ(&readyQueue, currentProc);
  /* Call the scheduler */
  scheduler();
}

HIDDEN void handleIntervalTimer(state_t *savedExcState) {
  /* Acknowledge the interrupt by reloading the interval timer with 100ms */
  LDIT(SYSTEM_TICK_INTERVAL);
  /* Unblock all processes waiting on the pseudo-clock semaphore */
  int *pseudoSem = &deviceSem[NUMDEVICES];
  pcb_PTR p;
  while ((p = removeBlocked(pseudoSem)) != NULL) {
    insertProcQ(&readyQueue, p);
    /* Decrement soft-block count for each unblocked process */
    softBlockCnt--;
  }
  /* Reset pseudo-clock semaphore */
  *pseudoSem = 0;
  /* Return control to the current process */
  switchContext(savedExcState);
}

void interruptHandler(state_t *savedExcState) {
  unsigned int pendingInterrupts = CAUSE_IP(savedExcState->s_cause);

  if (pendingInterrupts & STATUS_IM(1)) {
    /* Check for Processor Local Timer (PLT) interrupt (interrupt line 1) */
    handlePLT(savedExcState);
  } else if (pendingInterrupts & STATUS_IM(2)) {
    /* Check for Interval Timer interrupt (interrupt line 2) */
    handleIntervalTimer(savedExcState);
  } else {
    /* Check non-timer device interrupts (interrupt lines 3 to 7) */
    int lineNum;
    for (lineNum = 3; lineNum <= 7; lineNum++) {
      if (pendingInterrupts & STATUS_IM(lineNum)) {
        /* Check the Interrupting Devices Bit Map for this line */
        devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
        unsigned int interruptDevBitMap = busRegArea->interrupt_dev[(lineNum - 3)];
        int devNum;
        for (devNum = 0; devNum < DEVPERINT; devNum++) {
          if (interruptDevBitMap & (1 << devNum)) {
            handleDeviceInterrupt(savedExcState, lineNum, devNum);
          }
        }
      }
    }
  }

  /* Control flow should never return here */
  PANIC();
}
