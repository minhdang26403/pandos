#include "h/localLibumps.h"
#include "h/tconst.h"
#include "h/print.h"

#define SIZE 500

void bubbleSort(int arr[], int size) {
  int i, j, temp;
  for (i = 0; i < size - 1; i++) {
    for (j = 0; j < size - 1 - i; j++) {
      if (arr[j] > arr[j + 1]) {
        temp = arr[j];
        arr[j] = arr[j + 1];
        arr[j + 1] = temp;
      }
    }
  }
}

void main() {
  int input[SIZE];
  int expected[SIZE];
  int i;
  int testPassed = 1;

  print(WRITETERMINAL, "Bubble Sort Test starts\n");

  /* Initialize input array in reverse order */
  for (i = 0; i < SIZE; i++) {
    input[i] = SIZE - 1 - i;
  }

  /* Initialize expected array in sorted order */
  for (i = 0; i < SIZE; i++) {
    expected[i] = i;
  }

  bubbleSort(input, SIZE);

  /* Compare sorted array with expected array */
  for (i = 0; i < SIZE; i++) {
    if (input[i] != expected[i]) {
      testPassed = 0;
      break;
    }
  }

  if (testPassed) {
    print(WRITETERMINAL, "Bubble Sort Test of 500 numbers Passed\n");
  } else {
    print(WRITETERMINAL, "Bubble Sort Test of 500 numbers Failed\n");
  }

  /* Terminate the process normally */
  SYSCALL(TERMINATE, 0, 0, 0);
}