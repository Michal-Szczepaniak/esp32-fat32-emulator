#include "USB.h"
#include "USBMSC.h"
#include "main.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define HWSerial Serial0
#define USBSerial Serial
#else
#define HWSerial Serial
USBCDC USBSerial;
#endif

USBMSC MSC;

bool verbose = false;
FATFS Fatfs;
DiskMap* disk = new DiskMap;
uint64_t fileSize = 17179869184;
BYTE buff[512];

static int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buff, uint32_t buffSize) {
  if (verbose) HWSerial.printf("MSC WRITE: lba: %u, offset: %u, bufsize: %u\n", lba, offset, buffSize);
  return buffSize;

  if (disk->find(lba) == disk->end()) {
    if (verbose) HWSerial.printf("creating and assigning sector\n");
    disk->emplace(lba, new SectorMap);
  }

  SectorMap* sectorMap = disk->at(lba);
  for (int i = offset, buffHead = 0; i < buffSize; i++, buffHead++) {
    if (buff[buffHead] != '\0')
      sectorMap->emplace(i, buff[buffHead]);
    else sectorMap->erase(i);
  }

  return buffSize;
}

static int32_t onRead(uint32_t lba, uint32_t offset, void* buff, uint32_t buffSize) {
  if (verbose) HWSerial.printf("MSC READ: lba: %u, offset: %u, bufsize: %u\n", lba, offset, buffSize);

  if (disk->find(lba) == disk->end()) {
    memset(buff, '\0', buffSize);
    return buffSize;
  }

  SectorMap* sectorMap = disk->at(lba);
  for (int i = offset, buffHead = 0; i < buffSize; i++, buffHead++) {
    if (sectorMap->find(i) == sectorMap->end()) ((uint8_t*)buff)[buffHead] = '\0';
    else ((uint8_t*)buff)[buffHead] = sectorMap->at(i);
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
  USBSerial.begin();
  HWSerial.begin(115200);
  HWSerial.setDebugOutput(true);

  disk_image_array_init(disk, fileSize);

  f_mount(&Fatfs, "", 0);

  if (verbose) {
    printf("Creating fat file system\n");
  }

  MKFS_PARM opt = { FM_FAT32 };
  if (f_mkfs("", &opt, buff, sizeof buff) != FR_OK)
    HWSerial.printf("Error making fat file system\n");

  if (verbose) {
    HWSerial.printf("Done\n");
  }

  USB.onEvent(usbEventCallback);
  MSC.vendorID("ESP32");//max 8 chars
  MSC.productID("USB_MSC");//max 16 chars
  MSC.productRevision("1.0");//max 4 chars
  MSC.onStartStop(onStartStop);
  MSC.onRead(onRead);
  MSC.onWrite(onWrite);
  MSC.mediaPresent(true);
  MSC.begin(33554432, 512);
  USB.begin();
}

void loop() {
}
