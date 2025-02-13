
#include "umps3/umps/libumps.h"
#include "../h/types.h"

void uTLB_Refillhandler() {
  setENTRYHI(0x80000000);
  setENTRYLO(0x00000000);
  TLBWR();
  LDST ((state_PTR) 0x0FFFF000);
}
