#pragma once
#include <stdint.h>

// A restored life keeps its original saved epochs. The physical RTC remains
// truthful; only epochs presented to the life simulation are translated.
// This is deliberately separate from the disposable accelerated-test clock.
struct BuddyLifeClockOffset {
  uint32_t signature;
  uint32_t seconds;
  uint32_t inverse;
  uint32_t checksum;
};
static constexpr uint32_t BUDDY_LIFE_CLOCK_OFFSET_MAGIC = 0x424C434FUL;

static uint32_t buddyLifeClockOffsetChecksum(uint32_t seconds) {
  uint32_t hash = 2166136261UL;
  for (uint8_t i = 0; i < 4; ++i) {
    hash = (hash ^ (uint8_t)(seconds >> (8 * i))) * 16777619UL;
  }
  return hash ^ BUDDY_LIFE_CLOCK_OFFSET_MAGIC;
}

static BuddyLifeClockOffset buddyMakeLifeClockOffset(uint32_t seconds) {
  return {BUDDY_LIFE_CLOCK_OFFSET_MAGIC, seconds, ~seconds,
          buddyLifeClockOffsetChecksum(seconds)};
}

static bool buddyValidLifeClockOffset(const BuddyLifeClockOffset &record) {
  return record.signature == BUDDY_LIFE_CLOCK_OFFSET_MAGIC &&
         record.inverse == ~record.seconds &&
         record.checksum == buddyLifeClockOffsetChecksum(record.seconds);
}

static bool buddyLifeEpochFromPhysical(uint32_t physical, uint32_t offset,
                                       uint32_t &life) {
  if (physical < offset) return false;
  life = physical - offset;
  return true;
}

static bool buddyPhysicalEpochFromLife(uint32_t life, uint32_t offset,
                                       uint32_t &physical) {
  uint64_t result = (uint64_t)life + offset;
  if (result > UINT32_MAX) return false;
  physical = (uint32_t)result;
  return true;
}
