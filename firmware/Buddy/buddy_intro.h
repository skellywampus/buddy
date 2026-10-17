#pragma once
#include <stdint.h>

enum BuddyIntroStep : uint8_t { INTRO_FORM, INTRO_GIFT, INTRO_SPARK, INTRO_COMPLETE };
enum BuddySex : uint8_t { BUDDY_BOY, BUDDY_GIRL };
enum BuddySpark : uint8_t { SPARK_EXPLORE, SPARK_MAKE, SPARK_DREAM, SPARK_FRIEND };
static const uint32_t BUDDY_INTRO_MAGIC = 0x42494E54UL;
static const uint8_t BUDDY_INTRO_VERSION = 1;

// Independent of BuddyState's frozen save ABI. The final seed and birth epoch
// are committed before creating a life, so a power loss cannot reroll a baby.
struct BuddyIntroState {
  uint32_t magic, sequence, checksum, seed, birthEpoch;
  uint8_t version, step, cursor, sex, gift, spark, reserved[2];
};
static_assert(sizeof(BuddyIntroState) == 28, "Intro save ABI");

static uint8_t buddySexFromSeed(uint32_t seed) { return seed & 1 ? BUDDY_GIRL : BUDDY_BOY; }
static uint8_t introOptionCount(uint8_t step) { return step == INTRO_FORM ? 2 : 4; }

static void applyBuddySpark(BuddyTraits &traits, uint8_t spark) {
  uint8_t *primary = nullptr, *secondary = nullptr;
  switch (spark) {
    case SPARK_EXPLORE: primary=&traits.curiosity; secondary=&traits.independence; break;
    case SPARK_MAKE: primary=&traits.resilience; secondary=&traits.independence; break;
    case SPARK_DREAM: primary=&traits.sensitivity; secondary=&traits.curiosity; break;
    case SPARK_FRIEND: primary=&traits.sociability; secondary=&traits.playfulness; break;
    default: return;
  }
  *primary = *primary > 83 ? 95 : *primary + 12;
  *secondary = *secondary > 90 ? 95 : *secondary + 5;
}
