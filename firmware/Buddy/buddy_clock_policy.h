#pragma once
#include <stdint.h>

enum BuddyClockReason : uint8_t {
  CLOCK_REASON_NONE, CLOCK_REASON_COMM, CLOCK_REASON_INVALID,
  CLOCK_REASON_OSC_STOP, CLOCK_REASON_BACKWARD, CLOCK_REASON_FORWARD
};

struct BuddyClockDecision {
  uint32_t effective;
  BuddyClockReason reason;
  bool repair;
  bool usable;
};

// Without an independent wall clock, a good-looking RTC after a long absence
// cannot prove elapsed time. Prefer losing uncertain days to false aging.
static constexpr uint32_t BUDDY_MAX_BOOT_FORWARD = 45UL * 86400UL;
static constexpr uint32_t BUDDY_MAX_RUNNING_DRIFT = 120UL;

static BuddyClockDecision decideBuddyClock(bool communicationOk, bool calendarOk,
                                          bool oscillatorStop, uint32_t rtcEpoch,
                                          uint32_t savedFloor, bool trusted,
                                          uint32_t lastEpoch, uint32_t lastMs,
                                          uint32_t ticks) {
  uint32_t runtimeExpected = trusted ? lastEpoch + (uint32_t)(ticks - lastMs) / 1000UL : savedFloor;
  uint32_t expected = runtimeExpected;
  if (expected < savedFloor) expected = savedFloor;
  BuddyClockReason reason = !communicationOk ? CLOCK_REASON_COMM :
    oscillatorStop ? CLOCK_REASON_OSC_STOP :
    !calendarOk ? CLOCK_REASON_INVALID : CLOCK_REASON_NONE;
  if (reason != CLOCK_REASON_NONE) return {expected, reason, communicationOk && expected, expected != 0};
  if (rtcEpoch < savedFloor || (trusted && rtcEpoch < lastEpoch))
    return {expected, CLOCK_REASON_BACKWARD, expected != 0, expected != 0};
  if (trusted && expected > rtcEpoch && expected - rtcEpoch > BUDDY_MAX_RUNNING_DRIFT)
    return {expected, CLOCK_REASON_BACKWARD, expected != 0, expected != 0};
  uint32_t ceiling = trusted ? runtimeExpected + BUDDY_MAX_RUNNING_DRIFT :
    savedFloor + BUDDY_MAX_BOOT_FORWARD;
  if ((trusted || savedFloor) && rtcEpoch > ceiling)
    return {expected, CLOCK_REASON_FORWARD, expected != 0, expected != 0};
  return {rtcEpoch, CLOCK_REASON_NONE, false, true};
}
