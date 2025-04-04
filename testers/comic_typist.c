#include "../h/types.h"
#include "umps3/umps/libumps.h"

/*
 * Description: "Cosmic Typist" is an interactive, single-threaded user-space
 * program that transforms terminal typing into a sci-fi experience. The program
 * prompts the user to enter a message, reads the full input until a newline,
 * and echoes it back to the terminal with a rotating "starfield" animation (*,
 * -, +, |) prefixing the text across four frames. After the animation, it logs
 * the message to the printer as a "Transmission Log" and terminates gracefully.
 * This tests terminal I/O (SYS12, SYS13), printer output (SYS11), and process
 * termination (SYS9).
 *
 * Written by Dang Truong
 */

/* Constants */
#define BUFFER_SIZE 64
#define LOG_SIZE 128
#define DISPLAY_SIZE (BUFFER_SIZE + 4) /* Star + space + string + newline */

/* Starfield animation characters */
HIDDEN char stars[] = {'*', '-', '+', '|'};
HIDDEN int star_idx = 0;

/* Main process */
void cosmic_typist() {
  /* Local variables */
  char input[BUFFER_SIZE];
  char display[DISPLAY_SIZE];
  char log[LOG_SIZE];
  int log_len;
  int i, j;
  int volatile k;

  /* Prompt user */
  char prompt[] = "Enter cosmic message:\n";
  SYSCALL(WRITETERMINAL, (int)prompt, sizeof(prompt) - 1, 0);

  /* Read full input until newline */
  int input_len = SYSCALL(READTERMINAL, (int)input, 0, 0);
  if (input_len <= 0) {
    SYSCALL(TERMINATE, 0, 0, 0); /* Exit on error */
  }
  if (input[input_len - 1] == '\n') {
    input_len--; /* Strip newline for display/log */
  }
  input[input_len] = '\0';

  /* Echo with animation */
  for (i = 0; i < 4; i++) { /* Animate 4 frames */
    int display_len = 0;
    display[display_len++] = stars[star_idx];
    display[display_len++] = ' ';
    for (j = 0; j < input_len; j++) {
      display[display_len++] = input[j];
    }
    display[display_len++] = '\n';

    SYSCALL(WRITETERMINAL, (int)display, display_len, 0);
    star_idx = (star_idx + 1) % 4;

    /* Delay for animation */
    for (k = 0; k < 10000; k++);
  }

  /* Generate transmission log */
  log_len = 0;
  log[log_len++] = 'T';
  log[log_len++] = 'r';
  log[log_len++] = 'a';
  log[log_len++] = 'n';
  log[log_len++] = 's';
  log[log_len++] = 'm';
  log[log_len++] = 'i';
  log[log_len++] = 's';
  log[log_len++] = 's';
  log[log_len++] = 'i';
  log[log_len++] = 'o';
  log[log_len++] = 'n';
  log[log_len++] = ' ';
  log[log_len++] = 'L';
  log[log_len++] = 'o';
  log[log_len++] = 'g';
  log[log_len++] = ':';

  /* Message */
  log[log_len++] = ' ';
  log[log_len++] = 'M';
  log[log_len++] = 's';
  log[log_len++] = 'g';
  log[log_len++] = '=';
  for (i = 0; i < input_len; i++) {
    log[log_len++] = input[i];
  }
  log[log_len++] = '\n';

  SYSCALL(WRITEPRINTER, (int)log, log_len, 0);

  SYSCALL(TERMINATE, 0, 0, 0);
}

void main() { cosmic_typist(); }
