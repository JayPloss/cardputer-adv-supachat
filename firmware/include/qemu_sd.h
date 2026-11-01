#pragma once
// QEMU's ESP32-S3 has an SD controller but no SD over SPI, so the virtual card goes through
// SD_MMC. Everything above the block driver (VFS, FATFS, newlib, the heap) is the real code.
#ifdef __cplusplus
#include <SD.h>
#include <SD_MMC.h>
#include <vfs_api.h>
class QemuSd : public fs::SDMMCFS {
 public:
  QemuSd() : fs::SDMMCFS(FSImplPtr(new VFSImpl())) {}
  bool begin(gpio_num_t, SPIClass &, uint32_t) { setPins(14, 15, 2); return fs::SDMMCFS::begin("/sdcard", true); }
};
inline QemuSd qemuSd;
#define SD qemuSd
#endif
