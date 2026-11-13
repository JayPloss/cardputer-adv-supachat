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

  // Alarms: a new one is 7:00 on weekdays. It rings Monday once, not Saturday.
  Alarms alarms; assert(alarms.add() && alarms.count == 1);
  assert(alarms.entries[0].weekdays && !alarms.entries[0].weekends);
  assert(!alarms.due(6, 100, 7, 0));               // Saturday
  assert(!alarms.due(0, 101, 7, 0));               // Sunday
  assert(alarms.due(1, 102, 7, 0) && alarms.ringing == 0);  // Monday
  assert(!alarms.due(1, 102, 7, 0));               // same minute again
  assert(alarms.due(5, 106, 7, 0));                // Friday
  assert(alarms.minutesUntil(5, 8, 0) == 2 * 1440 + 23 * 60);  // Friday 8:00 -> Monday 7:00
  // Weekends only: Saturday and Sunday, not Monday. Both: every day. Neither: never.
  alarms.add(); AlarmEntry &weekend = alarms.entries[1];
  weekend.hour = 9; weekend.minute = 30; weekend.weekdays = false; weekend.weekends = true;
  assert(alarms.due(6, 107, 9, 30) && alarms.ringing == 1 && alarms.due(0, 108, 9, 30) && !alarms.due(1, 109, 9, 30));
  weekend.weekdays = true; for (int day = 0; day < 7; day++) assert(weekend.dayMatches(day));
  weekend.weekdays = weekend.weekends = false; assert(!weekend.on() && weekend.minutesUntil(3, 6, 0) == -1);
  // The next ring is the nearest of all: Saturday 8:00 -> Monday 7:00 with only weekdays left on.
  int next = -1; assert(alarms.minutesUntil(6, 8, 0, &next) == 1440 + 23 * 60 && next == 0);
  assert(alarms.entries[0].minutesUntil(3, 7, 0) == 1440);  // at the minute itself: tomorrow
  // Two at the same minute ring once, together.
  weekend.hour = 7; weekend.minute = 0; weekend.weekdays = true;
  assert(alarms.due(2, 110, 7, 0) && alarms.ringing == 0 && !alarms.due(2, 110, 7, 0));
  // Snooze: rings again five minutes later, once.
  alarms.snooze(1000); assert(!alarms.snoozeDue(1000 + kSnoozeMs - 1)); assert(alarms.snoozeDue(1000 + kSnoozeMs)); assert(!alarms.snoozeDue(1000 + kSnoozeMs + 1));
  // Deleting the snoozed one cancels the snooze; deleting one before it moves it.
  alarms.ringing = 1; alarms.snooze(0); alarms.remove(0); assert(alarms.ringing == 0 && alarms.snoozed);
  alarms.remove(0); assert(alarms.count == 0 && alarms.ringing == -1 && !alarms.snoozed);
  for (int index = 0; index < kMaxAlarms; index++) assert(alarms.add());
  assert(!alarms.add() && alarms.count == kMaxAlarms);
  // Saved and read back; a damaged list reads as none.
  alarms.entries[2].hour = 23; alarms.entries[2].minute = 59; alarms.entries[2].weekends = true;
  uint8_t packed[Alarms::kPackedBytes]; const int length = alarms.pack(packed);
  Alarms back; assert(back.unpack(packed, length) && back.count == kMaxAlarms);
  assert(back.entries[2].hour == 23 && back.entries[2].minute == 59 && back.entries[2].weekdays && back.entries[2].weekends);
  assert(!back.unpack(packed, length - 1) && back.count == 0);
  packed[1] = 24; assert(!back.unpack(packed, length) && back.count == 0);
  // Typed alarm digits: 0,7,3,0 ends at 07:30, passing 0:73, which is not a time.
  // Alarm time entry, as on a microwave: from 00:00, 7 is 00:07, 3 is 00:73 (not a time yet),
  // 0 is 07:30; Del takes digits back; a fifth digit pushes the first out.
  TimeEntry entry; assert(!entry.active() && !entry.valid());
  entry.push(7); assert(entry.active() && entry.digits == 7 && entry.valid() && entry.hour() == 0 && entry.minute() == 7);
  entry.push(3); assert(entry.digits == 73 && !entry.valid() && !entry.full());
  entry.push(0); assert(entry.digits == 730 && entry.valid() && entry.hour() == 7 && entry.minute() == 30 && !entry.full());
  assert(entry.undo() && entry.digits == 73 && entry.count == 2);
  assert(entry.undo() && entry.undo() && !entry.active() && entry.digits == 0 && !entry.undo());
  for (int d : {0, 7, 3, 0}) entry.push(d); assert(entry.full() && entry.valid() && entry.digits == 730);
  entry.clear(); for (int d : {2, 5, 7, 3}) entry.push(d); assert(entry.full() && !entry.valid());
  entry.push(0); assert(entry.digits == 5730 && entry.count == 4 && !entry.valid());
  entry.clear(); for (int d : {2, 3, 5, 9}) entry.push(d); assert(entry.valid() && entry.hour() == 23 && entry.minute() == 59);
  assert(!Alarms::isTime(2400) && Alarms::isTime(2359));

  // Stopwatch.
  Stopwatch watch; watch.toggle(100); watch.toggle(1100); assert(watch.elapsedMs(5000) == 1000);
  watch.toggle(6000); assert(watch.elapsedMs(6500) == 1500); watch.reset(); assert(watch.elapsedMs(9000) == 0);

  puts("clock apps: all checks pass");

  // Bedtime is 19:00 to 05:00.
  assert(isBedtime(19) && isBedtime(23) && isBedtime(0) && isBedtime(4) && !isBedtime(5) && !isBedtime(18));
  // Sleep timer: T steps 15, 30, 45, 60, off; from a running timer, to the next step above what's left.
  SleepTimer sleep; assert(sleep.nextStepMs(0) == 15 * 60000u && sleep.leftMs(0) == 0 && !sleep.expired(0));
  sleep.set(t0, sleep.nextStepMs(t0)); assert(sleep.leftSeconds(t0) == 15 * 60);
  assert(sleep.nextStepMs(t0 + 1000) == 30 * 60000u);
  sleep.set(t0, 30 * 60000u); assert(sleep.nextStepMs(t0 + 20 * 60000u) == 15 * 60000u);
  sleep.set(t0, 60 * 60000u); assert(sleep.nextStepMs(t0) == 0);
  sleep.set(t0, 0); assert(!sleep.on);
  // It fades over the last 30 seconds, across the millis() wrap, then expires.
  sleep.set(t0, 60000); assert(sleep.fadeScale(t0 + 29000) == 255);
  assert(sleep.fadeScale(t0 + 45000) == 127); assert(sleep.fadeScale(t0 + 60000) == 0);
  assert(!sleep.expired(t0 + 59999) && sleep.expired(t0 + 60000));
  sleep.cancel(); assert(!sleep.expired(t0 + 70000) && sleep.fadeScale(t0 + 70000) == 255);
  // School nights: Sunday to Thursday, 21:20 to 21:30.
  assert(isSchoolNight(0) && isSchoolNight(4) && !isSchoolNight(5) && !isSchoolNight(6));
  assert(schoolNightStopInMs(4, 21, 19, 59) == 0);
  assert(schoolNightStopInMs(4, 21, 20, 0) == 600000);
  assert(schoolNightStopInMs(0, 21, 29, 30) == 30000);
  assert(schoolNightStopInMs(4, 21, 30, 0) == 0);
  assert(schoolNightStopInMs(5, 21, 25, 0) == 0 && schoolNightStopInMs(6, 21, 25, 0) == 0);
  return 0;
}
