// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "sd_diskio.h"
#include "SD.h"

SDFS::SDFS() {}

bool SDFS::begin(uint8_t ssPin, SPIClass &spi, uint32_t frequency)
{
    spi.begin();

    int res = sdcard_init(ssPin, &spi, frequency);
    if (res & STA_NOINIT) {
        return false;
    }

    return true;
}

void SDFS::end()
{
    sdcard_uninit();
}

sdcard_type_t SDFS::type()
{
    return sdcard_type();
}

uint64_t SDFS::size()
{
    size_t sectors = sdcard_num_sectors();
    size_t sectorSize = sdcard_sector_size();
    return (uint64_t)sectors * sectorSize;
}

bool SDFS::read(uint8_t* buffer, uint32_t sector)
{
    return sd_read(buffer, sector);
}

bool SDFS::write(uint8_t* buffer, uint32_t sector)
{
    return sd_write(buffer, sector);
}


SDFS SD = SDFS();
