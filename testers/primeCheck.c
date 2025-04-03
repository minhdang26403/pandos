#include "h/localLibumps.h"
#include "h/tconst.h"
#include "h/print.h"

/* Check if a number is prime */
int isPrime(int n) {
    int i;
    if (n < 2)
        return 0;
    for (i = 2; i * i <= n; i++) {
        if (n % i == 0)
            return 0;
    }
    return 1;
}

void main() {
	int i;
	
	print(WRITETERMINAL, "Prime Check (9973) Test starts\n");
	
	i = isPrime(9973);
	
	print(WRITETERMINAL, "Prime Check Concluded\n");
	
	if (i == 1) {
		print(WRITETERMINAL, "Prime Check Concluded Successfully\n");
	}
	else
		print(WRITETERMINAL, "ERROR: Prime Check problems\n");
		
	/* Terminate normally */	
	SYSCALL(TERMINATE, 0, 0, 0);
}

