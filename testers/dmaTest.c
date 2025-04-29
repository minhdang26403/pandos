/*  Test DiskWrite, DiskRead, FlashWrite, FlashRead */

/**
 * @file dmaTest.c
 * @author Dang Truong
 * @brief A simple user-space program that tests DMA device syscalls: Disk_Put,
 * Disk_Get, Flash_Put, Flash_Get.
 * @date 2025-04-28
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "h/localLibumps.h"
#include "h/print.h"
#include "h/tconst.h"

#define TESTVAL1 0x12345678
#define TESTVAL2 0xDEADBEEF
#define FLASH_BLOCK 3
#define DISK_SECTOR 5
#define DISK_NUM 1  /* Disk 1 */
#define FLASH_NUM 0 /* Flash 0 */

void main() {
  int dstatus;
  int *diskBuf;
  int *flashBuf;

  diskBuf = (int *)(SEG2 + (10 * PAGESIZE));  /* Disk buffer in kuseg */
  flashBuf = (int *)(SEG2 + (11 * PAGESIZE)); /* Flash buffer in kuseg */

  print(WRITETERMINAL, "dmaTest: starts\n");

  /* ------------------ Test Disk Write and Disk Read ------------------ */

  *diskBuf = TESTVAL1; /* Store some value */
  dstatus = SYSCALL(DISK_PUT, (int)diskBuf, DISK_NUM, DISK_SECTOR);

  if (dstatus != READY) {
    print(WRITETERMINAL, "dmaTest error: disk write failed\n");
  } else {
    /* Clear buffer */
    *diskBuf = 0;

    dstatus = SYSCALL(DISK_GET, (int)diskBuf, DISK_NUM, DISK_SECTOR);

    if (dstatus != READY) {
      print(WRITETERMINAL, "dmaTest error: disk read failed\n");
    } else if (*diskBuf != TESTVAL1) {
      print(WRITETERMINAL, "dmaTest error: disk data mismatch\n");
    } else {
      print(WRITETERMINAL, "dmaTest ok: disk read/write verified\n");
    }
  }

  /* ------------------ Test Flash Write and Flash Read ------------------ */

  *flashBuf = TESTVAL2; /* Store another value */
  dstatus = SYSCALL(FLASH_PUT, (int)flashBuf, FLASH_NUM, FLASH_BLOCK);

  if (dstatus != READY) {
    print(WRITETERMINAL, "dmaTest error: flash write failed\n");
  } else {
    /* Clear buffer */
    *flashBuf = 0;

    dstatus = SYSCALL(FLASH_GET, (int)flashBuf, FLASH_NUM, FLASH_BLOCK);

    if (dstatus != READY) {
      print(WRITETERMINAL, "dmaTest error: flash read failed\n");
    } else if (*flashBuf != TESTVAL2) {
      print(WRITETERMINAL, "dmaTest error: flash data mismatch\n");
    } else {
      print(WRITETERMINAL, "dmaTest ok: flash read/write verified\n");
    }
  }

  /* ------------------ All Tests Passed ------------------ */
  print(WRITETERMINAL, "dmaTest: completed\n");

  SYSCALL(TERMINATE, 0, 0, 0);
}
