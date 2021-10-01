#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>
#include <Arduino.h>

#include "diskio.h"
#include "ffconf.h"
#include "sd_diskio.h"

#if 0
#define IO_TRACE
#endif

DSTATUS disk_initialize(BYTE drive) {
#ifdef IO_TRACE
    printf("disk_initialize\n");
#endif

    return 0;
}

DSTATUS disk_status(BYTE drive) {
#ifdef IO_TRACE
    printf("disk_status\n");
#endif

    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
#ifdef IO_TRACE
    printf("disk_read(%d, %d): \n", (int)sector, count);
#endif

    return (DRESULT)!sd_read(buff, sector);
}

DRESULT disk_write(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
#ifdef IO_TRACE
    printf("disk_write(%d, %d): \n", (int)sector, count);
#endif

    return (DRESULT)!sd_write(buff, sector);
}

DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer) {
    DRESULT rv = RES_ERROR;

    switch (command) {
        case (CTRL_SYNC):
            rv = RES_OK;
            break;

        case (GET_BLOCK_SIZE): {
                WORD *pW = (WORD *) buffer;
                *pW = 1;
                rv = RES_OK;
            }
            break;

        case (GET_SECTOR_COUNT): {
                DWORD nrSectors = sdcard_num_sectors();
                DWORD *pW = (DWORD *) buffer;
                *pW = nrSectors;

                rv = RES_OK;
            }
            break;

        default:
            printf("disk_ioctl: unsupported command! (%d)\n", command);
            rv = RES_PARERR;
            break;
    }

#ifdef IO_TRACE
    printf("disk_ioctl(%d): %d\n", command, rv);
#endif

    return rv;
}
