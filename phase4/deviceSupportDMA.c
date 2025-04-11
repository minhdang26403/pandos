#include "../h/deviceSupportDMA.h"

#include "../h/initProc.h"
#include "../h/scheduler.h"
#include "../h/sysSupport.h"
#include "../h/vmSupport.h"
#include "umps3/umps/libumps.h"

/* Validate arguments to SYS16 and SYS17 (flash number, user address, and block
 * number) */
HIDDEN void validateFlashSyscallArgs(memaddr logicalAddr, int flashNum,
                                     int blockNum, support_t *sup) {
  /* 1. Ensure the logical address lies entirely within KUSEG */
  if (!isValidAddr(logicalAddr) || !isValidAddr(logicalAddr + PAGESIZE - 1)) {
    programTrapHandler(sup);
  }

  /* 2. Ensure flash device number is within range [0..7] */
  if (flashNum < 0 || flashNum >= DEVPERINT) {
    programTrapHandler(sup);
  }

  /* 3. Ensure block number is in [32..maxBlock-1] to avoid the backing store
   * range */
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flash =
      &busRegArea->devreg[(FLASHINT - DISKINT) * DEVPERINT + flashNum];
  unsigned int maxBlock = flash->d_data1;

  if (blockNum < 32 || blockNum >= maxBlock) {
    programTrapHandler(sup);
  }
}

HIDDEN void memcopy(void *dest, const void *src, unsigned int size) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;

  /* Copy byte-by-byte from `src` to `dest` */
  unsigned int i;
  for (i = 0; i < size; i++) {
    d[i] = s[i];
  }
}

HIDDEN void diskOperation(state_t *excState, support_t *sup, unsigned int op) {
  memaddr logicalAddr = excState->s_a1;
  unsigned int diskNum = excState->s_a2;
  unsigned int sectorNum = excState->s_a3;

  /* Validate logical address lies entirely within KUSEG */
  if (!isValidAddr(logicalAddr) || !isValidAddr(logicalAddr + PAGESIZE - 1)) {
    programTrapHandler(sup);
  }

  /*
   * Validate disk number: must be in [1..7]
   * Note: diskNum is unsigned, so negative diskNum value is wrapped around to a
   * very large integer. Disk 0 is reserved for the backing store.
   */
  if (diskNum == 0 || diskNum >= DEVPERINT) {
    programTrapHandler(sup);
  }

  /* Read disk geometry from DATA1 and validate sector number */
  unsigned int devIdx = (DISKINT - DISKINT) * DEVPERINT + diskNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *disk = &busRegArea->devreg[devIdx];

  unsigned int data1 = disk->d_data1;
  unsigned int maxCyl = GET_DISK_CYLINDER(data1);
  unsigned int maxHead = GET_DISK_HEAD(data1);
  unsigned int maxSect = GET_DISK_SECTOR(data1);
  unsigned int maxSector = maxCyl * maxHead * maxSect;

  if (sectorNum >= maxSector) {
    programTrapHandler(sup);
  }

  /* Translate sector number to (cylinder, head, sector) */
  unsigned int cyl = sectorNum / (maxHead * maxSect);
  unsigned int rem = sectorNum % (maxHead * maxSect);
  unsigned int head = rem / maxSect;
  unsigned int sect = rem % maxSect;

  /* Compute physical DMA buffer address for this disk */
  memaddr dmaBuf = DISK_DMA_BASE + diskNum * PAGESIZE;

  /* Gain exclusive access to the device and DMA buffer */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[devIdx], 0, 0);

  if (op == DISK_WRITEBLK) {
    /* For disk write operations, copy data from user space to DMA buffer */
    memcopy((void *)dmaBuf, (void *)logicalAddr, PAGESIZE);
  }

  /* Issue SEEK to position disk head at the correct cylinder */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (cyl << DISK_CYL_SHIFT) | SEEKCYL;
  int result = SYSCALL(WAITIO, DISKINT, diskNum, 0);
  setSTATUS(status);

  if (result != READY) {
    /* Abort on SEEK failure */
    SYSCALL(VERHOGEN, (int)&supportDeviceSem[devIdx], 0, 0);
    excState->s_v0 = -result;
    switchContext(excState);
  }

  /* Set DMA buffer address */
  disk->d_data0 = dmaBuf;

  /* Issue READ or WRITE command */
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (head << DISK_HEAD_SHIFT) | (sect << DISK_SECT_SHIFT) | op;
  result = SYSCALL(WAITIO, DISKINT, diskNum, 0);
  setSTATUS(status);

  if (result == READY) {
    excState->s_v0 = result;

    if (op == DISK_READBLK) {
      /* For disk read operations, copy data from DMA buffer to user space */
      memcopy((void *)logicalAddr, (void *)dmaBuf, PAGESIZE);
    }
  } else {
    excState->s_v0 = -result;
  }

  /* Release device semaphore */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[diskNum], 0, 0);

  switchContext(excState);
}

/* SYS14 */
void sysDiskWrite(state_t *excState, support_t *sup) {
  diskOperation(excState, sup, DISK_WRITEBLK);
}

/* SYS15 */
void sysDiskRead(state_t *excState, support_t *sup) {
  diskOperation(excState, sup, DISK_READBLK);
}

int writeFlashPage(int flashNum, int blockNum, memaddr src) {
  /* 1. Get device pointer for the target flash device */
  int devIdx = (FLASHINT - DISKINT) * DEVPERINT + flashNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flash = &busRegArea->devreg[devIdx];

  /* 2. Lock device semaphore to ensure exclusive access */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[flashNum], 0, 0);

  /* 3. Set the physical address of the DMA source buffer */
  flash->d_data0 = src;

  /* 4. Disable interrupts and issue WRITEBLK command */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  flash->d_command = (blockNum << BYTELEN) | FLASH_WRITEBLK;

  /* 5. Wait for I/O to complete */
  int result = SYSCALL(WAITIO, FLASHINT, flashNum, 0);
  setSTATUS(status);

  /* 6. Unlock device semaphore */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[flashNum], 0, 0);

  /* 7. Return result: READY (1) on success, negative status on error */
  return (result == READY) ? result : -result;
}

int readFlashPage(int flashNum, int blockNum, memaddr dest) {
  /* 1. Get device pointer for the target flash device */
  int devIdx = (FLASHINT - DISKINT) * DEVPERINT + flashNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flash = &busRegArea->devreg[devIdx];

  /* 2. Lock device semaphore to ensure exclusive access */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[flashNum], 0, 0);

  /* 3. Set the physical address of the DMA destination buffer */
  flash->d_data0 = dest;

  /* 4. Disable interrupts and issue READBLK command */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  flash->d_command = (blockNum << BYTELEN) | FLASH_READBLK;

  /* 5. Wait for I/O to complete */
  int result = SYSCALL(WAITIO, FLASHINT, flashNum, 0);
  setSTATUS(status);

  /* 6. Unlock device semaphore */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[flashNum], 0, 0);

  /* 7. Return result: READY (1) on success, negative status on error */
  return (result == READY) ? result : -result;
}

void sysFlashPut(state_t *excState, support_t *sup) {
  memaddr logicalAddr = excState->s_a1;
  int flashNum = excState->s_a2;
  int blockNum = excState->s_a3;

  /* 1. Validate syscall arguments: address, device, block range */
  validateFlashSyscallArgs(logicalAddr, flashNum, blockNum, sup);

  /* 2. Compute DMA buffer address for the given flash device */
  memaddr dmaBuf = FLASH_DMA_BASE + flashNum * PAGESIZE;

  /* 3. Copy one page from user space to kernel DMA buffer */
  memcopy((void *)dmaBuf, (void *)logicalAddr, PAGESIZE);

  /* 4. Issue write to flash block using kernel DMA buffer */
  excState->s_v0 = writeFlashPage(flashNum, blockNum, dmaBuf);

  /* 5. Resume user process */
  switchContext(excState);
}

void sysFlashGet(state_t *excState, support_t *sup) {
  memaddr logicalAddr = excState->s_a1;
  int flashNum = excState->s_a2;
  int blockNum = excState->s_a3;

  /* 1. Validate syscall arguments: address, device, block range */
  validateFlashSyscallArgs(logicalAddr, flashNum, blockNum, sup);

  /* 2. Compute DMA buffer address for the given flash device */
  memaddr dmaBuf = FLASH_DMA_BASE + flashNum * PAGESIZE;

  /* 3. Issue read from flash block into kernel DMA buffer */
  int result = readFlashPage(flashNum, blockNum, dmaBuf);

  /* 4. If successful, copy page from DMA buffer to user space */
  if (result > 0) {
    memcopy((void *)logicalAddr, (void *)dmaBuf, PAGESIZE);
  }

  /* 5. Return result code to user process */
  excState->s_v0 = result;

  /* 6. Resume user process */
  switchContext(excState);
}
