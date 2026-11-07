#pragma once

#include <stdint.h>

// Timer, screen time, alarm and stopwatch: the arithmetic only, with no hardware, so a host test
// can run it (firmware/tools/clock-apps-test.cpp). Times are millis() values; every comparison is
// a signed difference, so the 49-day wrap of millis() changes nothing.
namespace clockapps {

inline bool reached(uint32_t now, uint32_t at) { return static_cast<int32_t>(now - at) >= 0; }

// A countdown: the timer, and screen time.
struct Countdown {
  enum class State : uint8_t { Idle, Running, Paused, Ringing };
  State state = State::Idle;
  uint32_t durationMs = 0;   // What a reset returns to.
  uint32_t endsAt = 0;       // While running.
  uint32_t pausedLeftMs = 0; // While paused.

  uint32_t leftMs(uint32_t now) const {
    if (state == State::Running) return reached(now, endsAt) ? 0 : endsAt - now;
    if (state == State::Paused) return pausedLeftMs;
    if (state == State::Ringing) return 0;
    return durationMs;
  }
  // Rounded up, so a countdown shows 0:01 until it is really over.
  uint32_t leftSeconds(uint32_t now) const { return (leftMs(now) + 999) / 1000; }
  void set(uint32_t ms) { state = State::Idle; durationMs = ms; }
  bool start(uint32_t now) {
    if (state == State::Paused) { endsAt = now + pausedLeftMs; state = State::Running; return true; }
    if (state != State::Idle || durationMs == 0) return false;
    endsAt = now + durationMs; state = State::Running; return true;
  }
  void pause(uint32_t now) { if (state == State::Running) { pausedLeftMs = leftMs(now); state = State::Paused; } }
  void reset() { state = State::Idle; }
  // True once, the moment it runs out; it then rings until dismissed.
  bool expire(uint32_t now) {
    if (state != State::Running || !reached(now, endsAt)) return false;
    state = State::Ringing; return true;
  }
};

// Screen time warns at five minutes and one minute left, then rings at zero.
enum class Warning : uint8_t { None, FiveMinutes, OneMinute };
struct ScreenTime {
  Countdown countdown;
  bool warnedFive = false;
  bool warnedOne = false;
  bool start(uint32_t now) {
    const bool fresh = countdown.state == Countdown::State::Idle;
    if (!countdown.start(now)) return false;
    // A session shorter than a warning doesn't get that warning at its start.
    if (fresh) { warnedFive = countdown.durationMs <= 5 * 60000u; warnedOne = countdown.durationMs <= 60000u; }
    return true;
  }
  Warning warning(uint32_t now) {
    if (countdown.state != Countdown::State::Running) return Warning::None;
    const uint32_t left = countdown.leftMs(now);
    if (left == 0) return Warning::None;
    if (!warnedOne && left <= 60000u) { warnedOne = true; warnedFive = true; return Warning::OneMinute; }
    if (!warnedFive && left <= 5 * 60000u) { warnedFive = true; return Warning::FiveMinutes; }
    return Warning::None;
  }
};

// One alarm. Repeat is a mask of days, bit 0 Sunday as in struct tm; 0 rings once, then turns off.
constexpr uint8_t kOnce = 0;
constexpr uint8_t kWeekdays = 0x3E;
constexpr uint8_t kEveryDay = 0x7F;
constexpr uint32_t kSnoozeMs = 5 * 60000u;

struct Alarm {
  bool enabled = false;
  uint8_t hour = 7;
  uint8_t minute = 0;
  uint8_t repeat = kOnce;
  bool snoozed = false;
  uint32_t snoozeUntil = 0;
  int32_t lastFired = -1;  // The minute it last rang, so one minute rings once.

  static int32_t minuteKey(int yearDay, int hour, int minute) { return (yearDay * 24 + hour) * 60 + minute; }
  bool dayMatches(int weekday) const { return repeat == kOnce || (repeat >> weekday) & 1; }
  // Called with the wall clock when it is known; false means nothing to ring.
  bool due(int weekday, int yearDay, int hour24, int minute24) {
    if (!enabled || hour24 != hour || minute24 != minute || !dayMatches(weekday)) return false;
    const int32_t key = minuteKey(yearDay, hour24, minute24);
    if (key == lastFired) return false;
    lastFired = key;
    if (repeat == kOnce) enabled = false;
    snoozed = false;
    return true;
  }
  void snooze(uint32_t now) { snoozed = true; snoozeUntil = now + kSnoozeMs; }
  bool snoozeDue(uint32_t now) {
    if (!snoozed || !reached(now, snoozeUntil)) return false;
    snoozed = false; return true;
  }
  // Minutes from this moment to the next ring, or -1 if it will not ring. Snooze is not counted.
  int32_t minutesUntil(int weekday, int hour24, int minute24) const {
    if (!enabled) return -1;
    const int now = hour24 * 60 + minute24, at = hour * 60 + minute;
    for (int day = 0; day < 8; day++) {
      const int32_t ahead = day * 1440 + at - now;
      if (ahead <= 0) continue;
      if (dayMatches((weekday + day) % 7)) return ahead;
    }
    return -1;
  }
  // Typed digits push in from the right, as 24-hour HHMM; the alarm takes the time whenever the
  // digits so far make one (0, 7, 3, 0 sets 0:07, skips 0:73, then sets 7:30).
  static uint16_t pushDigit(uint16_t typed, int digit) { return static_cast<uint16_t>((typed % 1000) * 10 + digit); }
  static bool isTime(uint16_t typed) { return typed / 100 <= 23 && typed % 100 <= 59; }
};

// The stopwatch.
struct Stopwatch {
  bool running = false;
  uint32_t startedAt = 0;
  uint32_t heldMs = 0;
  uint32_t elapsedMs(uint32_t now) const { return heldMs + (running ? now - startedAt : 0); }
  void toggle(uint32_t now) {
    if (running) { heldMs += now - startedAt; running = false; }
    else { startedAt = now; running = true; }
  }
  void reset() { running = false; heldMs = 0; }
};

// The timer's typed digits, pushed in from the right as MMSS: 5, 0, 0 is 5:00. Seconds over 59
// carry, as on a microwave (9, 0 is 1:30), and the most is 99:59.
inline uint16_t timerPushDigit(uint16_t typed, int digit) { return static_cast<uint16_t>((typed % 1000) * 10 + digit); }
inline uint32_t timerDigitsMs(uint16_t typed) {
  const uint32_t seconds = (typed / 100) * 60u + typed % 100;
  return (seconds > 99 * 60 + 59 ? 99 * 60 + 59 : seconds) * 1000u;
}

}  // namespace clockapps
