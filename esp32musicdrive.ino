#include "USB.h"
#include "USBMSC.h"
#include <sstream>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <SPI.h>
#include "SD.h"
#include "ff.h"

#define HWSerial Serial

USBMSC MSC;

WiFiMulti WiFiMulti;
WiFiClient client;

enum RequestType {
    List,
    Get
};

struct FileInfo {
    uint32_t id;
    std::string name;
    uint64_t size;
    uint32_t sector;
};

std::vector<FileInfo> files;
bool verbose = false;
FATFS Fatfs;
MKFS_PARM opt = { FM_FAT32 };
BYTE _buff[512];
uint8_t _tempBuff[512];
String pendingRequest = "list /";
int pendingRequestType = RequestType::List;


static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buff, uint32_t buffSize) {
    if (verbose) HWSerial.printf("MSC WRITE: lba: %u, offset: %u, bufsize: %u\n", lba, offset, buffSize);
    bool res = true;

    if (buffSize < 512) {
        uint8_t* newBuff = (uint8_t*)malloc(sizeof(uint8_t) * 512);
        res = SD.read(newBuff, lba);
        if (!res) return 0;
        memcpy(newBuff + offset, buff, buffSize);
        res = SD.write(newBuff, lba);
        free(newBuff);
        if (!res) return 0;
    } else {
        res = SD.write(buff, lba);
        if (!res) return 0;
    }

    return buffSize;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void* buff, uint32_t buffSize) {
    if (verbose) HWSerial.printf("MSC READ: lba: %u, offset: %u, bufsize: %u\n", lba, offset, buffSize);
    bool res = true;

    for (FileInfo  file : files) {
        if (lba >= file.sector && lba <= ceil(file.size / 512.)) {
            Serial.printf("Reading file: %s Sector: %d\n", file.name.c_str(), lba - file.sector);
            if (buffSize == 512) {
                Serial.printf("get %d %d\n", file.id, lba - file.sector);
                client.printf("get %d %d\n", 0, 0);
                int maxloops = 0;

                while (client.available() < 512 && maxloops < 1000) {
                    maxloops++;
                    delay(1);
                }
                Serial.println("Available");

                if (client.available() == 512) {
                    Serial.printf("Reading response: %d %d\n", client.available(), ESP.getFreeHeap());
                    BYTE _tmp[512];
                    client.readBytes(_tmp, 512);
                    Serial.println("readbytes");
//                    for (int i = 0; i < 512; i++) {
//                        ((uint8_t*)buff)[i] = _tempBuff[i];
//                    }
//                    Serial.println("memcpy");
                }

                Serial.printf("Left bytes: %d\n", client.available());
            } else {
                Serial.println("SMALL BUFFSIZE READ");
            }
            return buffSize;
        }
    }

    if (buffSize < 512) {
        uint8_t* newBuff = (uint8_t*)malloc(sizeof(uint8_t) * 512);
        res = SD.read(newBuff, lba);
        if (!res) return 0;
        memcpy(buff, newBuff + offset, buffSize);
        free(newBuff);
    } else {
        res = SD.read((uint8_t*)buff, lba);
        if (!res) return 0;
    }

    return buffSize;
}

static bool onStartStop(uint8_t power_condition, bool start, bool load_eject) {
    HWSerial.printf("MSC START/STOP: power: %u, start: %u, eject: %u\n", power_condition, start, load_eject);
    return true;
}

static void usbEventCallback(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == ARDUINO_USB_EVENTS) {
        arduino_usb_event_data_t * data = (arduino_usb_event_data_t*)event_data;
        switch (event_id) {
            case ARDUINO_USB_STARTED_EVENT:
                HWSerial.println("USB PLUGGED");
                break;
            case ARDUINO_USB_STOPPED_EVENT:
                HWSerial.println("USB UNPLUGGED");
                break;
            case ARDUINO_USB_SUSPEND_EVENT:
                HWSerial.printf("USB SUSPENDED: remote_wakeup_en: %u\n", data->suspend.remote_wakeup_en);
                break;
            case ARDUINO_USB_RESUME_EVENT:
                HWSerial.println("USB RESUMED");
                break;

            default:
                break;
        }
    }
}

void setup() {
    HWSerial.begin(115200);
    HWSerial.setDebugOutput(true);

    SD.begin();

    f_mount(&Fatfs, "", 0);

//    Serial.println("Creating fat file system");
//    if (f_mkfs("", &opt, _buff, sizeof _buff) != FR_OK)
//        Serial.printf("Error making fat file system\n");
//
//    Serial.println("Created fat file system");

    USB.onEvent(usbEventCallback);
    MSC.vendorID("ESP32");//max 8 chars
    MSC.productID("USB_MSC");//max 16 chars
    MSC.productRevision("1.0");//max 4 chars
    MSC.onStartStop(onStartStop);
    MSC.onRead(onRead);
    MSC.onWrite(onWrite);
    MSC.mediaPresent(false);
    MSC.begin(SD.size() / 512, FF_MAX_SS);
    USB.begin();

    WiFiMulti.addAP("Everyone", "78901234");

    while (WiFiMulti.run() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
}

unsigned long resend = 0;

void loop() {

    if (!client.connected() && !client.connect("192.168.69.3", 12345)) {
        Serial.println("Connection failed.");
        Serial.println("Waiting 5 seconds before retrying...");
        delay(5000);
        return;
    }

    //    if (millis() - resend > 500000) pendingRequest = "list /";

    if (pendingRequest != "") {
        resend = millis();
        client.println(pendingRequest);
        int maxloops = 0;

        while (!client.available() && maxloops < 1000) {
            maxloops++;
            delay(1);
        }

        if (client.available() > 0) {
            if (pendingRequestType == RequestType::List) {
                String line;
                MSC.mediaPresent(false);
                files.clear();
                int id = 0;
                while (client.available() > 0) {
                    line = client.readStringUntil('\n');
                    FileInfo info;
                    int splitIndex = line.indexOf('\0');
                    info.id = id++;
                    info.name = line.substring(0, splitIndex).c_str();
                    info.size = strtoull(line.substring(splitIndex + 1, line.length()).c_str(), NULL, 10);
                    Serial.printf("Name: %s Size: %llu\n", info.name.c_str(), info.size);
                    files.emplace_back(info);
                    if (line.indexOf("\r") != -1) break;
                }

                for (FileInfo &file : files) {
                    FIL f_out;
                    Serial.printf("Creating file: %s\n", getFatFileName(file.name).c_str());
                    FRESULT res = f_open(&f_out, getFatFileName(file.name).c_str(), FA_CREATE_ALWAYS | FA_WRITE);
                    if (res != FR_OK) {
                        Serial.printf("Error creating file: %d\n", res);
                        continue;
                    }
                    Serial.printf("Created file: %d\n", res);

                    UINT bw;
                    res = f_expand(&f_out, static_cast<uint32_t>(file.size), 1);
                    Serial.printf("Expanded file res: %d\n", res);

                    f_write(&f_out, "\0", 1, &bw);

                    file.sector = f_out.sect;

                    f_close(&f_out);
                }

                MSC.mediaPresent(true);
            }
            pendingRequest = "";

        } else {
            Serial.println("client.available() timed out ");
        }
    }
}

FileInfo splitFile(std::string data) {
    FileInfo info;
    uint32_t splitIndex = data.find_last_of('.');
    Serial.printf("Data: %s\n", data.c_str());
    info.name = data.substr(0, splitIndex);
    info.size = static_cast<int>(strtoll(data.substr(splitIndex + 1, data.size()).c_str(), NULL, 10));
    Serial.printf("Data: %s\n", data.c_str());
    Serial.printf("Original size: %lld\nSize: %d\n", strtoll(data.substr(splitIndex + 1, data.size()).c_str(), NULL, 10), static_cast<int>(strtoll(data.substr(splitIndex + 1, data.size()).c_str(), NULL, 10)));
    return info;
}

std::string getFatFileName(std::string fileName) {
    std::string name, ext;
    uint32_t dotIndex = fileName.find_last_of('.');
    if (dotIndex == std::string::npos) {
        name = fileName;
        ext = "";
    } else {
        name = fileName.substr(0, dotIndex);
        ext = fileName.substr(dotIndex, fileName.size());
    }

    name = name.substr(0, (name.size() > (255 - ext.size()) ? (255 - ext.size()) : name.size()));

    return name + ext;
}
