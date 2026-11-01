#include "os.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <mbedtls/platform.h>

#include <atomic>
#include <cstring>

// The allocator underneath. On the device these are newlib's, reached through the linker's
// --wrap; the emulator supplies its device-sized heap under the same names.
extern "C" {
void *__real_malloc(size_t);
void __real_free(void *);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
}

#if defined(__SANITIZE_ADDRESS__)
#define OS_READS_BEFORE_POINTER __attribute__((no_sanitize("address")))
#else
#define OS_READS_BEFORE_POINTER
#endif

extern "C" {
void *__wrap_malloc(size_t bytes);
void __wrap_free(void *payload);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *payload, size_t bytes);
}

namespace os {
namespace {
constexpr int kOwners = static_cast<int>(Owner::Count);
constexpr int kBoundTasks = 12;
constexpr uint32_t kMagic = 0x4F534C47;

struct Prefix {
  uint32_t size;
  uint16_t owner;
  uint16_t check;
  uint32_t guard;
  uint32_t reserved;  // Keeps the payload on the 8 byte boundary it would have had.
};
inline uint32_t guardFor(const void *payload) { return kMagic ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(payload)); }

std::atomic<uint32_t> usedBytes[kOwners];
std::atomic<uint32_t> peakBytes[kOwners];
std::atomic<uint8_t> states[kOwners];
std::atomic<uint8_t> currentMediaOwner{static_cast<uint8_t>(Owner::Music)};
std::atomic<uint8_t> bootPhase{static_cast<uint8_t>(Boot::Joining)};

struct Binding { std::atomic<void *> task; std::atomic<uint8_t> owner; };
Binding bindings[kBoundTasks];

SemaphoreHandle_t secureMutex = nullptr;
std::atomic<bool> secureBusy{false};
std::atomic<bool> secureHeldBack{false};

char faultText[64] = "";
std::atomic<uint32_t> faultAt{0};
std::atomic<uint32_t> faults{0};
std::atomic<bool> faultNotice{false};

Owner ownerOfThisTask() {
  void *task = xTaskGetCurrentTaskHandle();
  if (!task) return Owner::System;
  for (auto &binding : bindings) {
    if (binding.task.load(std::memory_order_relaxed) == task) {
      const Owner owner = static_cast<Owner>(binding.owner.load(std::memory_order_relaxed));
      // The library and player tasks work for whichever media app is using them.
      return owner == Owner::Music ? static_cast<Owner>(currentMediaOwner.load()) : owner;
    }
  }
  return Owner::System;
}

bool admit(Owner owner, size_t bytes) {
  if (owner == Owner::System || owner == Owner::Ui) return true;
  const uint32_t limit = budget(owner);
  if (usedBytes[static_cast<int>(owner)].load() + bytes > limit) return false;
  return ESP.getFreeHeap() > bytes + kFloorBytes;
}

void charge(Owner owner, uint32_t bytes) {
  const int index = static_cast<int>(owner);
  const uint32_t now = usedBytes[index].fetch_add(bytes) + bytes;
  uint32_t high = peakBytes[index].load();
  while (now > high && !peakBytes[index].compare_exchange_weak(high, now)) {}
}
}  // namespace

// mbedTLS allocates through its own hook. It may use the broker's RAM but never the floor.
static void *secureCalloc(size_t count, size_t size) {
  if (ESP.getFreeHeap() < count * size + kFloorBytes / 2) return nullptr;
  return __wrap_calloc(count, size);
}

void begin() {
  if (!secureMutex) secureMutex = xSemaphoreCreateMutex();
  mbedtls_platform_set_calloc_free(secureCalloc, __wrap_free);
  setState(Owner::Ui, AppState::Foreground);
  setState(Owner::Chat, AppState::Starting);
}

void bindTask(Owner owner) {
  void *task = xTaskGetCurrentTaskHandle();
  for (auto &binding : bindings) {
    void *expected = nullptr;
    if (binding.task.compare_exchange_strong(expected, task) || expected == task) {
      binding.owner = static_cast<uint8_t>(owner);
      return;
    }
  }
}

void setMediaOwner(Owner owner) { currentMediaOwner = static_cast<uint8_t>(owner); }
Owner mediaOwner() { return static_cast<Owner>(currentMediaOwner.load()); }

uint32_t used(Owner owner) { return usedBytes[static_cast<int>(owner)].load(); }
uint32_t peak(Owner owner) { return peakBytes[static_cast<int>(owner)].load(); }
uint32_t budget(Owner owner) {
  switch (owner) {
    case Owner::Chat: return kChatBudget;
    case Owner::Music: case Owner::Books: return kMediaBudget;
    default: return 0;
  }
}
const char *name(Owner owner) {
  static const char *const names[] = {"SYSTEM", "HOME", "SUPACHAT", "MUSIC", "BOOKS"};
  return names[static_cast<int>(owner)];
}

void setState(Owner owner, AppState value) { states[static_cast<int>(owner)] = static_cast<uint8_t>(value); }
AppState state(Owner owner) { return static_cast<AppState>(states[static_cast<int>(owner)].load()); }
const char *stateName(AppState value) {
  static const char *const names[] = {"STOPPED", "STARTING", "FRONT", "BACK"};
  return names[static_cast<int>(value)];
}

void holdSecure(bool hold) { secureHeldBack = hold; }

bool secureFits() { return !secureHeldBack.load() && ESP.getFreeHeap() > kSecureConnectionPeak + kFloorBytes && ESP.getMaxAllocHeap() > 20 * 1024; }

bool acquireSecure(Priority priority, uint32_t waitMs) {
  const uint32_t started = millis();
  for (;;) {
    if (xSemaphoreTake(secureMutex, 0) == pdTRUE) {
      if (!secureBusy.load() && secureFits()) { secureBusy = true; xSemaphoreGive(secureMutex); return true; }
      xSemaphoreGive(secureMutex);
    }
    if (priority == Priority::Background || millis() - started >= waitMs) return false;
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
void releaseSecure() { secureBusy = false; }
bool secureHeld() { return secureBusy.load(); }

void recordFault(Owner owner, const char *what) {
  snprintf(faultText, sizeof(faultText), "%s: %s", name(owner), what);
  faultAt = millis();
  faults++;
  faultNotice = true;
}
const char *lastFault() { return faultText; }
uint32_t lastFaultAt() { return faultAt.load(); }
uint32_t faultCount() { return faults.load(); }
bool takeFaultNotice(char *text, size_t size) {
  if (!faultNotice.exchange(false)) return false;
  snprintf(text, size, "%s", faultText);
  return true;
}

void setBoot(Boot phase) {
  // Skipping ends the waiting only; a settled outcome stays what it was.
  bootPhase = static_cast<uint8_t>(phase);
}
Boot boot() { return static_cast<Boot>(bootPhase.load()); }
bool bootSettled() { const Boot phase = boot(); return phase != Boot::Joining && phase != Boot::Syncing; }
const char *bootText(Boot phase) {
  static const char *const names[] = {"JOINING WIFI", "SYNCING CHAT", "READY", "NO NETWORK", "SERVER DOWN", "STILL SYNCING", "SKIPPED"};
  return names[static_cast<int>(phase)];
}

void snapshot(char *text, size_t size) {
  size_t at = snprintf(text, size, "free %u largest %u secure %s |", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), secureBusy.load() ? "busy" : "idle");
  for (int index = 0; index < kOwners && at < size; index++) {
    const Owner owner = static_cast<Owner>(index);
    at += snprintf(text + at, size - at, " %s %u/%u peak %u %s", name(owner), used(owner), budget(owner), peak(owner), stateName(state(owner)));
  }
  if (at < size) snprintf(text + at, size - at, " | faults %u %s", faultCount(), lastFault());
}

}  // namespace os

// The ledger itself: every malloc through here carries its owner in a small prefix. A pointer
// without the prefix came from outside (ROM code, the prebuilt libraries' own heap calls) and
// goes straight back to the real free, uncounted. What can't be seen here: anything calling
// heap_caps_malloc directly, which means the Wi-Fi driver, lwIP's pools and M5GFX's buffers.
using os::Prefix;
extern "C" {
void *__wrap_malloc(size_t bytes) {
  const os::Owner owner = os::ownerOfThisTask();
  if (!os::admit(owner, bytes)) return nullptr;
  auto *prefix = static_cast<Prefix *>(__real_malloc(bytes + sizeof(Prefix)));
  if (!prefix) return nullptr;
  void *payload = prefix + 1;
  prefix->size = bytes;
  prefix->owner = static_cast<uint16_t>(owner);
  prefix->guard = os::guardFor(payload);
  os::charge(owner, bytes);
  return payload;
}
OS_READS_BEFORE_POINTER void __wrap_free(void *payload) {
  if (!payload) return;
  auto *prefix = static_cast<Prefix *>(payload) - 1;
  if (prefix->guard != os::guardFor(payload)) { __real_free(payload); return; }
  prefix->guard = 0;
  os::usedBytes[prefix->owner].fetch_sub(prefix->size);
  __real_free(prefix);
}
void *__wrap_calloc(size_t count, size_t size) {
  const size_t bytes = count * size;
  void *payload = __wrap_malloc(bytes);
  if (payload) memset(payload, 0, bytes);
  return payload;
}
OS_READS_BEFORE_POINTER void *__wrap_realloc(void *payload, size_t bytes) {
  if (!payload) return __wrap_malloc(bytes);
  auto *prefix = static_cast<Prefix *>(payload) - 1;
  if (prefix->guard != os::guardFor(payload)) return __real_realloc(payload, bytes);
  void *moved = __wrap_malloc(bytes);
  if (!moved) return nullptr;
  memcpy(moved, payload, prefix->size < bytes ? prefix->size : bytes);
  __wrap_free(payload);
  return moved;
}
}
