/*	Test Virtual P's and V's. pvTestA (consumer) and pvTestB (producer)
 *	exchange a message using the shared segment for synchronization
 *	and data transmission.
 */

#include "h/localLibumps.h"
#include "h/tconst.h"
#include "h/print.h"

int *hold = (int *)(SEG3);
int *empty = (int *)(SEG3 + 4);
int *full = (int *)(SEG3 + 8);
char *charbuff = (char *)(SEG3 + 12);

void main() {
	int mysem;
	char *msg;

	print(WRITETERMINAL, "pvBTest starts\n");

	/* give pvTestA a chance to start up and initialize shared memory */
	
	/* Delay for 3 seconds */
	SYSCALL(DELAY, 3, 0, 0);
	
	SYSCALL(VSEMVIRT, (int) hold, 0, 0);
	print(WRITETERMINAL, "pvBTest is free\n");

	msg = "virtual synch OK\n";	/* message to be sent to t3 */

	/* send message to t3 */
	do {
		SYSCALL(PSEMVIRT, (int) empty, 0, 0);
		*charbuff = *msg;
		print(WRITETERMINAL, ".");
		SYSCALL(VSEMVIRT, (int) full, 0, 0);
	} while (*msg++ != '\0');

	print(WRITETERMINAL, "\npvBTest finished sending\n");
	
	print(WRITETERMINAL, "pvBTest completed\n");
	
	/* try to block on a private semaphore */
	mysem = 0;
	SYSCALL(PSEMVIRT, (int) &mysem, 0, 0);

	/* should never reach here */
	print(WRITETERMINAL, "pvBTest error: private sem block did not terminate\n");
	SYSCALL(TERMINATE, 0, 0, 0);
}

