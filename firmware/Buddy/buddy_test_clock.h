#pragma once
#include <stdint.h>
#include <stddef.h>
#include "buddy_accelerated_time.h"

// These records exist only in a disposable six-hour test NVS image. The
// original NVS is backed up before that image is created.
static constexpr uint32_t BUDDY_TEST_CLOCK_MAGIC = 0x42544336UL;
static constexpr uint32_t BUDDY_TEST_STORAGE_MAGIC = 0x42545336UL;
struct BuddyTestClockRecord {
  uint32_t magic;
  uint32_t sequence;
  uint32_t checksum;
  BuddyAcceleratedTime time;
};
static_assert(sizeof(BuddyAcceleratedTime) == 16, "Test time record ABI");
static_assert(sizeof(BuddyTestClockRecord) == 28, "Test clock record ABI");

static uint32_t buddyTestClockChecksum(const BuddyTestClockRecord &record) {
  uint32_t hash = 2166136261UL;
  const uint8_t *bytes = (const uint8_t *)&record.time;
  for (size_t i = 0; i < sizeof(record.time); ++i)
    hash = (hash ^ bytes[i]) * 16777619UL;
  return hash ^ record.sequence ^ BUDDY_TEST_CLOCK_MAGIC;
}

static bool buddyTestClockValid(const BuddyTestClockRecord &record) {
  return record.magic == BUDDY_TEST_CLOCK_MAGIC &&
         buddyAcceleratedTimeValid(record.time) &&
         record.checksum == buddyTestClockChecksum(record);
}

static BuddyTestClockRecord buddyMakeTestClockRecord(
    uint32_t sequence, const BuddyAcceleratedTime &time) {
  BuddyTestClockRecord record = {BUDDY_TEST_CLOCK_MAGIC, sequence, 0, time};
  record.checksum = buddyTestClockChecksum(record);
  return record;
}
