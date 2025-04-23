/**
 * @file deviceSupportDMA.c
 * @author Dang Truong, Loc Pham
 * @brief Implements Phase 4 DMA support: disk (SYS14/15) and flash (SYS16/17)
 * block I/O. Handles DMA buffer setup, mutual exclusion (SYS3/SYS4), low-level
 * seek and transfer (SYS5), and user-kernel data copies.
 * @date 2025-04-21
 */

#include "../h/deviceSupportDMA.h"

#include "../h/initProc.h"
#include "../h/scheduler.h"
#include "../h/sysSupport.h"
#include "../h/vmSupport.h"
#include "umps3/umps/libumps.h"

/**
 * @brief Copy a memory region byte-by-byte.
 *
 * @param dest Destination buffer.
 * @param src  Source buffer.
 * @param size Number of bytes to copy.
 */
HIDDEN void memcopy(void *dest, const void *src, unsigned int size) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;

  /* Copy byte-by-byte from `src` to `dest` */
  unsigned int i;
  for (i = 0; i < size; i++) {
    d[i] = s[i];
  }
}

/**
 * @brief Perform a disk I/O operation (read or write) on the specified device
 * and sector. Handles DMA buffer setup, user/kernel data transfer, device
 * synchronization, and sector validation.
 *
 * @param excState the saved exception state
 * @param sup the support structure of the calling U-proc
 * @param op operation type (DISK_READBLK or DISK_WRITEBLK)
 */
HIDDEN void sysDiskOperation(state_t *excState, support_t *sup,
                             unsigned int op) {
  memaddr logicalAddr = excState->s_a1;
  unsigned int diskNum = excState->s_a2;
  unsigned int sectorNum = excState->s_a3;

  /* Validate that logical address lies entirely in KUSEG */
  if (!isValidAddr(logicalAddr) || !isValidAddr(logicalAddr + PAGESIZE - 1)) {
    programTrapHandler(sup);
  }

  /*
   * Validate disk number: must be in [1..7]
   * Note: diskNum is unsigned, so a negative diskNum value is wrapped around to
   * a very large integer. Disk 0 is reserved as backing store and must not be
   * accessed by user code.
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
    excState->s_v0 = ERR;
    switchContext(excState);
  }

  /* Compute physical DMA buffer address for this disk */
  memaddr dmaBuf = DISK_DMA_BASE + diskNum * PAGESIZE;

  /* Gain exclusive access to device register and DMA buffer */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[devIdx], 0, 0);

  /* For disk write operations, copy data from user space to DMA buffer */
  if (op == DISK_WRITEBLK) {
    memcopy((void *)dmaBuf, (void *)logicalAddr, PAGESIZE);
  }

  excState->s_v0 = diskOperation(diskNum, sectorNum, dmaBuf, op);

  /* For successful disk reads, copy data from DMA buffer to user space */
  if (excState->s_v0 == READY && op == DISK_READBLK) {
    memcopy((void *)logicalAddr, (void *)dmaBuf, PAGESIZE);
  }

  /* Release device semaphore */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[devIdx], 0, 0);

  /* Resume user process */
  switchContext(excState);
}

/**
 * @brief Low-level disk operation: seek + read or write via DMA. Note that this
 * function assumes the caller has already acquired mututal exclusion.
 *
 * @param diskNum   Disk device number [1..7] (0 reserved for backing store).
 * @param sectorNum Sector index within [0..maxSector−1].
 * @param frameAddr Physical RAM address of 4KB DMA buffer.
 * @param op        DISK_READBLK or DISK_WRITEBLK.
 * @return OK (0) if I/O completed successfully; otherwise, ERR (-1)
 */
int diskOperation(unsigned int diskNum, unsigned int sectorNum,
                  memaddr frameAddr, unsigned int op) {
  unsigned int devIdx = (DISKINT - DISKINT) * DEVPERINT + diskNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *disk = &busRegArea->devreg[devIdx];

  /* Read disk geometry from DATA1 */
  unsigned int data1 = disk->d_data1;
  unsigned int maxHead = GET_DISK_HEAD(data1);
  unsigned int maxSect = GET_DISK_SECTOR(data1);

  /* Translate sector number to cylinder, head, sector */
  unsigned int cyl = sectorNum / (maxHead * maxSect);
  unsigned int rem = sectorNum % (maxHead * maxSect);
  unsigned int head = rem / maxSect;
  unsigned int sect = rem % maxSect;

  /* Issue SEEK to position disk head at the correct cylinder */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (cyl << DISK_CYL_SHIFT) | SEEKCYL;
  int result = SYSCALL(WAITIO, DISKINT, diskNum, 0);
  setSTATUS(status);

  if (result != READY) {
    /* return error on seek failure */
    return -result;
  }

  /* Set DMA buffer address */
  disk->d_data0 = frameAddr;

  /* Perform read or write operation */
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (head << DISK_HEAD_SHIFT) | (sect << DISK_SECT_SHIFT) | op;
  result = SYSCALL(WAITIO, DISKINT, diskNum, 0);
  setSTATUS(status);

  return (result == READY) ? result : -result;
}

/**
 * @brief Shared handler for SYS16/17 flash I/O. Handles DMA setup, user/kernel
 * data copy, and flash I/O.
 *
 * @param excState Saved exception state of U-proc.
 * @param sup      Pointer to U‑proc support structure.
 * @param op       FLASH_READBLK or FLASH_WRITEBLK.
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

  /* Validate block number */
  int devIdx = (FLASHINT - DISKINT) * DEVPERINT + flashNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flash = &busRegArea->devreg[devIdx];
  unsigned int maxBlock = flash->d_data1;

  if (blockNum >= maxBlock) {
    programTrapHandler(sup);
  }

  /* Compute DMA buffer address in kernel memory for the flash device */
  memaddr dmaBuf = FLASH_DMA_BASE + flashNum * PAGESIZE;

  /* Gain exclusive access to the device register and DMA buffer */
  SYSCALL(PASSEREN, (int)&supportDeviceSem[devIdx], 0, 0);

  /* If writing: copy one page from user space to kernel DMA buffer */
  if (op == FLASH_WRITEBLK) {
    memcopy((void *)dmaBuf, (void *)logicalAddr, PAGESIZE);
  }

  excState->s_v0 = flashOperation(flashNum, blockNum, dmaBuf, op);

  /* If reading: copy one page from DMA buffer into user space */
  if (excState->s_v0 == READY && op == FLASH_READBLK) {
    memcopy((void *)logicalAddr, (void *)dmaBuf, PAGESIZE);
  }

  /* Release device semaphore */
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[devIdx], 0, 0);

  /* Resume user process */
  switchContext(excState);
}

/**
 * @brief Perform a flash I/O operation (READ or WRITE) on the specified device
 * and block. Used by the Pager to interact with the backing store.
 *
 * @param flashNum flash device number in [0..7]
 * @param blockNum flash block number (must be valid)
 * @param frameAddr Physical RAM address for the 4KB page (DMA buffer)
 * @param op operation type (FLASH_READBLK or FLASH_WRITEBLK)
 * @return OK (0) if I/O completed successfully; otherwise, ERR (-1)
 */
int flashOperation(unsigned int flashNum, unsigned int blockNum,
                   memaddr frameAddr, unsigned int op) {
  /* Get device register for the target flash device */
  unsigned devIdx = (FLASHINT - DISKINT) * DEVPERINT + flashNum;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *flash = &busRegArea->devreg[devIdx];

  /* Set DMA buffer physical address in flash register */
  flash->d_data0 = frameAddr;

  /* Issue command to flash device with interrupts disabled */
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  flash->d_command = (blockNum << BYTELEN) | op;
  int result = SYSCALL(WAITIO, FLASHINT, flashNum, 0);
  setSTATUS(status);

  return (result == READY) ? result : -result;
}

/**
 * @brief SYS14: Write one page (4KB) to a disk sector
 *
 * @param excState the saved exception state
 * @param sup the support structure of the calling U-proc
 */
void sysDiskWrite(state_t *excState, support_t *sup) {
  sysDiskOperation(excState, sup, DISK_WRITEBLK);
}

/**
 * @brief SYS15: Read one page (4KB) from a disk sector
 *
 * @param excState the saved exception state
 * @param sup the support structure of the calling U-proc
 */
void sysDiskRead(state_t *excState, support_t *sup) {
  sysDiskOperation(excState, sup, DISK_READBLK);
}

/**
 * @brief SYS16: Write one 4KB page to a flash block.
 *
 * @param excState the saved exception state
 * @param sup the support structure of the calling U-proc
 */
void sysFlashWrite(state_t *excState, support_t *sup) {
  sysFlashOperation(excState, sup, FLASH_WRITEBLK);
}

/**
 * @brief SYS17: Read one 4KB page from a flash block.
 *
 * @param excState the saved exception state
 * @param sup the support structure of the calling U-proc
 */
void sysFlashRead(state_t *excState, support_t *sup) {
  sysFlashOperation(excState, sup, FLASH_READBLK);
}
