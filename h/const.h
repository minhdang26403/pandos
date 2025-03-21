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
#define STATUS_IM_ALL_ON  0xFF00

#define STATUS_BEV   (1U << 22) /* Bootstrap Exception Vector */
#define STATUS_TE    (1U << 27) /* Local Timer Enable */

#define STATUS_CU0   (1U << 28) /* Coprocessor 0 Usability */

/* Mask to clear the ExcCode field (bits 2-6) */
#define EXCCODE_MASK 0x7C  /* 1111100 in binary, covers bits 2-6 */


#define RI_EXCCODE   (10 << 2)  /* Value for RI (10) shifted into position */
#define CAUSE_EXCCODE(cause) (((cause) >> 2) & 0x1F)  /* Extract ExcCode (bits 2-6) */ 
#define CAUSE_IP(cause) ((cause) & 0xFF00) /* Extract pending interrupt bits (bits 8-15) */

/* Cause register Status Codes */
#define EXC_TLBMOD    1
#define EXC_SYSCALL   8

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

/* Bit mask for device number in interrupting device bitmap */
#define DEV_BIT(devNum) (1 << (devNum))

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
#define PSEUDOCLOCK   NUMDEVICES  /* Index of the pseudo-clock semaphore in the device semaphore array */

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
#define UNINSTALLED		    0
#define READY			        1
#define BUSY			        3
#define CHAR_RECEIVED     5   /* Terminal receive completion */
#define CHAR_TRANSMITTED  5   /* Terminal transmit completion */

/* 
 * For terminal devices, their status code is stored in the first byte 
 * of the 32-bit TRANSM_STATUS/RECV_STATUS field */
#define TERMINT_STATUS_MASK 0xFF

/* device common COMMAND codes */
#define RESET			    0
#define ACK				    1

/* Flash COMMAND codes */
#define FLASH_READBLK       2
#define FLASH_WRITEBLK      3

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

/* Constants for VM management */
#define MAXPAGES        32                          /* 32 pages per U-proc */
#define UPROCMAX        8                           /* Maximum number of concurrent user processes */
#define STACKPAGE      (MAXPAGES - 1)               /* Page 31 for stack */

#define SWAP_POOL_SIZE  (2 * UPROCMAX)              /* Size of the Swap Pool */
#define SWAP_POOL_BASE  (RAMSTART + 32 * PAGESIZE)  /* Starting address of the Swap Pool */

#define ASID_UNOCCUPIED -1                          /* Marker for free Swap Pool frame */
#define ASID_SHIFT      6
#define ASID_MASK       6

#define VPN_SHIFT       12                          /* Shift for VPN (log2 PAGESIZE) */
#define VPN_MASK        0xFFFFF000                  /* Mask for VPN (bits 31-12) */
#define PFN_MASK        0xFFFFF000                  /* Mask for PFN (bits 31-12) */

#define VPN_TEXT_BASE   0x80000                     /* Base VPN for .text/.data */
#define VPN_STACK       0xBFFFF                     /* VPN for stack page */
#define TEXT_PAGE_COUNT 31                          /* Number of .text/.data pages */

/*
For a 32-bit EntryLo, the format is:
  - bits 31-12 (highest 20 bits): PFN
  - bit 11: N (Non-cacheable bit), unused
  - bit 10: D (Dirty bit)
  - bit 9: V (Valid bit)
  - bit 8: G (Global bit)
  - bits 7-0 (lowest 8 bits): Unused
*/
#define PTE_VALID       (1U << 9)
#define PTE_DIRTY       (1U << 10)

/* Constants to manipulate TLB-related CP0 control registers */
#define TLB_PRESENT     (1U << 31)

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

/* system call codes */
#define	CREATEPROCESS	    1	  /* create process */
#define	TERMINATEPROCESS	2	  /* terminate process */
#define	PASSEREN			    3	  /* P a semaphore */
#define	VERHOGEN		      4	  /* V a semaphore */
#define	WAITIO			      5	  /* delay on a io semaphore */
#define	GETCPUTIME		    6	  /* get cpu time used to date */
#define	WAITCLOCK		      7	  /* delay on the clock semaphore */
#define	GETSUPPORTPTR     8	  /* return support structure ptr. */

#endif
