#pragma once
#include <stdint.h>

// Test-only time arithmetic. This file has no effect on ordinary firmware;
// callers must validate real RTC time before passing it here. One simulated
// lifespan (not a fixed 365 days) occupies exactly six physical hours.
struct BuddyAcceleratedTime {
  uint32_t realBirth;
  uint32_t simulatedBirth;
  uint32_t seed;
  uint16_t generation;
  uint16_t lifespanDays;
};
static constexpr uint32_t BUDDY_TEST_REAL_LIFE_SECONDS = 6UL * 3600UL;

static bool buddyAcceleratedTimeValid(const BuddyAcceleratedTime &time) {
  return time.realBirth && time.simulatedBirth && time.seed &&
         time.generation && time.lifespanDays >= 305 &&
         time.lifespanDays <= 425;
}

static bool buddyAcceleratedNow(const BuddyAcceleratedTime &time,
                                uint32_t physicalNow, uint32_t &simulatedNow) {
  if (!buddyAcceleratedTimeValid(time) || physicalNow < time.realBirth)
    return false;
  uint64_t result = (uint64_t)time.simulatedBirth +
                    (uint64_t)(physicalNow - time.realBirth) *
                    time.lifespanDays * 4UL;
  if (result > UINT32_MAX) return false;
  simulatedNow = (uint32_t)result;
  return true;
}

// The smallest real epoch that could have produced this simulated epoch.
// Used to reconstruct an interrupted successor rebase without replaying an
// entire life or pretending that its birth happened at reboot time.
static bool buddyAcceleratedRealAt(const BuddyAcceleratedTime &time,
                                   uint32_t simulatedEpoch, uint32_t &physicalEpoch) {
  if (!buddyAcceleratedTimeValid(time) || simulatedEpoch < time.simulatedBirth)
    return false;
  uint64_t factor = (uint64_t)time.lifespanDays * 4UL;
  uint64_t elapsed = ((uint64_t)(simulatedEpoch - time.simulatedBirth) +
                      factor - 1) / factor;
  uint64_t result = (uint64_t)time.realBirth + elapsed;
  if (result > UINT32_MAX) return false;
  physicalEpoch = (uint32_t)result;
  return true;
}

static bool buddyAcceleratedRebase(const BuddyAcceleratedTime &previous,
                                   uint32_t newSimulatedBirth, uint32_t newSeed,
                                   uint16_t newGeneration, uint16_t newLifespanDays,
                                   BuddyAcceleratedTime &next) {
  uint32_t physicalBirth = 0;
  if (!buddyAcceleratedRealAt(previous, newSimulatedBirth, physicalBirth))
    return false;
  next = {physicalBirth, newSimulatedBirth, newSeed, newGeneration,
          newLifespanDays};
  return buddyAcceleratedTimeValid(next);
}
