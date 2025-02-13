#ifndef EXCEPTIONS
#define EXCEPTIONS

/************************** EXCEPTIONS.H ******************************
*
*  The externals declaration file for the Exceptions Module.
*
*  Written by Dang Truong
*/

#include "types.h"

extern void uTLB_Refillhandler();

/* process context */
typedef struct context_t {
  /* process context fields */
  unsigned int  c_stackPtr,             /* stack pointer value */
                c_status,               /* status reg value    */
                c_pc;                   /* PC address          */
} context_t;

typedef struct support_t {
  int           sup_asid;               /* Process Id (asid)   */
  state_t       sup_exceptState[2];     /* stored excpt states */
  context_t     sup_exceptContext[2];   /* pass up contexts    */

} support_t;
 
/* Exceptions related constants */
#define PGFAULTEXCEPT 0
#define GENERALEXCEPT 1

/***************************************************************/

#endif
