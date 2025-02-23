#ifndef CONSTS
#define CONSTS

/**************************************************************************** 
 *
 * This header file contains utility constants & macro definitions.
 * 
 ****************************************************************************/

/* Hardware & software constants */
#define PAGESIZE		  4096			/* page size in bytes	*/
#define WORDLEN			  4				  /* word size in bytes	*/
#define MAXINT        2147483647

#define MAXPROC       20        /* Maximum number of concurrent processes */
#define QUANTUM       5000      /* Each process gets a time slice of 5 miliseconds */

/* Status Register Bit Definitions */
#define ZERO_MASK    (0U)
#define STATUS_IEC   (1U << 0)  /* Current Global Interrupt Enable */
#define STATUS_KUC   (1U << 1)  /* Current Kernel/User Mode (0 = Kernel, 1 = User) */

#define STATUS_IEP   (1U << 2)  /* Previous Interrupt Enable */
#define STATUS_KUP   (1U << 3)  /* Previous Kernel/User Mode */

#define STATUS_IEO   (1U << 4)  /* Old Interrupt Enable */
#define STATUS_KUO   (1U << 5)  /* Old Kernel/User Mode */

#define STATUS_IM(i) (1U << (8 + (i))) /* Interrupt Mask (bits 8-15) */

#define STATUS_BEV   (1U << 22) /* Bootstrap Exception Vector */
#define STATUS_TE    (1U << 27) /* Local Timer Enable */

#define STATUS_CU0   (1U << 28) /* Coprocessor 0 Usability */

/* Mask to clear the ExcCode field (bits 2-6) */
#define EXCCODE_MASK 0x7C  /* 1111100 in binary, covers bits 2-6 */


#define RI_EXCCODE   (10 << 2)  /* Value for RI (10) shifted into position */
#define CAUSE_EXCCODE(cause) (((cause) >> 2) & 0x1F)  /* Extract ExcCode (bits 2-6) */ 
#define CAUSE_IP(cause) ((cause) & 0xFF00) /* Extract pending interrupt bits (bits 8-15) */

/* timer, timescale, TOD-LO and other bus regs */
#define RAMBASEADDR		0x10000000
#define RAMBASESIZE		0x10000004
#define TODLOADDR		  0x1000001C
#define INTERVALTMR		0x10000020	
#define TIMESCALEADDR	0x10000024


/* utility constants */
#define	TRUE			    1
#define	FALSE			    0
#define HIDDEN			  static
#define EOS				    '\0'

#define NULL 			    ((void *)0xFFFFFFFF)

/* device interrupts */
#define DISKINT			  3
#define FLASHINT 		  4
#define NETWINT 		  5
#define PRNTINT 		  6
#define TERMINT			  7

#define DEVINTNUM		  5		  /* interrupt lines used by devices */
#define DEVPERINT		  8		  /* devices per interrupt line */
#define DEVREGLEN		  4		  /* device register field length in bytes, and regs per dev */	
#define DEVREGSIZE	  16 		/* device register size in bytes */

/* uMPS3 supports five different classes of peripheral devices (disk, flash,
 * (network card, printer and terminal) and up to eight instances of each type.
 * Each terminal device consists of two independent sub-devices (one for receiving
 * and one for transmitting), meaning we need to double the count for terminals:
 *    NUMDEVICES = (4 x 8) + (8 x 2) = 48
 */
#define NUMDEVICES    48

/* device register field number for non-terminal devices */
#define STATUS			  0
#define COMMAND			  1
#define DATA0			    2
#define DATA1			    3

/* device register field number for terminal devices */
#define RECVSTATUS  	0
#define RECVCOMMAND 	1
#define TRANSTATUS  	2
#define TRANCOMMAND 	3

/* device common STATUS codes */
#define UNINSTALLED		0
#define READY			    1
#define BUSY			    3
#define CHAR_RECV     5
#define CHAR_TRANSM   5

/* device common COMMAND codes */
#define RESET			    0
#define ACK				    1

/* Memory related constants */
#define KSEG0           0x00000000
#define KSEG1           0x20000000
#define KSEG2           0x40000000
#define KUSEG           0x80000000
#define RAMSTART        0x20000000
#define BIOSDATAPAGE    0x0FFFF000
#define	PASSUPVECTOR	  0x0FFFF900
#define STACKTOP        0x20001000  /* Nucleus stack size is one page (4KB) */
#define DEVREG          0x10000054 /* All 40 device registers are located in low memory starting at 0x1000.0054 */

/* Exceptions related constants */
#define	PGFAULTEXCEPT	  0
#define GENERALEXCEPT	  1


/* operations */
#define	MIN(A,B)		((A) < (B) ? A : B)
#define MAX(A,B)		((A) < (B) ? B : A)
#define	ALIGNED(A)		(((unsigned)A & 0x3) == 0)

#define SYSTEM_TICK_INTERVAL  100000 /* system-wide Interval Timer with 100 milliseconds */

/* Macro to load the Interval Timer */
#define LDIT(T)	((* ((cpu_t *) INTERVALTMR)) = (T) * (* ((cpu_t *) TIMESCALEADDR))) 

/* Macro to read the TOD clock */
#define STCK(T) ((T) = ((* ((cpu_t *) TODLOADDR)) / (* ((cpu_t *) TIMESCALEADDR))))

#endif
