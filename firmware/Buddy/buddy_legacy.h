#pragma once
#include "buddy_types.h"

static const uint32_t BUDDY_LEGACY_MAGIC = 0x424C4743UL;
static const uint8_t BUDDY_LEGACY_VERSION = 2;
static const uint8_t BUDDY_LEGACY_NEW_CONDITION = 72;

// A family site's handmade marks outlive a life and its 24-event history.
// This record is independent of the frozen v2 life and v1 ambition layouts.
struct BuddyLegacyStateV1 {
  uint32_t magic, sequence, checksum, lastWeatherDay;
  uint32_t lastCareDay[OLD_OBJECT_COUNT];
  uint16_t makerGeneration[OLD_OBJECT_COUNT];
  uint8_t condition[OLD_OBJECT_COUNT];
  uint8_t version, reserved[3];
};
static_assert(sizeof(BuddyLegacyStateV1) == 48, "Legacy v1 save ABI changed");
struct BuddyLegacyState {
  uint32_t magic, sequence, checksum, lastWeatherDay;
  uint32_t lastCareDay[OBJECT_COUNT];
  uint16_t makerGeneration[OBJECT_COUNT];
  uint8_t condition[OBJECT_COUNT];
  uint8_t version, reserved[3];
};
static_assert(sizeof(BuddyLegacyState) == 76, "Eight-anchor site save ABI changed");

static bool buddyLegacyUsable(const BuddyLegacyState &site, uint8_t object) {
  return object < OBJECT_COUNT && site.condition[object] > 0 &&
    site.makerGeneration[object] > 0;
}

// A surviving family work can call to an heir, but inheritance alone does not
// make it a duty. The same life and day always make the same choice, including
// after a reset. Attachment, temperament, use and weathering all matter.
static uint8_t buddyLegacyInvitation(const BuddyState &life,
                                     const BuddyLegacyState &site,
                                     uint32_t epoch) {
  if (!life.alive || epoch < life.birthEpoch ||
      epoch - life.birthEpoch < 15UL * 86400UL) return OBJECT_COUNT;
  uint8_t best = OBJECT_COUNT;
  int bestScore = 310;
  for (uint8_t object = 0; object < OBJECT_COUNT; object++) {
    if (!buddyLegacyUsable(site, object) ||
        site.makerGeneration[object] == life.generation ||
        !(life.possessions & (1 << object))) continue;
    int trait = object == OBJECT_STONE || object == OBJECT_GROVE ? life.traits.curiosity :
      object == OBJECT_PLANT || object == OBJECT_FLOWERS || object == OBJECT_JOURNAL ?
        life.traits.sensitivity :
      object == OBJECT_NOOK ? life.traits.sociability : life.traits.playfulness;
    uint32_t mixed = life.seed ^ ((epoch / 86400UL) * 0x9E3779B9UL) ^
      ((uint32_t)(object + 1) * 0x85EBCA6BUL);
    mixed ^= mixed >> 16;
    mixed *= 0x7FEB352DUL;
    mixed ^= mixed >> 15;
    int attachment = life.affection[object];
    int score = trait * 2 + attachment / 3 +
      (life.visits[object] < 20 ? life.visits[object] : 20) * 3 +
      (life.inheritedObject == object ? 80 : 0) +
      (life.favoriteObject == object ? 50 : 0) +
      (100 - site.condition[object]) * (attachment < 240 ? attachment : 240) / 240 +
      (mixed % 120);
    if (score > bestScore) { bestScore = score; best = object; }
  }
  return best;
}
