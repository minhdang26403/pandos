/************************** INTERRUPTS.C ******************************
 *
 * The Device Interrupt Handling Module.
 * 
 * Description:
 *   This module implements the device interrupt handling routines for Phase 2.
 *   It processes both timer and non-timer interrupts by:
 *     - Handling Processor Local Timer (PLT) interrupts to preempt the running
 *       process.
 *     - Handling Interval Timer interrupts to unblock processes waiting on the
 *       pseudo-clock.
 *     - Handling device interrupts (including terminal devices) by acknowledging
 *       the interrupt, performing the corresponding V operation on the appropriate
 *       semaphore, and unblocking any waiting process.
 *   The module uses helper functions to encapsulate the specific handling logic
 *   for different types of interrupts.
 *
 * Written by Dang Truong, Loc Pham
 */

/***************************************************************/

#include "../h/interrupts.h"

#include "../h/asl.h"
#include "../h/exceptions.h"
#include "../h/initial.h"
#include "../h/pcb.h"
#include "../h/scheduler.h"
#include "umps3/umps/libumps.h"

/**
 * Function: handleDeviceInterrupt
 * -------------------------------
 * Purpose:
 *   Handles interrupts for non-timer devices including terminal devices. The
 *   function calculates the proper device register address, acknowledges the
 *   interrupt by sending an ACK command, and performs a V operation on the
 *   corresponding Nucleus-maintained semaphore. If a process is waiting on the
 *   device, it is unblocked and its return status is set accordingly.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state at the time of the
 *                   interrupt.
 *   lineNum       - The interrupt line number where the device interrupt occurred.
 *   devNum        - The device number on the specified interrupt line.
 *
 * Returns:
 *   This function does not return normally; control is transferred either via
 *   switchContext or scheduler.
 */
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
    /* Wake up from WAIT state */
    scheduler();
  } else {
    /* Return control to the current process */
    switchContext(savedExcState);
  }
}

/**
 * Function: handlePLT
 * ---------------------
 * Purpose:
 *   Handles Processor Local Timer (PLT) interrupts, which signal that the
 *   current process's time slice (quantum) has expired. This routine:
 *     - Reloads the timer for a new 5ms quantum.
 *     - Updates the current process's CPU time.
 *     - Saves its state and enqueues it back onto the ready queue.
 *     - Calls the scheduler to dispatch the next process.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state at the time of the
 *                   PLT interrupt.
 *
 * Returns:
 *   This function does not return normally; control is transferred via scheduler.
 */
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

/**
 * Function: handleIntervalTimer
 * -----------------------------
 * Purpose:
 *   Handles Interval Timer interrupts, which occur every 100ms and are used to
 *   generate pseudo-clock ticks. The function:
 *     - Acknowledges the interrupt by reloading the timer.
 *     - Unblocks all processes waiting on the pseudo-clock semaphore.
 *     - Resets the pseudo-clock semaphore.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state at the time of the
 *                   Interval Timer interrupt.
 *
 * Returns:
 *   This function does not return normally; control is transferred via either
 *   switchContext or scheduler.
 */
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
    /* Wake up from WAIT state */
    scheduler();
  } else {
    /* Return control to the current process */
    switchContext(savedExcState);
  }
}

/**
 * Function: interruptHandler
 * --------------------------
 * Purpose:
 *   Serves as the main interrupt dispatcher. It examines the saved exception
 *   state's Cause register to determine which interrupts are pending, then:
 *     - Dispatches PLT interrupts if bit 1 is set.
 *     - Dispatches Interval Timer interrupts if bit 2 is set.
 *     - Iterates through interrupt lines 3-7 to process device interrupts.
 *   After handling an interrupt, it transfers control either by switching
 *   context or invoking the scheduler.
 *
 * Parameters:
 *   savedExcState - Pointer to the saved exception state at the time of the
 *                   interrupt.
 *
 * Returns:
 *   This function should never return normally. If control reaches the end,
 *   PANIC is invoked.
 */
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

  if (currentProc == NULL) {
    scheduler();
  } else {
    switchContext(savedExcState);
  }
}
