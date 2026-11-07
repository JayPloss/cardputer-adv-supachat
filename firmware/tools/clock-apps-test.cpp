// Host test of the clock apps' arithmetic.
//   g++ -std=c++11 -I firmware/include firmware/tools/clock-apps-test.cpp -o /tmp/clock-test && /tmp/clock-test
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "clock_apps.h"

using namespace clockapps;

int main() {
  // Timer: start, pause, resume, ring once; across the millis() wrap.
  Countdown timer; timer.set(5000);
  const uint32_t t0 = 0xFFFFF000u;  // 4 s before millis() wraps.
  assert(timer.start(t0));
  assert(timer.leftSeconds(t0 + 1) == 5);
  timer.pause(t0 + 2000); assert(timer.leftMs(t0 + 9000) == 3000);
  assert(timer.start(t0 + 9000));
  assert(!timer.expire(t0 + 11999));
  assert(timer.expire(t0 + 12000)); assert(!timer.expire(t0 + 12001));
  assert(timer.state == Countdown::State::Ringing && timer.leftMs(t0 + 20000) == 0);
  timer.reset(); assert(timer.leftMs(0) == 5000);
  Countdown empty; assert(!empty.start(0));

  // Typed timer digits: 5,0,0 is 5:00; 9,0 is 1:30; overflow holds at 99:59.
  uint16_t digits = 0; for (int d : {5, 0, 0}) digits = timerPushDigit(digits, d); assert(timerDigitsMs(digits) == 300000);
  digits = 0; for (int d : {9, 0}) digits = timerPushDigit(digits, d); assert(timerDigitsMs(digits) == 90000);
  digits = 0; for (int d : {9, 9, 9, 9, 9}) digits = timerPushDigit(digits, d); assert(timerDigitsMs(digits) == (99 * 60 + 59) * 1000u);

  // Screen time: 30 minutes warns at 5:00 and 1:00, once each, then rings.
  ScreenTime screen; screen.countdown.set(30 * 60000u); assert(screen.start(1000));
  assert(screen.warning(1000 + 24 * 60000u) == Warning::None);
  assert(screen.warning(1000 + 25 * 60000u) == Warning::FiveMinutes);
  assert(screen.warning(1000 + 25 * 60000u + 10) == Warning::None);
  assert(screen.warning(1000 + 29 * 60000u) == Warning::OneMinute);
  assert(screen.warning(1000 + 29 * 60000u + 10) == Warning::None);
  assert(screen.countdown.expire(1000 + 30 * 60000u));
  // A jump past both (a long stall) gives only the one-minute warning.
  ScreenTime late; late.countdown.set(30 * 60000u); late.start(0);
  assert(late.warning(29 * 60000u + 30000) == Warning::OneMinute && late.warning(29 * 60000u + 31000) == Warning::None);
  // Three minutes: no five-minute warning at its start.
  ScreenTime shortOne; shortOne.countdown.set(3 * 60000u); shortOne.start(0);
  assert(shortOne.warning(1) == Warning::None && shortOne.warning(2 * 60000u) == Warning::OneMinute);

  // Alarm: weekdays at 7:00 rings Monday once, not Saturday; once turns itself off.
  Alarm alarm; alarm.enabled = true; alarm.hour = 7; alarm.minute = 0; alarm.repeat = kWeekdays;
  assert(!alarm.due(6, 100, 7, 0));               // Saturday
  assert(alarm.due(1, 102, 7, 0));                // Monday
  assert(!alarm.due(1, 102, 7, 0));               // same minute again
  assert(alarm.due(2, 103, 7, 0) && alarm.enabled);
  assert(alarm.minutesUntil(5, 8, 0) == 2 * 1440 + 23 * 60);  // Friday 8:00 -> Monday 7:00
  Alarm once; once.enabled = true; once.hour = 6; once.minute = 30;
  assert(once.minutesUntil(3, 6, 30) == 1440);    // at the minute itself: tomorrow
  assert(once.due(3, 10, 6, 30) && !once.enabled && once.minutesUntil(3, 6, 31) == -1);
  // Snooze: rings again five minutes later, once.
  once.snooze(1000); assert(!once.snoozeDue(1000 + kSnoozeMs - 1)); assert(once.snoozeDue(1000 + kSnoozeMs)); assert(!once.snoozeDue(1000 + kSnoozeMs + 1));
  // Typed alarm digits: 0,7,3,0 ends at 07:30, passing 0:73, which is not a time.
  uint16_t typed = 0; for (int d : {0, 7, 3}) typed = Alarm::pushDigit(typed, d); assert(typed == 73 && !Alarm::isTime(typed));
  typed = Alarm::pushDigit(typed, 0); assert(typed == 730 && Alarm::isTime(typed));
  assert(!Alarm::isTime(2400) && Alarm::isTime(2359));

  // Stopwatch.
  Stopwatch watch; watch.toggle(100); watch.toggle(1100); assert(watch.elapsedMs(5000) == 1000);
  watch.toggle(6000); assert(watch.elapsedMs(6500) == 1500); watch.reset(); assert(watch.elapsedMs(9000) == 0);

  puts("clock apps: all checks pass");
  return 0;
}
