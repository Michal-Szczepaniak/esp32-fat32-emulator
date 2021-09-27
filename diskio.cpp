#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>

#include "ff.h"
#include "diskio.h"
#include "ffconf.h"

#include "main.h"

#if 1
#define IO_TRACE
#endif

static DiskMap* _disk = nullptr;
static uint64_t _diskSize = 0;

void disk_image_array_init(DiskMap* disk, uint64_t diskSize) {
    _disk = disk;
    _diskSize = diskSize;
}

DSTATUS disk_initialize(BYTE drive) {
#ifdef IO_TRACE
    printf("disk_initialize\n");
#endif

    if (drive)
        return STA_NOINIT;            /* Supports only single drive */

    if (_disk == nullptr) {
        return STA_NOINIT;
    }

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
    printf("disk_read(%d, %d): ", (int)sector, count);
#endif

    if (_disk->find(sector) == _disk->end()) {
#ifdef IO_TRACE
        printf("ERROR! (1)\n");
#endif
        return RES_ERROR;
    }

    for (int i = 0; i < count; i++) {
        for (int j = 0; j < FF_MAX_SS; j++) {
            SectorMap* sectorMap = _disk->at(sector);
            if (sectorMap->find((FF_MAX_SS*i)+j) == sectorMap->end()) buff[(FF_MAX_SS*i)+j] = '\0';
            else buff[(FF_MAX_SS*i)+j] = _disk->at(sector)->at(j);
        }
    }

#ifdef IO_TRACE
    printf("OK\n");
#endif

    return RES_OK;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
#ifdef IO_TRACE
    printf("disk_write(%d, %d): ", (int)sector, count);
#endif

    bool nonNull = false;
    for (int i = 0; i < FF_MAX_SS*count; i++) {
      if (buff[i] != '\0') {
        nonNull = true;
        break;
      }
    }
    if (!nonNull) return RES_OK;

#ifdef IO_TRACE
    printf("checking if contains sector\n");
#endif
    if (_disk->find(sector) == _disk->end()) {
#ifdef IO_TRACE
        printf("creating and assigning sector\n");
#endif
        for (int i = 0; i < count; i++)
            _disk->emplace(sector+i, new SectorMap);
    }

    for (int i = 0; i < count; i++) {
        for (int j = 0; j < FF_MAX_SS; j++) {
            if (buff[(FF_MAX_SS*i)+j] != '\0')
                _disk->at(sector+i)->emplace(j, buff[(FF_MAX_SS*i)+j]);
        }
    }


#ifdef IO_TRACE
    printf("OK\n");
#endif

    return RES_OK;
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
            DWORD nrSectors = _diskSize / FF_MAX_SS;
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
