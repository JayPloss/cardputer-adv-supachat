#include "os.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>

namespace os {
namespace {
constexpr int kOwners = static_cast<int>(Owner::Count);

std::atomic<uint8_t> states[kOwners];
std::atomic<uint8_t> currentMediaOwner{static_cast<uint8_t>(Owner::Music)};
std::atomic<uint8_t> bootPhase{static_cast<uint8_t>(Boot::Joining)};

SemaphoreHandle_t secureMutex = nullptr;
std::atomic<bool> secureBusy{false};
std::atomic<bool> secureHeldBack{false};

char faultText[64] = "";
std::atomic<uint32_t> faultAt{0};
std::atomic<uint32_t> faults{0};
std::atomic<bool> faultNotice{false};
}  // namespace

void begin() {
  if (!secureMutex) secureMutex = xSemaphoreCreateMutex();
  setState(Owner::Ui, AppState::Foreground);
  setState(Owner::Chat, AppState::Starting);
}

void setMediaOwner(Owner owner) { currentMediaOwner = static_cast<uint8_t>(owner); }
Owner mediaOwner() { return static_cast<Owner>(currentMediaOwner.load()); }

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

bool secureFits() { return !secureHeldBack.load() && ESP.getFreeHeap() > kSecureAdmitFree && ESP.getMaxAllocHeap() > kSecureAdmitLargest; }

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

void setBoot(Boot phase) { bootPhase = static_cast<uint8_t>(phase); }
Boot boot() { return static_cast<Boot>(bootPhase.load()); }
bool bootSettled() { const Boot phase = boot(); return phase != Boot::Joining && phase != Boot::Syncing; }
const char *bootText(Boot phase) {
  static const char *const names[] = {"JOINING WIFI", "SYNCING CHAT", "READY", "NO NETWORK", "SERVER DOWN", "STILL SYNCING", "SKIPPED"};
  return names[static_cast<int>(phase)];
}

void snapshot(char *text, size_t size) {
  size_t at = snprintf(text, size, "free %u largest %u lowest %u secure %s |", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), secureBusy.load() ? "busy" : "idle");
  for (int index = 0; index < kOwners && at < size; index++) {
    const Owner owner = static_cast<Owner>(index);
    at += snprintf(text + at, size - at, " %s %s", name(owner), stateName(state(owner)));
  }
  if (at < size) snprintf(text + at, size - at, " | faults %u %s", faultCount(), lastFault());
}

}  // namespace os
