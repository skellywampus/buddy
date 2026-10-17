#pragma once
#include <stdint.h>

enum BuddyHousePhase : uint8_t {
  HOUSE_EMPTY, HOUSE_BUILDING, HOUSE_OCCUPIED, HOUSE_RUIN
};

// Separate, checksummed NVS record. The BuddyState v2 save ABI stays frozen.
// The record belongs to this little world, not to one generation.
struct BuddyHouseState {
  uint32_t magic, sequence, checksum;
  uint32_t siteSeed, lastDay, builtEpoch, collapsedEpoch;
  uint16_t work, condition, builderGeneration, cycles;
  uint8_t phase, style, version, reserved;
};
static_assert(sizeof(BuddyHouseState) == 40, "House save ABI must remain stable");

static const uint32_t BUDDY_HOUSE_MAGIC = 0x42484F4DUL;
static const uint8_t BUDDY_HOUSE_VERSION = 1;
static const uint16_t BUDDY_HOUSE_WORK_TARGET = 700;
