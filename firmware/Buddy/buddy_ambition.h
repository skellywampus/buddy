#pragma once
#include "buddy_types.h"

static const uint32_t BUDDY_AMBITION_MAGIC = 0x4241494DUL;
static const uint8_t BUDDY_AMBITION_VERSION = 2;
static const uint16_t BUDDY_AMBITION_TARGET = 96;

enum BuddyAmbitionOutcome : uint8_t {
  AMBITION_NONE, AMBITION_FULFILLED, AMBITION_ABANDONED
};

// Independent two-slot NVS record. The established BuddyState v2 layout is
// untouched; a missing record means an existing life has not formed a goal yet.
struct BuddyAmbitionState {
  uint32_t magic, sequence, checksum;
  uint32_t lifeSeed, birthEpoch, startedEpoch, lastWorkDay, lastEndedEpoch;
  uint16_t generation, progress;
  uint8_t version, kind, lastKind, lastOutcome, legacyKind, legacyStrength;
};
static_assert(sizeof(BuddyAmbitionState) == 44, "Ambition save ABI changed");

static int buddyAmbitionAffinity(const BuddyState &life, uint8_t kind) {
  if (kind >= OBJECT_COUNT || !(life.possessions & (1 << kind))) return -10000;
  int drive = kind == OBJECT_STONE ? life.traits.curiosity * 3 + life.traits.independence :
    kind == OBJECT_PLANT ? life.traits.sensitivity * 2 + life.traits.resilience * 2 :
    kind == OBJECT_NOOK ? life.traits.independence * 2 + life.traits.resilience * 2 :
    kind == OBJECT_GROVE ? life.traits.curiosity * 2 + life.traits.resilience * 2 :
    kind == OBJECT_FLOWERS ? life.traits.sensitivity * 3 + life.traits.curiosity :
    kind == OBJECT_JOURNAL ? life.traits.sensitivity * 2 + life.traits.independence * 2 :
    life.traits.playfulness * 3 + life.traits.sociability;
  return drive + life.affection[kind] / 3 +
    (life.favoriteObject == kind ? 55 : 0) +
    (life.inheritedObject == kind ? 25 : 0);
}

static uint8_t buddyAmbitionWork(uint8_t kind, LifeEvent event, uint8_t object) {
  if (kind >= OBJECT_COUNT || object != kind) return 0;
  if (event == EVENT_PROJECT_WORKED || event == EVENT_PROJECT_FINISHED ||
      event == EVENT_FAMILY_CUSTOM) return 3;
  if (event == EVENT_VISITED || event == EVENT_RESTED || event == EVENT_PLAYED ||
      event == EVENT_TENDED) return 2;
  if (kind == OBJECT_STONE && (event == EVENT_POLISHED || event == EVENT_SCULPTED ||
      event == EVENT_CHARTED || event == EVENT_COLLECTED)) return 4;
  if (kind == OBJECT_PLANT && (event == EVENT_WATERED || event == EVENT_GARDENED)) return 4;
  if (kind == OBJECT_NOOK && (event == EVENT_TIDIED || event == EVENT_DECORATED ||
      event == EVENT_HOUSE_PROGRESS || event == EVENT_HOUSE_REPAIRED)) return 4;
  if (kind == OBJECT_TOY && (event == EVENT_MENDED || event == EVENT_PRACTICED ||
      event == EVENT_INVENTED)) return 4;
  if (kind == OBJECT_GROVE && event == EVENT_NATURE) return 4;
  if (kind == OBJECT_FLOWERS && event == EVENT_FLOWERS) return 4;
  if (kind == OBJECT_JOURNAL && (event == EVENT_JOURNAL || event == EVENT_TOLD_STORY)) return 4;
  if (kind == OBJECT_INSTRUMENT && event == EVENT_MUSIC) return 4;
  return 0;
}
