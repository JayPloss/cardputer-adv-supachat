#pragma once

#include <Arduino.h>

// A small operating layer over the firmware's tasks, because RAM is the one thing they all
// compete for. Allocations are charged to the app that owns the task making them, through hooks that
// never change a pointer, and an app over its budget gets a failed allocation. A secure
// connection starts only when the free heap has room, and an app that runs out is stopped, not the device.
namespace os {

// Who a task's allocations are charged to. Ui has no budget, and it alone may use the floor.
enum class Owner : uint8_t { System, Ui, Chat, Music, Books, Count };
enum class AppState : uint8_t { Stopped, Starting, Foreground, Background };

// Sized from the emulator's figures, not yet from a device; the SYSTEM screen shows the real numbers.
constexpr uint32_t kFloorBytes = 16 * 1024;
constexpr uint32_t kChatBudget = 44 * 1024;
constexpr uint32_t kMediaBudget = 72 * 1024;
// TLS allocates outside the ledger, through mbedTLS's own calls, so the broker admits by free heap.
constexpr uint32_t kSecureAdmitFree = 80 * 1024;
constexpr uint32_t kSecureAdmitLargest = 20 * 1024;

void begin();
void bindTask(Owner owner);
uint32_t used(Owner owner);
uint32_t peak(Owner owner);
uint32_t budget(Owner owner);
uint32_t uncountedBlocks();
uint32_t refusedAllocations();
void reconcile(Owner owner);
void setMediaOwner(Owner owner);
Owner mediaOwner();

const char *name(Owner owner);

// Apps. The UI says which is in front; the rest follows from what each is doing.
void setState(Owner owner, AppState state);
AppState state(Owner owner);
const char *stateName(AppState state);

// The secure connection broker: one at a time, and only when the RAM is really there.
// A foreground request waits up to waitMs; a background one is refused at once.
enum class Priority : uint8_t { Background, Foreground };
bool acquireSecure(Priority priority, uint32_t waitMs = 0);
void releaseSecure();
bool secureHeld();
bool secureFits();
// A media app in front needs the RAM: no new secure connection starts until it has gone.
void holdSecure(bool hold);
struct SecureLease {
  explicit SecureLease(Priority priority, uint32_t waitMs = 0) : held(acquireSecure(priority, waitMs)) {}
  ~SecureLease() { if (held) releaseSecure(); }
  const bool held;
};

// Containment: an app that ran out was stopped. The UI reads this and shows it once.
void recordFault(Owner owner, const char *what);
const char *lastFault();
uint32_t lastFaultAt();
uint32_t faultCount();
bool takeFaultNotice(char *text, size_t size);

// The boot phase: the splash stays until chat's first sync settles or can't proceed.
enum class Boot : uint8_t { Joining, Syncing, Ready, NoNetwork, ServerDown, TimedOut, Skipped };
constexpr uint32_t kBootMaxMs = 45000;
void setBoot(Boot phase);
Boot boot();
bool bootSettled();
const char *bootText(Boot phase);

// One line for logs and the emulator's report.
void snapshot(char *text, size_t size);

}  // namespace os
