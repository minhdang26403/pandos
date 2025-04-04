#include "h/localLibumps.h"
#include "h/tconst.h"
#include "h/print.h"

/**
 * User program to test the bubble sort algorithm.
 *
 * This module implements a bubble sort algorithm to sort an array of SIZE (500) 
 * numbers in ascending order. The unsorted array is initialized in reverse order 
 * (largest to smallest), and an expected array in sorted order (0 to SIZE-1) is hardcoded.
 *
 * The program performs the following steps:
 *  - Initializes an unsorted array and a corresponding expected sorted array.
 *  - Calls the bubbleSort() function to sort the unsorted array.
 *  - Compares the sorted array with the expected array.
 *  - Prints a success message if the arrays match, or an error message otherwise.
 *  - Terminates the process using a SYS call.
 * 
 * Written by Loc Pham
 */

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