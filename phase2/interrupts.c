#include "../h/asl.h"
#include "../h/exceptions.h"
#include "../h/initial.h"
#include "../h/scheduler.h"
#include "umps3/umps/libumps.h"

HIDDEN void handleDeviceInterrupt(state_t *savedExcState, int lineNum,
                                  int devNum) {
  /* Get the device's device register */
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *devreg = &busRegArea->devreg[(lineNum - 3) * DEVPERINT + devNum];

  unsigned int statusCode;
  int semIndex;

  if (lineNum == TERMINT) {
    if ((devreg->t_transm_status & TERMINT_STATUS_MASK) != BUSY) {
      /* Transmitter (write) - higher priority */
      statusCode = devreg->t_transm_status;
      devreg->t_transm_command = ACK;                /* Ack transmit */
      semIndex = (lineNum - 3) * DEVPERINT + devNum; /* 32-39 */
    } else if ((devreg->t_recv_status & TERMINT_STATUS_MASK) != BUSY) {
      /* Receiver (read) */
      statusCode = devreg->t_recv_status;
      devreg->t_recv_command = ACK;                      /* Ack receive */
      semIndex = (lineNum - 3 + 1) * DEVPERINT + devNum; /* 40-47 */
    } else {
      /* Either transmission or receipt must be completed */
      PANIC();
    }
  } else {
    /* Non-terminal devices */
    statusCode = devreg->d_status;
    devreg->d_command = ACK;
    semIndex = (lineNum - 3) * DEVPERINT + devNum;
  }

  /* Perform a V operation on the Nucleus maintained semaphore associated with
   * this (sub)device */
  deviceSem[semIndex]++;
  pcb_PTR p = removeBlocked(&deviceSem[semIndex]);
  if (p != NULL) {
    p->p_s.s_v0 = statusCode; /* Return status to process */
    softBlockCnt--;
    insertProcQ(&readyQueue, p);
  }

  if (currentProc == NULL) {
    scheduler();
  } else {
    /* Return control to the current process */
    switchContext(savedExcState);
  }
}

HIDDEN void handlePLT(state_t *savedExcState) {
  /* Acknowledge the interrupt by reloading the timer with a 5ms time slice */
  setTIMER(QUANTUM);

  /* Copy the saved exception state into the current process's pcb */
  copyState(&currentProc->p_s, savedExcState);

  /* Update the accumulated CPU time */
  cpu_t now;
  STCK(now);
  currentProc->p_time += now - quantumStartTime;

  /* Enqueue the current process back into the ready queue */
  insertProcQ(&readyQueue, currentProc);
  currentProc = NULL;

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
    softBlockCnt--;
  }

  /* Reset pseudo-clock semaphore */
  *pseudoSem = 0;

  if (currentProc == NULL) {
    scheduler();
  } else {
    /* Return control to the current process */
    switchContext(savedExcState);
  }
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
    devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;

    /* Check which interrupt lines have pending interrupts */
    for (lineNum = 3; lineNum <= 7; lineNum++) {
      if (pendingInterrupts & STATUS_IM(lineNum)) {
        unsigned int interruptDevBitMap =
            busRegArea->interrupt_dev[(lineNum - 3)];
        int devNum;
        /* Check which devices on this line have a pending interrupt */
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
