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
#include "esp_system.h"
extern "C" {
    char CRC7(const char* data, int length);
    unsigned short CRC16(const char* data, int length);
}

typedef enum {
    GO_IDLE_STATE           = 0,
    SEND_OP_COND            = 1,
    SEND_CID                = 2,
    SEND_RELATIVE_ADDR      = 3,
    SEND_SWITCH_FUNC        = 6,
    SEND_IF_COND            = 8,
    SEND_CSD                = 9,
    STOP_TRANSMISSION       = 12,
    SEND_STATUS             = 13,
    SET_BLOCKLEN            = 16,
    READ_BLOCK_SINGLE       = 17,
    READ_BLOCK_MULTIPLE     = 18,
    SEND_NUM_WR_BLOCKS      = 22,
    SET_WR_BLK_ERASE_COUNT  = 23,
    WRITE_BLOCK_SINGLE      = 24,
    WRITE_BLOCK_MULTIPLE    = 25,
    APP_OP_COND             = 41,
    APP_CLR_CARD_DETECT     = 42,
    APP_CMD                 = 55,
    READ_OCR                = 58,
    CRC_ON_OFF              = 59
} ardu_sdcard_command_t;

typedef struct {
    uint8_t ssPin;
    SPIClass * spi;
    int frequency;
    char * base_path;
    sdcard_type_t type;
    unsigned long sectors;
    bool supports_crc;
    int status;
} ardu_sdcard_t;

static ardu_sdcard_t* s_card = nullptr;

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_ERROR
const char * fferr2str[] = {
    "(0) Succeeded",
    "(1) A hard error occurred in the low level disk I/O layer",
    "(2) Assertion failed",
    "(3) The physical drive cannot work",
    "(4) Could not find the file",
    "(5) Could not find the path",
    "(6) The path name format is invalid",
    "(7) Access denied due to prohibited access or directory full",
    "(8) Access denied due to prohibited access",
    "(9) The file/directory object is invalid",
    "(10) The physical drive is write protected",
    "(11) The logical drive number is invalid",
    "(12) The volume has no work area",
    "(13) There is no valid FAT volume",
    "(14) The f_mkfs() aborted due to any problem",
    "(15) Could not get a grant to access the volume within defined period",
    "(16) The operation is rejected according to the file sharing policy",
    "(17) LFN working buffer could not be allocated",
    "(18) Number of open files > FF_FS_LOCK",
    "(19) Given parameter is invalid"
};
#endif

/*
    SD SPI
 * */

bool sdWait(int timeout)
{
    char resp;
    uint32_t start = millis();

    do {
        resp = s_card->spi->transfer(0xFF);
    } while (resp == 0x00 && (millis() - start) < (unsigned int)timeout);

    if (!resp) {
        Serial.println("Wait Failed");
    }
    return (resp > 0x00);
}

void sdStop()
{
    s_card->spi->write(0xFD);
}

void sdDeselectCard()
{
    digitalWrite(s_card->ssPin, HIGH);
}

bool sdSelectCard()
{
    digitalWrite(s_card->ssPin, LOW);
    bool s = sdWait(300);
    if (!s) {
        log_e("Select Failed");
        digitalWrite(s_card->ssPin, HIGH);
        return false;
    }
    return true;
}

char sdCommand(char cmd, unsigned int arg, unsigned int* resp)
{
    char token;

    for (int f = 0; f < 3; f++) {
        if (cmd == SEND_NUM_WR_BLOCKS || cmd == SET_WR_BLK_ERASE_COUNT || cmd == APP_OP_COND || cmd == APP_CLR_CARD_DETECT) {
            token = sdCommand(APP_CMD, 0, NULL);
            sdDeselectCard();
            if (token > 1) {
                break;
            }
            if (!sdSelectCard()) {
                token = 0xFF;
                break;
            }
        }

        char cmdPacket[7];
        cmdPacket[0] = cmd | 0x40;
        cmdPacket[1] = arg >> 24;
        cmdPacket[2] = arg >> 16;
        cmdPacket[3] = arg >> 8;
        cmdPacket[4] = arg;
        if (s_card->supports_crc || cmd == GO_IDLE_STATE || cmd == SEND_IF_COND) {
            cmdPacket[5] = (CRC7(cmdPacket, 5) << 1) | 0x01;
        } else {
            cmdPacket[5] = 0x01;
        }
        cmdPacket[6] = 0xFF;

        s_card->spi->writeBytes((uint8_t*)cmdPacket, (cmd == STOP_TRANSMISSION) ? 7 : 6);

        for (int i = 0; i < 9; i++) {
            token = s_card->spi->transfer(0xFF);
            if (!(token & 0x80)) {
                break;
            }
        }

        if (token == 0xFF) {
            Serial.println("no token received");
            sdDeselectCard();
            delay(100);
            sdSelectCard();
            continue;
        } else if (token & 0x08) {
            Serial.println("crc error");
            sdDeselectCard();
            delay(100);
            sdSelectCard();
            continue;
        } else if (token > 1) {
            Serial.printf("token error [%u] 0x%x\n", cmd, token);
            break;
        }

        if (cmd == SEND_STATUS && resp) {
            *resp = s_card->spi->transfer(0xFF);
        } else if ((cmd == SEND_IF_COND || cmd == READ_OCR) && resp) {
            *resp = s_card->spi->transfer32(0xFFFFFFFF);
        }

        break;
    }
    if (token == 0xFF) {
        log_e("Card Failed! cmd: 0x%02x", cmd);
        s_card->status = STA_NOINIT;
    }
    return token;
}

bool sdReadBytes(char* buffer, int length)
{
    char token;
    unsigned short crc;

    uint32_t start = millis();
    do {
        token = s_card->spi->transfer(0xFF);
    } while (token == 0xFF && (millis() - start) < 500);

    if (token != 0xFE) {
        return false;
    }

    s_card->spi->transferBytes(NULL, (uint8_t*)buffer, length);
    crc = s_card->spi->transfer16(0xFFFF);
    return (!s_card->supports_crc || crc == CRC16(buffer, length));
}

char sdWriteBytes(const char* buffer, char token)
{
    unsigned short crc = (s_card->supports_crc) ? CRC16(buffer, 512) : 0xFFFF;
    if (!sdWait(500)) {
        return false;
    }

    s_card->spi->write(token);
    s_card->spi->writeBytes((uint8_t*)buffer, 512);
    s_card->spi->write16(crc);
    return (s_card->spi->transfer(0xFF) & 0x1F);
}

/*
    SPI SDCARD Communication
 * */

char sdTransaction(char cmd, unsigned int arg, unsigned int* resp)
{
    if (!sdSelectCard()) {
        return 0xFF;
    }
    char token = sdCommand(cmd, arg, resp);
    sdDeselectCard();
    return token;
}

bool sdReadSector(char* buffer, unsigned long long sector)
{
    for (int f = 0; f < 3; f++) {
        if (!sdSelectCard()) {
            return false;
        }
        if (!sdCommand(READ_BLOCK_SINGLE, (s_card->type == CARD_SDHC) ? sector : sector << 9, NULL)) {
            bool success = sdReadBytes(buffer, 512);
            sdDeselectCard();
            if (success) {
                return true;
            }
        } else {
            break;
        }
    }
    sdDeselectCard();
    return false;
}

bool sdReadSectors(char* buffer, unsigned long long sector, int count)
{
    for (int f = 0; f < 3;) {
        if (!sdSelectCard()) {
            return false;
        }

        if (!sdCommand(READ_BLOCK_MULTIPLE, (s_card->type == CARD_SDHC) ? sector : sector << 9, NULL)) {
            do {
                if (!sdReadBytes(buffer, 512)) {
                    f++;
                    break;
                }

                sector++;
                buffer += 512;
                f = 0;
            } while (--count);

            if (sdCommand(STOP_TRANSMISSION, 0, NULL)) {
                log_e("command failed");
                break;
            }

            sdDeselectCard();
            if (count == 0) {
                return true;
            }
        } else {
            break;
        }
    }
    sdDeselectCard();
    return false;
}

bool sdWriteSector(const char* buffer, unsigned long long sector)
{
    for (int f = 0; f < 3; f++) {
        if (!sdSelectCard()) {
            return false;
        }
        if (!sdCommand(WRITE_BLOCK_SINGLE, (s_card->type == CARD_SDHC) ? sector : sector << 9, NULL)) {
            char token = sdWriteBytes(buffer, 0xFE);
            sdDeselectCard();

            if (token == 0x0A) {
                continue;
            } else if (token == 0x0C) {
                return false;
            }

            unsigned int resp;
            if (sdTransaction(SEND_STATUS, 0, &resp) || resp) {
                return false;
            }
            return true;
        } else {
            break;
        }
    }
    sdDeselectCard();
    return false;
}

bool sdWriteSectors(const char* buffer, unsigned long long sector, int count)
{
    char token;
    const char* currentBuffer = buffer;
    unsigned long long currentSector = sector;
    int currentCount = count;

    for (int f = 0; f < 3;) {
        if (s_card->type != CARD_MMC) {
            if (sdTransaction(SET_WR_BLK_ERASE_COUNT, currentCount, NULL)) {
                return false;
            }
        }

        if (!sdSelectCard()) {
            return false;
        }

        if (!sdCommand(WRITE_BLOCK_MULTIPLE, (s_card->type == CARD_SDHC) ? currentSector : currentSector << 9, NULL)) {
            do {
                token = sdWriteBytes(currentBuffer, 0xFC);
                if (token != 0x05) {
                    f++;
                    break;
                }
                currentBuffer += 512;
                f = 0;
            } while (--currentCount);

            if (!sdWait(500)) {
                break;
            }

            if (currentCount == 0) {
                sdStop();
                sdDeselectCard();

                unsigned int resp;
                if (sdTransaction(SEND_STATUS, 0, &resp) || resp) {
                    return false;
                }
                return true;
            } else {
                if (sdCommand(STOP_TRANSMISSION, 0, NULL)) {
                    break;
                }

                if (token == 0x0A) {
                    sdDeselectCard();
                    unsigned int writtenBlocks = 0;
                    if (s_card->type != CARD_MMC && sdSelectCard()) {
                        if (!sdCommand(SEND_NUM_WR_BLOCKS, 0, NULL)) {
                            char acmdData[4];
                            if (sdReadBytes(acmdData, 4)) {
                                writtenBlocks = acmdData[0] << 24;
                                writtenBlocks |= acmdData[1] << 16;
                                writtenBlocks |= acmdData[2] << 8;
                                writtenBlocks |= acmdData[3];
                            }
                        }
                        sdDeselectCard();
                    }
                    currentBuffer = buffer + (writtenBlocks << 9);
                    currentSector = sector + writtenBlocks;
                    currentCount = count - writtenBlocks;
                    continue;
                } else {
                    break;
                }
            }
        } else {
            break;
        }
    }
    sdDeselectCard();
    return false;
}

unsigned long sdGetSectorsCount()
{
    for (int f = 0; f < 3; f++) {
        if (!sdSelectCard()) {
            return false;
        }

        if (!sdCommand(SEND_CSD, 0, NULL)) {
            char csd[16];
            bool success = sdReadBytes(csd, 16);
            sdDeselectCard();
            if (success) {
                if ((csd[0] >> 6) == 0x01) {
                    unsigned long size = (
                                             ((unsigned long)(csd[7] & 0x3F) << 16)
                                             | ((unsigned long)csd[8] << 8)
                                             | csd[9]
                                         ) + 1;
                    return size << 10;
                }
                unsigned long size = (
                                         ((unsigned long)(csd[6] & 0x03) << 10)
                                         | ((unsigned long)csd[7] << 2)
                                         | ((csd[8] & 0xC0) >> 6)
                                     ) + 1;
                size <<= ((
                              ((csd[9] & 0x03) << 1)
                              | ((csd[10] & 0x80) >> 7)
                          ) + 2);
                size <<= (csd[5] & 0x0F);
                return size >> 9;
            }
        } else {
            break;
        }
    }

    sdDeselectCard();
    return 0;
}


namespace
{

struct AcquireSPI
{
        ardu_sdcard_t *card;
        explicit AcquireSPI(ardu_sdcard_t* card)
            : card(card)
        {
            card->spi->beginTransaction(SPISettings(card->frequency, MSBFIRST, SPI_MODE0));
        }
        AcquireSPI(ardu_sdcard_t* card, int frequency)
            : card(card)
        {
            card->spi->beginTransaction(SPISettings(frequency, MSBFIRST, SPI_MODE0));
        }
        ~AcquireSPI()
        {
            card->spi->endTransaction();
        }
    private:
        AcquireSPI(AcquireSPI const&);
        AcquireSPI& operator=(AcquireSPI const&);
};

}

DSTATUS sd_initialize()
{
    char token;
    unsigned int resp;
    unsigned int start;

    if (!(s_card->status & STA_NOINIT)) {
        return s_card->status;
    }

    AcquireSPI card_locked(s_card, 400000);

    digitalWrite(s_card->ssPin, HIGH);
    for (uint8_t i = 0; i < 20; i++) {
        s_card->spi->transfer(0XFF);
    }

    if (sdTransaction(GO_IDLE_STATE, 0, NULL) != 1) {
        Serial.println("GO_IDLE_STATE failed");
        goto unknown_card;
    }

    token = sdTransaction(CRC_ON_OFF, 1, NULL);
    if (token == 0x5) {
        //old card maybe
        s_card->supports_crc = false;
    } else if (token != 1) {
        Serial.printf("CRC_ON_OFF failed: %u\n", token);
        goto unknown_card;
    }

    if (sdTransaction(SEND_IF_COND, 0x1AA, &resp) == 1) {
        if ((resp & 0xFFF) != 0x1AA) {
            Serial.printf("SEND_IF_COND failed: %03X\n", resp & 0xFFF);
            goto unknown_card;
        }

        if (sdTransaction(READ_OCR, 0, &resp) != 1 || !(resp & (1 << 20))) {
            Serial.printf("READ_OCR failed: %X\n", resp);
            goto unknown_card;
        }

        start = millis();
        do {
            token = sdTransaction(APP_OP_COND, 0x40100000, NULL);
        } while (token == 1 && (millis() - start) < 1000);

        if (token) {
            Serial.printf("APP_OP_COND failed: %u\n", token);
            goto unknown_card;
        }

        if (!sdTransaction( READ_OCR, 0, &resp)) {
            if (resp & (1 << 30)) {
                s_card->type = CARD_SDHC;
            } else {
                s_card->type = CARD_SD;
            }
        } else {
            Serial.printf("READ_OCR failed: %X\n", resp);
            goto unknown_card;
        }
    } else {
        if (sdTransaction(READ_OCR, 0, &resp) != 1 || !(resp & (1 << 20))) {
            Serial.printf("READ_OCR failed: %X\n", resp);
            goto unknown_card;
        }

        start = millis();
        do {
            token = sdTransaction(APP_OP_COND, 0x100000, NULL);
        } while (token == 0x01 && (millis() - start) < 1000);

        if (!token) {
            s_card->type = CARD_SD;
        } else {
            start = millis();
            do {
                token = sdTransaction(SEND_OP_COND, 0x100000, NULL);
            } while (token != 0x00 && (millis() - start) < 1000);

            if (token == 0x00) {
                s_card->type = CARD_MMC;
            } else {
                Serial.printf("SEND_OP_COND failed: %u\n", token);
                goto unknown_card;
            }
        }
    }

    if (s_card->type != CARD_MMC) {
        if (sdTransaction(APP_CLR_CARD_DETECT, 0, NULL)) {
            Serial.println("APP_CLR_CARD_DETECT failed");
            goto unknown_card;
        }
    }

    if (s_card->type != CARD_SDHC) {
        if (sdTransaction(SET_BLOCKLEN, 512, NULL) != 0x00) {
            Serial.println("SET_BLOCKLEN failed");
            goto unknown_card;
        }
    }
    
    s_card->sectors = sdGetSectorsCount();

    if (s_card->frequency > 25000000) {
        s_card->frequency = 25000000;
    }
    s_card->status &= ~STA_NOINIT;
    return s_card->status;

unknown_card:
    s_card->type = CARD_UNKNOWN;
    return s_card->status;
}

DSTATUS sd_status()
{
    return s_card->status;
}

bool sd_read(uint8_t* buffer, uint32_t sector)
{
    if (s_card->status & STA_NOINIT) {
        return RES_NOTRDY;
    }

    AcquireSPI lock(s_card);

    return sdReadSector((char*)buffer, sector);
}

bool sd_write(uint8_t* buffer, uint32_t sector)
{
    if (s_card->status & STA_NOINIT) {
        return false;
    }

    if (s_card->status & STA_PROTECT) {
        return false;
    }

    AcquireSPI lock(s_card);

    return sdWriteSector((const char*)buffer, sector);
}

DRESULT sd_ioctl(uint8_t cmd, void* buff)
{
    switch (cmd) {
        case CTRL_SYNC:
            {
                AcquireSPI lock(s_card);
                if (sdSelectCard()) {
                    sdDeselectCard();
                    return RES_OK;
                }
            }
            return RES_ERROR;
        case GET_SECTOR_COUNT:
            *((unsigned long*) buff) = s_card->sectors;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *((WORD*) buff) = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *((uint32_t*)buff) = 1;
            return RES_OK;
    }
    return RES_PARERR;
}

/*
    Public methods
 * */

uint8_t sdcard_uninit()
{
    if (s_card == NULL) {
        return 1;
    }
    sdTransaction(GO_IDLE_STATE, 0, NULL);
    esp_err_t err = ESP_OK;
    if (s_card->base_path) {
        free(s_card->base_path);
    }
    free(s_card);
    s_card = nullptr;
    return err;
}

bool sdcard_init(uint8_t cs, SPIClass * spi, int hz)
{
    s_card = (ardu_sdcard_t *)malloc(sizeof(ardu_sdcard_t));
    if (!s_card) {
        return false;
    }

    s_card->base_path = NULL;
    s_card->frequency = hz;
    s_card->spi = spi;
    s_card->ssPin = cs;

    s_card->supports_crc = true;
    s_card->type = CARD_NONE;
    s_card->status = STA_NOINIT;

    pinMode(s_card->ssPin, OUTPUT);
    digitalWrite(s_card->ssPin, HIGH);

    return sd_initialize();
}

uint32_t sdcard_num_sectors()
{
    if (s_card == nullptr) {
        return 0;
    }

    return s_card->sectors;
}

uint32_t sdcard_sector_size()
{
    return 512;
}

sdcard_type_t sdcard_type()
{
    if (s_card == nullptr) {
        return CARD_NONE;
    }

    return s_card->type;
}
