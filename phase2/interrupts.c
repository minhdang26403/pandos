/**
 * @file interrupts.c
 * @author Dang Truong, Loc Pham
 * @brief This module implements the device interrupt handling routines for
 * Phase 2. It processes both timer and non-timer interrupts by:
 * - Handling Processor Local Timer (PLT) interrupts to preempt the running
 * process.
 * - Handling Interval Timer interrupts to unblock processes waiting on the
 * pseudo-clock.
 * - Handling device interrupts (including terminal devices) by acknowledging
 * the interrupt, performing the corresponding V operation on the appropriate
 * semaphore, and unblocking any waiting process. The module uses helper
 * functions to encapsulate the specific handling logic for different types of
 * interrupts.
 * @date 2025-04-17
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../h/interrupts.h"

#include "../h/asl.h"
#include "../h/exceptions.h"
#include "../h/initial.h"
#include "../h/pcb.h"
#include "../h/scheduler.h"
#include "umps3/umps/libumps.h"

/**
 * @brief Handle device interrupts (non-timer devices, including terminals).
 *
 * Acknowledges the device interrupt by issuing an ACK command. Performs a V
 * operation on the corresponding Nucleus-managed semaphore. If a process is
 * waiting on the device, it is unblocked, its return value (s_v0) is set to the
 * device status, and it is moved to the ready queue.
 *
 * @param savedExcState The saved exception state at the time of the interrupt.
 * @param lineNum Interrupt line number (3–7).
 * @param devNum Device number on that interrupt line (0–7).
 * @return This function does not return; control is transferred via
 * switchContext or scheduler.
 */

HIDDEN void handleDeviceInterrupt(state_t *savedExcState, int lineNum,
                                  int devNum) {
  /* Get the device's device register */
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  /* Calculate device index: offset from DISKINT (line 3), 8 devices per line */
  int devIdx = (lineNum - DISKINT) * DEVPERINT + devNum;
  device_t *devreg = &busRegArea->devreg[devIdx];

  unsigned int transStatus = devreg->t_transm_status & TERMINT_STATUS_MASK;
  unsigned int recvStatus = devreg->t_recv_status & TERMINT_STATUS_MASK;

  unsigned int statusCode;
  if (lineNum == TERMINT) {
    if (transStatus != BUSY && transStatus != READY) {
      /* Transmitter (write) - higher priority */
      statusCode = devreg->t_transm_status;
      devreg->t_transm_command = ACK; /* Ack transmit */
      /* devIdx maps to write semaphores (32-39) directly */
    } else if (recvStatus != BUSY && recvStatus != READY) {
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
 * @brief Handle Processor Local Timer (PLT) interrupt.
 *
 * Signals that the current process's quantum has expired. Reloads the timer for
 * a new quantum, updates the process's CPU time, saves its state, enqueues it
 * back on the ready queue, and invokes the scheduler.
 *
 * @param savedExcState The saved exception state at the time of the PLT
 * interrupt.
 * @return This function does not return; control is transferred via scheduler.
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
 * @brief Handle Interval Timer interrupt (every 100ms).
 *
 * Acknowledges the timer interrupt by reloading it. Unblocks all processes
 * waiting on the pseudo-clock semaphore and resets that semaphore to 0. Invokes
 * the scheduler if no process is currently running.
 *
 * @param savedExcState The saved exception state at the time of the interrupt.
 * @return This function does not return; control is transferred via
 * switchContext or scheduler.
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
 * @brief Main interrupt dispatcher for all interrupt types.
 *
 * Determines which interrupt(s) are pending by examining the Cause register:
 * - Line 1: PLT interrupt is handled by handlePLT()
 * - Line 2: Interval Timer is handled by handleIntervalTimer()
 * - Lines 3–7: Device interrupts are handled by handleDeviceInterrupt()
 *
 * Device interrupts are handled by scanning the interrupt bitmap for each
 * device line and invoking the appropriate handler for each active device.
 *
 * @param savedExcState The saved exception state at the time of the interrupt.
 * @return This function does not return; control is transferred via
 * switchContext or scheduler.
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
