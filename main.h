#ifndef UNTITLED_MAIN_H
#define UNTITLED_MAIN_H

#include <map>
#include <cstdint>
#include "ff.h"

typedef std::map<uint32_t, uint8_t> SectorMap;
typedef std::map<uint32_t, SectorMap*> DiskMap;
void disk_image_array_init(DiskMap* disk, uint64_t diskSize);

#endif //UNTITLED_MAIN_H
