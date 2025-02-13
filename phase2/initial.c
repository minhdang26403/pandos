#include "../h/asl.h"
#include "../h/scheduler.h"
#include "../h/initial.h"

/* Test function of phase 2 */
extern void test();

/* Global variables */
int procCnt;
int softBlockCnt;
pcb_PTR readyQueue;
pcb_PTR currentProc;
int deviceSem[NUMDEVICES + 1]; /* TODO: may need to change this */

int main() {
  /* 2. Populate the Processor 0 Pass Up Vector (TODO) */
  

  /* 3. Initialize pcb free list and active semaphore list */
  initPcbs();
  initASL();
  
  /* 4. Initialize all Nuclueus maintained variables */
  procCnt = 0;
  softBlockCnt = 0;
  readyQueue = mkEmptyProcQ();
  currentProc = NULL;
  int i;
  for (i = 0; i < NUMDEVICES + 1; i++) {
    deviceSem[i] = 0;
  }

  /* 5. Load the system-wide Interval Timer (TOOD) */

  /* 6. Instantiate a single process */
  pcb_PTR p = allocPcb(); /* TODO: need to initialize various field of proc */
  insertProcQ(&readyQueue, p);
  procCnt++;

  /* Note: When setting up a new processor state, one must set the previous bist
   * (i.e. IEp & KUp) and not the current bits (i.e. IEc & KUc) in the Status
   * register for the desired assignment to take effect after the initial LDST
   * loads the processor state.
   */
  p->p_s.s_status |= STATUS_IEP; /* Enable interrupts */
  p->p_s.s_status |= STATUS_TE; /* Enable processor Local Timer */
  p->p_s.s_status &= ~STATUS_KUP; /* Turn kernel mode on */
  
  /* TODO: RAMTOP = RAMSTART + installed RAM size
   * where to read installed RAM size?
   * read from register at RAMBASESIZE?
   */
  /* p->p_s.s_sp = RAMSTART + RAMBASESIZE; */

  p->p_s.s_pc = (memaddr) test;
  /* this register must get the same value as PC whenever PC is assigned a new
   * value 
   */
  p->p_s.s_t9 = (memaddr) test;

  /* 7. Call the scheduler */
  scheduler();
}
