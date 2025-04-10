#include "../h/deviceSupportDMA.h"

#include "../h/initProc.h"
#include "../h/scheduler.h"
#include "../h/sysSupport.h"
#include "../h/vmSupport.h"
#include "umps3/umps/libumps.h"

/*
 * Function: isValidAddr
 * Purpose: Validate that a given memory address is within the U-proc's logical
 *          address space (KUSEG). Returns non-zero if valid; zero otherwise.
 * Parameters:
 *    - addr: The memory address to validate.
 */
HIDDEN int isValidAddr(memaddr addr) { return addr >= KUSEG; }

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

/* SYS14: Write 4KB from user space to a sector on disk */
void sysDiskPut(state_t *excState, support_t *sup) {
  memaddr logicalAddr = excState->s_a1;
  int diskNo = excState->s_a2;
  int sectorNo = excState->s_a3;

  /* 1. Validate disk number is in [0..7] */
  if (diskNo < 0 || diskNo >= DEVPERINT) {
    programTrapHandler(sup);
  }

  /* 2. Validate logical address lies entirely within KUSEG */
  if (!isValidAddr(logicalAddr) || !isValidAddr(logicalAddr + PAGESIZE - 1)) {
    programTrapHandler(sup);
  }

  /* 3. Compute physical DMA buffer address for this disk */
  memaddr dmaBuf = DISK_DMA_BASE + diskNo * PAGESIZE;

  /* 4. Read disk geometry from DATA1 and validate sector number */
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *disk =
      &busRegArea->devreg[(DISKINT - DISKINT) * DEVPERINT + diskNo];

  unsigned int data1 = disk->d_data1;
  int maxCyl = GET_DISK_CYLINDER(data1);
  int maxHead = GET_DISK_HEAD(data1);
  int maxSect = GET_DISK_SECTOR(data1);
  int maxSector = maxCyl * maxHead * maxSect;

  if (sectorNo < 0 || sectorNo >= maxSector) {
    programTrapHandler(sup);
  }

  /* 5. Copy one page from user space to kernel DMA buffer */
  memcopy((void *)dmaBuf, (void *)logicalAddr, PAGESIZE);

  /* 6. Lock disk device semaphore to ensure exclusive access */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[diskNo], 0, 0);

  /* 7. Translate sector number to (cylinder, head, sector) */
  int cyl = sectorNo / (maxHead * maxSect);
  int rem = sectorNo % (maxHead * maxSect);
  int head = rem / maxSect;
  int sect = rem % maxSect;

  /* 8. Issue SEEK command with interrupts disabled */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (cyl << BYTELEN) | SEEKCYL;
  SYSCALL(WAITIO, DISKINT, diskNo, 0);
  setSTATUS(status);

  /* 9. Set DMA address in device register */
  disk->d_data0 = dmaBuf;

  /* 10. Issue WRITEBLK command with interrupts disabled */
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (head << BYTELEN) | sect | DISK_WRITEBLK;
  int result = SYSCALL(WAITIO, DISKINT, diskNo, 0);
  setSTATUS(status);

  /* 11. Unlock disk device semaphore */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[diskNo], 0, 0);

  /* 12. Return device status code to user */
  excState->s_v0 = (result == READY) ? result : -result;

  /* 13. Resume U-proc execution */
  switchContext(excState);
}

void sysDiskGet(state_t *excState, support_t *sup) {
  memaddr logicalAddr = excState->s_a1;
  int diskNo = excState->s_a2;
  int sectorNo = excState->s_a3;

  /* 1. Validate disk number: must be in [1..7], DISK0 is reserved for backing
   * store */
  if (diskNo <= 0 || diskNo >= DEVPERINT) {
    programTrapHandler(sup);
  }

  /* 2. Validate logical address lies completely within KUSEG */
  if (!isValidAddr(logicalAddr) || !isValidAddr(logicalAddr + PAGESIZE - 1)) {
    programTrapHandler(sup);
  }

  /* 3. Calculate DMA buffer address and get disk device register pointer */
  memaddr dmaBuf = DISK_DMA_BASE + diskNo * PAGESIZE;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *disk =
      &busRegArea->devreg[(DISKINT - DISKINT) * DEVPERINT + diskNo];

  /* 4. Read geometry and validate sector number against disk capacity */
  unsigned int data1 = disk->d_data1;
  int maxCyl = GET_DISK_CYLINDER(data1);
  int maxHead = GET_DISK_HEAD(data1);
  int maxSect = GET_DISK_SECTOR(data1);
  int maxSector = maxCyl * maxHead * maxSect;

  if (sectorNo < 0 || sectorNo >= maxSector) {
    programTrapHandler(sup);
  }

  /* 5. Lock disk device semaphore for exclusive access */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[diskNo], 0, 0);

  /* 6. Translate sector number */
  int cyl = sectorNo / (maxHead * maxSect);
  int rem = sectorNo % (maxHead * maxSect);
  int head = rem / maxSect;
  int sect = rem % maxSect;

  /* 7. Issue SEEK command with interrupts disabled */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (cyl << BYTELEN) | SEEKCYL;
  SYSCALL(WAITIO, DISKINT, diskNo, 0);
  setSTATUS(status);

  /* 8. Set DMA buffer address in device register */
  disk->d_data0 = dmaBuf;

  /* 9. Issue READBLK command with interrupts disabled */
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (head << BYTELEN) | sect | DISK_READBLK;
  int result = SYSCALL(WAITIO, DISKINT, diskNo, 0);
  setSTATUS(status);

  /* 10. Unlock disk device semaphore */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[diskNo], 0, 0);

  /* 11. Copy one page from kernel DMA buffer to user space */
  memcopy((void *)logicalAddr, (void *)dmaBuf, PAGESIZE);

  /* 12. Return device status code to user */
  excState->s_v0 = (result == READY) ? result : -result;

  /* 13. Resume user process */
  switchContext(excState);
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
