#include "os.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_heap_caps.h>

#include <atomic>

// The allocator underneath, reached through the linker's --wrap. The hooks below return exactly the
// pointer it gave and never touch the block: libc's own free goes straight to the heap, past them
// (a version that prefixed each block boot-looped the real device), so such a free must stay harmless.
struct _reent;
extern "C" {
void *__real_malloc(size_t);
void __real_free(void *);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real__free_r(struct _reent *, void *);
}

namespace os {
namespace {
constexpr int kOwners = static_cast<int>(Owner::Count);
constexpr int kBoundTasks = 12;
std::atomic<uint8_t> states[kOwners];
std::atomic<uint8_t> currentMediaOwner{static_cast<uint8_t>(Owner::Music)};
std::atomic<uint8_t> bootPhase{static_cast<uint8_t>(Boot::Joining)};

// The ledger: a fixed side table of block to owner, allocated once as static memory, so the hooks
// never allocate. A free that bypassed the hooks leaves a stale entry: it overstates its owner until
// an allocation lands on that address again, or the owner's next reconcile. A full table leaves the
// new block uncounted but working.
constexpr int kTableSize = 1024;
constexpr int kMaxProbes = 48;
struct Entry { uintptr_t block; uint32_t size : 24; uint32_t owner : 8; };
Entry table[kTableSize];
portMUX_TYPE tableLock = portMUX_INITIALIZER_UNLOCKED;
uint32_t usedBytes[kOwners];
uint32_t peakBytes[kOwners];
std::atomic<uint32_t> uncounted{0};
std::atomic<uint32_t> refused{0};

struct Binding { std::atomic<void *> task; std::atomic<uint8_t> owner; };
Binding bindings[kBoundTasks];

inline int slotFor(uintptr_t block) { return static_cast<int>((block >> 3) * 2654435761u >> 22) & (kTableSize - 1); }

// With tableLock held. Linear probing; removal shifts later entries back, so no tombstones build up.
void tableRemove(int at) {
  usedBytes[table[at].owner] -= table[at].size;
  int gap = at;
  for (int step = 1; step < kTableSize; step++) {
    const int next = (at + step) & (kTableSize - 1);
    if (!table[next].block) break;
    const int home = slotFor(table[next].block);
    // Can this entry move back into the gap? Only if its home isn't between the gap and it.
    if (((next - home) & (kTableSize - 1)) >= ((next - gap) & (kTableSize - 1))) { table[gap] = table[next]; gap = next; }
  }
  table[gap].block = 0;
}
int tableFind(uintptr_t block) {
  int at = slotFor(block);
  for (int probe = 0; probe < kMaxProbes; probe++) {
    if (table[at].block == block) return at;
    if (!table[at].block) return -1;
    at = (at + 1) & (kTableSize - 1);
  }
  return -1;
}
void tableAdd(uintptr_t block, uint32_t size, Owner owner) {
  const int index = static_cast<int>(owner);
  portENTER_CRITICAL(&tableLock);
  // An entry already here is stale: its block was freed past the hooks.
  const int stale = tableFind(block);
  if (stale >= 0) tableRemove(stale);
  int at = slotFor(block);
  bool stored = false;
  for (int probe = 0; probe < kMaxProbes && !stored; probe++) {
    if (!table[at].block) {
      table[at].block = block; table[at].size = size; table[at].owner = index;
      usedBytes[index] += size;
      if (usedBytes[index] > peakBytes[index]) peakBytes[index] = usedBytes[index];
      stored = true;
    }
    at = (at + 1) & (kTableSize - 1);
  }
  portEXIT_CRITICAL(&tableLock);
  if (!stored) uncounted++;
}
void tableDrop(uintptr_t block) {
  portENTER_CRITICAL(&tableLock);
  const int at = tableFind(block);
  if (at >= 0) tableRemove(at);
  portEXIT_CRITICAL(&tableLock);
}

// System while there is no scheduler, inside an interrupt, or on a task nobody bound: those pass
// straight through, untouched and uncounted.
Owner ownerOfThisTask() {
  if (xPortInIsrContext()) return Owner::System;
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
  if (used(owner) + bytes <= budget(owner) && ESP.getFreeHeap() > bytes + kFloorBytes) return true;
  refused++;
  return false;
}


SemaphoreHandle_t secureMutex = nullptr;
std::atomic<bool> secureBusy{false};
std::atomic<bool> secureHeldBack{false};

char faultText[64] = "";
std::atomic<uint32_t> faultAt{0};
std::atomic<uint32_t> faults{0};
std::atomic<bool> faultNotice{false};
}  // namespace

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

uint32_t used(Owner owner) { portENTER_CRITICAL(&tableLock); const uint32_t value = usedBytes[static_cast<int>(owner)]; portEXIT_CRITICAL(&tableLock); return value; }
uint32_t peak(Owner owner) { portENTER_CRITICAL(&tableLock); const uint32_t value = peakBytes[static_cast<int>(owner)]; portEXIT_CRITICAL(&tableLock); return value; }
uint32_t budget(Owner owner) {
  switch (owner) {
    case Owner::Chat: return kChatBudget;
    case Owner::Music: case Owner::Books: return kMediaBudget;
    default: return 0;
  }
}
uint32_t uncountedBlocks() { return uncounted.load(); }
uint32_t refusedAllocations() { return refused.load(); }

// Forgets an owner's entries, so that blocks freed past the hooks stop counting against it. Live
// blocks stop counting too, until they are freed, so right after this the figure is low rather
// than high. Called with the owner at rest: when a media app stops, and for chat every few minutes.
void reconcile(Owner owner) {
  portENTER_CRITICAL(&tableLock);
  for (int at = 0; at < kTableSize; at++) if (table[at].block && table[at].owner == static_cast<int>(owner)) table[at].block = 0;
  usedBytes[static_cast<int>(owner)] = 0;
  // Clearing several entries can break probe chains, so what's left is re-inserted in place.
  for (int at = 0; at < kTableSize; at++) {
    if (!table[at].block) continue;
    const Entry entry = table[at];
    table[at].block = 0;
    int slot = slotFor(entry.block);
    while (table[slot].block) slot = (slot + 1) & (kTableSize - 1);
    table[slot] = entry;
  }
  portEXIT_CRITICAL(&tableLock);
}

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
  size_t at = snprintf(text, size, "free %u largest %u lowest %u secure %s uncounted %u refused %u |", ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getMinFreeHeap(), secureBusy.load() ? "busy" : "idle", uncountedBlocks(), refusedAllocations());
  for (int index = 0; index < kOwners && at < size; index++) {
    const Owner owner = static_cast<Owner>(index);
    at += snprintf(text + at, size - at, " %s %u/%u peak %u %s", name(owner), used(owner), budget(owner), peak(owner), stateName(state(owner)));
  }
  if (at < size) snprintf(text + at, size - at, " | faults %u %s", faultCount(), lastFault());
}

}  // namespace os

// The hooks. Each returns exactly what the real allocator returned.
extern "C" {
void *__wrap_malloc(size_t bytes) {
  const os::Owner owner = os::ownerOfThisTask();
  if (owner == os::Owner::System) return __real_malloc(bytes);
  if (!os::admit(owner, bytes)) return nullptr;
  void *block = __real_malloc(bytes);
  if (block) os::tableAdd(reinterpret_cast<uintptr_t>(block), heap_caps_get_allocated_size(block), owner);
  return block;
}
void *__wrap_calloc(size_t count, size_t size) {
  const os::Owner owner = os::ownerOfThisTask();
  if (owner == os::Owner::System) return __real_calloc(count, size);
  if (!os::admit(owner, count * size)) return nullptr;
  void *block = __real_calloc(count, size);
  if (block) os::tableAdd(reinterpret_cast<uintptr_t>(block), heap_caps_get_allocated_size(block), owner);
  return block;
}
void __wrap_free(void *block) {
  if (block && !xPortInIsrContext()) os::tableDrop(reinterpret_cast<uintptr_t>(block));
  __real_free(block);
}
// libc's own route to free, which stdio takes when closing a file. It only clears the entry;
// anything still getting past this (ROM code) leaves a stale entry, which does no harm but overcount.
void __wrap__free_r(struct _reent *reent, void *block) {
  if (block && !xPortInIsrContext()) os::tableDrop(reinterpret_cast<uintptr_t>(block));
  __real__free_r(reent, block);
}
void *__wrap_realloc(void *block, size_t bytes) {
  const os::Owner owner = os::ownerOfThisTask();
  if (owner == os::Owner::System) {
    if (block) os::tableDrop(reinterpret_cast<uintptr_t>(block));
    return __real_realloc(block, bytes);
  }
  if (!os::admit(owner, bytes)) return nullptr;
  void *moved = __real_realloc(block, bytes);
  if (moved) {
    if (block) os::tableDrop(reinterpret_cast<uintptr_t>(block));
    os::tableAdd(reinterpret_cast<uintptr_t>(moved), heap_caps_get_allocated_size(moved), owner);
  }
  return moved;
}
}
