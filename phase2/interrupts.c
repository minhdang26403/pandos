static void handlePLT(state_t *savedExcState) {
  /* Acknowledge the interrupt by reloading the timer with a 5ms time slice */
  setTIMER(5000);
  /* Copy the saved exception state into the current process's pcb */
  currentProc->p_s = *savedExcState;
  /* Update the accumulated CPU time (we have a 5ms slice) */
  currentProc->p_time += 5000;
  /* Enqueue the current process back into the ready queue */
  insertProcQ(&readyQueue, currentProc);
  /* Call the scheduler */
  scheduler();
}


static void handleIntervalTimer(state_t *savedExcState) {
  /* Acknowledge the interrupt by reloading the interval timer with 100ms */
  LDIT(100000);
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
  if (currentProc != NULL) {
    LDST(&currentProc->p_s);
  } else {
    /* It is also possible that there is no Current Process to return control to. This will be the case when the Scheduler executes the WAIT instruction instead of dispatching a process for execution */
    scheduler();
  }
}


static void handleDeviceInterrupt(state_t *savedExcState, int line) {
  int devnum;
  int semIndex = -1;
  int status = 0;
  device_t *dev = NULL;

  /* TODO: pandos.pdf section 5.6 says that the handler should only process one interrupt at a time, thus I'm using break inside the loop. But I'm not sure how to implement this idea properly inside if, else conditions */
  if (line != TERMINT) {
    /* Non-terminal devices */
    for (devnum = 0; devnum < DEVPERINT; devnum++) {
      /* Calculate the address for this device’s device register */
      semIndex = (line - 3) * DEVPERINT + devnum;
      dev = &(DEVREG + semIndex)
      /* Save off the status code from the device’s device register. */
      status = dev->d_status;
      /* Acknowledge the interrupt */
      dev->d_command = ACK;
      break;
    }
  } else {
    /* Terminal devices: check transmit (higher priority) then receive */
    for (devnum = 0; devnum < 8; devnum++) {
      /* Check transmit sub-device first */
      semIndex = 32 + devnum * 2 + 1;
      dev = &(DEVREG + semIndex);
      status = dev->d_status;
      dev->d_command = ACK;
      break;

      /* Then check receive sub-device */
      semIndex = 32 + devnum * 2;
      dev = &(DEVREG + semIndex);
      status = dev->d_status;
      dev->d_command = ACK;
      
      break;
    }
  }

  if (semIndex >= 0) {
    /* Perform V on the corresponding device semaphore */
    deviceSem[semIndex]++;
    if (deviceSem[semIndex] <= 0) {
      pcb_PTR p = removeBlocked(&deviceSem[semIndex]);
      if (p != NULL) {
        /* Place the stored off status code in the newly unblocked pcb’s v0 register */
        p->p_s.s_v0 = status;
        insertProcQ(&readyQueue, p);
      }
    }
  }
  /* Return control to the current process */
  if (currentProc != NULL) {
    LDST(&currentProc->p_s);
  } else {
    /* It is also possible that there is no Current Process to return control to. This will be the case when the Scheduler executes the WAIT instruction instead of dispatching a process for execution */
    scheduler();
  }
}


static void interruptHandler(void) {
  state_t *savedExcState = (state_t *)BIOSDATAPAGE;
  unsigned int ipending = CAUSE_IP(savedExcState->s_cause);

  /* Check for Processor Local Timer (PLT) interrupt (interrupt line 1) */
  if (ipending & (1 << 1)) {
    handlePLT(savedExcState);
  } else if (ipending & (1 << 2)) {
    /* Check for Interval Timer interrupt (interrupt line 2) */
    handleIntervalTimer(savedExcState);
  } else {
    /* Check non-timer device interrupts (interrupt lines 3 to 7) */
    int line;
    /* TODO: pandos.pdf section 5.6 says that the handler should only process one interrupt at a time, thus I'm using break inside the loop. But I'm not sure how to implement this idea properly inside if, else conditions */
    for (line = 3; line <= 7; line++) {
      if (ipending & (1 << line)) {
        handleDeviceInterrupt(savedExcState, line);
        /* After handling one interrupt, return to the interrupted process */
        return;
      }
    }

    /* If no recognized pending interrupt, return to the interrupted process */
    LDST(savedExcState);
  }
}


