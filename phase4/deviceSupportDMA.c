#include "../h/deviceSupportDMA.h"

#include "../h/initProc.h"
#include "../h/sysSupport.h"
#include "../h/vmSupport.h"

HIDDEN void memcopy(void *dest, const void *src, unsigned int size) {
  unsigned char *d = (unsigned char *)dest;
  const unsigned char *s = (const unsigned char *)src;
  for (unsigned int i = 0; i < size; i++) {
    d[i] = s[i];
  }
}

void sysDiskPut(state_t *excState, support_t *sup) {
  memaddr logicalAddr = excState->s_a1;
  int diskNo = excState->s_a2;
  int sectorNo = excState->s_a3;

  // 1. Validate disk number
  if (diskNo < 0 || diskNo >= DEVPERINT) {
    programTrapHandler(sup);
  }

  // 2. Validate logical address
  if (!isValidAddr(logicalAddr) || !isValidAddr(logicalAddr + PAGESIZE - 1)) {
    programTrapHandler(sup);
  }

  // 3. DMA buffer base
  memaddr dmaBuf = DISK_DMA_BASE + diskNo * PAGESIZE;

  // 4. Get disk geometry from DATA1
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

  // 5. Copy from user to DMA buffer
  memcopy((void *)dmaBuf, (void *)logicalAddr, PAGESIZE);

  // 6. Lock device
  SYSCALL(PASSEREN, (int)&supportDeviceSem[diskNo], 0, 0);

  // 7. Translate sector number to (cyl, head, sect)
  int cyl = sectorNo / (maxHead * maxSect);
  int rem = sectorNo % (maxHead * maxSect);
  int head = rem / maxSect;
  int sect = rem % maxSect;

  // 8. Issue SEEK
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (cyl << BYTELEN) | SEEKCYL;
  SYSCALL(WAITIO, DISKINT, diskNo, 0);
  setSTATUS(status);

  // 9. Set DMA address
  disk->d_data0 = dmaBuf;

  // 10. Issue WRITEBLK
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (head << BYTELEN) | sect | DISK_WRITEBLK;
  int result = SYSCALL(WAITIO, DISKINT, diskNo, 0);
  setSTATUS(status);

  // 11. Unlock device
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[diskNo], 0, 0);

  // 12. Return result
  excState->s_v0 = (result == READY) ? result : -result;

  // 13. Resume U-proc
  switchContext(excState);
}

void sysDiskGet(state_t *excState, support_t *sup) {
  memaddr logicalAddr = excState->s_a1;
  int diskNo = excState->s_a2;
  int sectorNo = excState->s_a3;

  // 1. Validate diskNo: [1..7], DISK0 is backing store
  if (diskNo <= 0 || diskNo >= DEVPERINT) {
    programTrapHandler(sup);
  }

  // 2. Validate logical address
  if (!isValidAddr(logicalAddr) || !isValidAddr(logicalAddr + PAGESIZE - 1)) {
    programTrapHandler(sup);
  }

  // 3. Get device and DMA buffer
  memaddr dmaBuf = DISK_DMA_BASE + diskNo * PAGESIZE;
  devregarea_t *busRegArea = (devregarea_t *)RAMBASEADDR;
  device_t *disk =
      &busRegArea->devreg[(DISKINT - DISKINT) * DEVPERINT + diskNo];

  // 4. Read geometry and validate sectorNo
  unsigned int data1 = disk->d_data1;
  int maxCyl = GET_DISK_CYLINDER(data1);
  int maxHead = GET_DISK_HEAD(data1);
  int maxSect = GET_DISK_SECTOR(data1);
  int maxSector = maxCyl * maxHead * maxSect;

  if (sectorNo < 0 || sectorNo >= maxSector) {
    programTrapHandler(sup);
  }

  // 5. Lock device register
  SYSCALL(PASSEREN, (int)&supportDeviceSem[diskNo], 0, 0);

  // 6. Compute (cyl, head, sect)
  int cyl = sectorNo / (maxHead * maxSect);
  int rem = sectorNo % (maxHead * maxSect);
  int head = rem / maxSect;
  int sect = rem % maxSect;

  // 7. Issue SEEK
  unsigned int status = getSTATUS();
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (cyl << BYTELEN) | SEEKCYL;
  SYSCALL(WAITIO, DISKINT, diskNo, 0);
  setSTATUS(status);

  // 8. Setup DMA buffer for read
  disk->d_data0 = dmaBuf;

  // 9. Issue READBLK
  setSTATUS(status & ~STATUS_IEC);
  disk->d_command = (head << BYTELEN) | sect | DISK_READBLK;
  int result = SYSCALL(WAITIO, DISKINT, diskNo, 0);
  setSTATUS(status);

  // 10. Unlock device
  SYSCALL(VERHOGEN, (int)&supportDeviceSem[diskNo], 0, 0);

  // 11. Copy from DMA buffer to U-proc memory
  memcopy((void *)logicalAddr, (void *)dmaBuf, PAGESIZE);

  // 12. Return result
  excState->s_v0 = (result == READY) ? result : -result;

  // 13. Resume U-proc
  switchContext(excState);
}
