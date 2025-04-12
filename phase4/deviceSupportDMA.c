#include "../h/deviceSupportDMA.h"

#include "../h/initProc.h"
#include "../h/scheduler.h"
#include "../h/sysSupport.h"
#include "../h/vmSupport.h"
#include "umps3/umps/libumps.h"

HIDDEN void memcopy(void *dest, const void *src, unsigned int size) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;

  /* Copy byte-by-byte from `src` to `dest` */
  unsigned int i;
  for (i = 0; i < size; i++) {
    d[i] = s[i];
  }
}

/*
 * Perform a disk I/O operation (read or write) on the specified device and
 * sector. Handles DMA setup, user/kernel data copy, and disk I/O with semaphore
 * protection.
 *
 * Parameters:
 *   excState   - Saved exception state with syscall arguments
 *   sup        - Support structure for the calling U-proc
 *   op         - Operation type (DISK_READBLK or DISK_WRITEBLK)
 */
HIDDEN void diskOperation(state_t *excState, support_t *sup, unsigned int op) {
  memaddr logicalAddr = excState->s_a1;
  unsigned int diskNum = excState->s_a2;
  unsigned int sectorNum = excState->s_a3;

  /* Validate that the logical address lies fully within KUSEG */
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

  /* Translate sector number to cylinder, head, sector */
  unsigned int cyl = sectorNum / (maxHead * maxSect);
  unsigned int rem = sectorNum % (maxHead * maxSect);
  unsigned int head = rem / maxSect;
  unsigned int sect = rem % maxSect;

  /* Compute physical DMA buffer address for this disk */
  memaddr dmaBuf = DISK_DMA_BASE + diskNum * PAGESIZE;

  /* Gain exclusive access to device register and DMA buffer */
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
    /* Release semaphore and return error on seek failure */
    SYSCALL(VERHOGEN, (int)&supportDeviceSem[devIdx], 0, 0);
    excState->s_v0 = -result;
    switchContext(excState);
  }

  /* Set DMA buffer address */
  disk->d_data0 = dmaBuf;

  /* Perform read or write operation */
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

  /* Resume user process */
  switchContext(excState);
}

/* Shared syscall implementation for SYS16 (Flash_Put) and SYS17 (Flash_Get).
 * Handles DMA setup, user/kernel data copy, and flash I/O.
 */
HIDDEN void sysFlashOperation(state_t *excState, support_t *sup,
                              unsigned int op) {
  memaddr logicalAddr = excState->s_a1;
  unsigned int flashNum = excState->s_a2;
  unsigned int blockNum = excState->s_a3;

  /* Validate that the logical address lies fully within KUSEG */
  if (!isValidAddr(logicalAddr) || !isValidAddr(logicalAddr + PAGESIZE - 1)) {
    programTrapHandler(sup);
  }

  /* Validate flash device number is within valid range [0..7] */
  if (flashNum >= DEVPERINT) {
    programTrapHandler(sup);
  }

  /* Validate block number is not within the backing store region (0..31).
   * TODO: If U-procs are switched to use DISK0 as backing store instead of
   * flash, this check can be relaxed to allow blockNum >= 0.
   */
  int devIdx = (FLASHINT - DISKINT) * DEVPERINT + flashNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flash = &busRegArea->devreg[devIdx];
  unsigned int maxBlock = flash->d_data1;

  if (blockNum < 32 || blockNum >= maxBlock) {
    programTrapHandler(sup);
  }

  /* Compute DMA buffer address in kernel memory for the flash device */
  memaddr dmaBuf = FLASH_DMA_BASE + flashNum * PAGESIZE;

  /* Gain exclusive access to the device register and DMA buffer */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[devIdx], 0, 0);

  if (op == FLASH_WRITEBLK) {
    /* If writing: copy one page from user space to kernel DMA buffer */
    memcopy((void *)dmaBuf, (void *)logicalAddr, PAGESIZE);
  }

  /* Set DMA buffer physical address in flash register */
  flash->d_data0 = dmaBuf;

  /* Issue command to flash device with interrupts disabled */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  flash->d_command = (blockNum << BYTELEN) | op;
  int result = SYSCALL(WAITIO, FLASHINT, flashNum, 0);
  setSTATUS(status);

  if (result == READY) {
    excState->s_v0 = result;

    if (op == FLASH_READBLK) {
      /* If reading: copy one page from DMA buffer into user space */
      memcopy((void *)logicalAddr, (void *)dmaBuf, PAGESIZE);
    }
  } else {
    excState->s_v0 = -result;
  }

  /* Release device semaphore */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[devIdx], 0, 0);

  /* Resume user process */
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

/* SYS16 */
void sysFlashWrite(state_t *excState, support_t *sup) {
  sysFlashOperation(excState, sup, FLASH_WRITEBLK);
}

/* SYS17 */
void sysFlashRead(state_t *excState, support_t *sup) {
  sysFlashOperation(excState, sup, FLASH_READBLK);
}

/*
 * Perform a flash I/O operation (READ or WRITE) on the specified device and
 * block. Used by the Pager to interact with the backing store.
 *
 * Parameters:
 *   - flashNum: Flash device number in [0..7]
 *   - blockNum: Flash block number (must be valid)
 *   - frameAddr: Physical RAM address for the 4KB page (DMA buffer)
 *   - op: Either FLASH_READBLK or FLASH_WRITEBLK
 *
 * Returns:
 *   - OK (0) if I/O completed successfully
 *   - ERR (-1) if the I/O failed (device not installed or other error)
 */
int flashOperation(unsigned int flashNum, unsigned int blockNum,
                   memaddr frameAddr, unsigned int op) {
  /* Get device register for the target flash device */
  unsigned devIdx = (FLASHINT - DISKINT) * DEVPERINT + flashNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flash = &busRegArea->devreg[devIdx];

  /* Gain exclusive access to the device register */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[devIdx], 0, 0);

  /* Set DMA buffer physical address in flash register */
  flash->d_data0 = frameAddr;

  /* Issue command to flash device with interrupts disabled */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  flash->d_command = (blockNum << BYTELEN) | op;
  int result = SYSCALL(WAITIO, FLASHINT, flashNum, 0);
  setSTATUS(status);

  /* Release device semaphore */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[devIdx], 0, 0);

  return (result == READY) ? OK : ERR;
}
