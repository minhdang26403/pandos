/************************** INTERRUPTS.C ******************************
 *
 *  The Device Interrupt Handling Module.
 *
 *  Written by Dang Truong
 */

/***************************************************************/

#include "../h/interrupts.h"

#include "../h/asl.h"
#include "../h/exceptions.h"
#include "../h/initial.h"
#include "../h/scheduler.h"
#include "umps3/umps/libumps.h"

HIDDEN void handleDeviceInterrupt(state_t *savedExcState, int lineNum,
                                  int devNum) {
  /* Get the device's device register */
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  /* Calculate device index: offset from DISKINT (line 3), 8 devices per line */
  int devIdx = (lineNum - DISKINT) * DEVPERINT + devNum;
  device_t *devreg = &busRegArea->devreg[devIdx];

  unsigned int statusCode;
  if (lineNum == TERMINT) {
    if ((devreg->t_transm_status & TERMINT_STATUS_MASK) != BUSY) {
      /* Transmitter (write) - higher priority */
      statusCode = devreg->t_transm_status;
      devreg->t_transm_command = ACK; /* Ack transmit */
      /* devIdx maps to write semaphores (32-39) directly */
    } else if ((devreg->t_recv_status & TERMINT_STATUS_MASK) != BUSY) {
      /* Receiver (read) */
      statusCode = devreg->t_recv_status;
      devreg->t_recv_command = ACK; /* Ack receive */
      /* Offset devIdx by DEVPERINT (8) to map to read semaphores (40-47), since
         terminal devices have two sub-devices per devNum:
          - Write: 32-39 (base index from TERMINT - DISKINT = 4 * 8)
          - Read: 40-47 (base + 8) */
      devIdx += DEVPERINT;
    } else {
      /* Either transmission or receipt must be completed */
      PANIC();
    }
  } else {
    /* Non-terminal devices */
    statusCode = devreg->d_status;
    devreg->d_command = ACK;
    /* devIdx maps directly to semaphores 0-31 for lines 3-6 */
  }

  /* Perform a V operation on the Nucleus maintained semaphore */
  deviceSem[devIdx]++;
  pcb_PTR p = removeBlocked(&deviceSem[devIdx]);
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
  int *pseudoSem = &deviceSem[PSEUDOCLOCK];
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
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;

  /* Handle highest-priority interrupts first */
  if (pendingInterrupts & STATUS_IM(1)) {
    /* PLT interrupt (line 1) */
    handlePLT(savedExcState);
  } else if (pendingInterrupts & STATUS_IM(2)) {
    /* Interval Timer interrupt (line 2) */
    handleIntervalTimer(savedExcState);
  } else {
    /* Non-timer device interrupts (lines 3-7) */
    int lineNum;
    for (lineNum = DISKINT; lineNum <= TERMINT; lineNum++) {
      if (pendingInterrupts & STATUS_IM(lineNum)) {
        /* This interrupt line has pending interrupts */
        unsigned int interruptBitMap =
            busRegArea->interrupt_dev[lineNum - DISKINT];
        int devNum;
        for (devNum = 0; devNum < DEVPERINT; devNum++) {
          /* This device has a pending interrupt */
          if (interruptBitMap & DEV_BIT(devNum)) {
            handleDeviceInterrupt(savedExcState, lineNum, devNum);
          }
        }
      }
    }
  }

  /* Should never reach here; each handler should return via switchContext or
   * scheduler */
  PANIC();
}
