#pragma once
#include <stdint.h>
struct RtcDateTime {
  uint16_t year;
  uint8_t month, day, hour, minute, second;
};

struct Shtc3Reading {
  bool ok;
  float tempC;
  float humidity;
};

struct TouchPoint {
  bool ok;
  uint16_t x, y;
};

enum BuddyStage : uint8_t {
  STAGE_BABY, STAGE_CHILD, STAGE_YOUNG, STAGE_ADULT, STAGE_ELDER, STAGE_GONE
};

enum BuddyBehavior : uint8_t {
  BEHAVIOR_SLEEP, BEHAVIOR_REST, BEHAVIOR_PLAY, BEHAVIOR_EXPLORE,
  BEHAVIOR_OBSERVE, BEHAVIOR_DAYDREAM, BEHAVIOR_SELF_SOOTHE,
  BEHAVIOR_WARM_UP, BEHAVIOR_COOL_DOWN, BEHAVIOR_COUNT
};

enum BuddyView : uint8_t {
  VIEW_BUDDY, VIEW_ABOUT, VIEW_SETTINGS, VIEW_COUNT
};

enum OrientationMode : uint8_t {
  ORIENTATION_AUTO, ORIENTATION_NORMAL, ORIENTATION_FLIPPED, ORIENTATION_COUNT
};

enum ButtonEvent : uint8_t {
  BUTTON_NONE, BUTTON_SHORT, BUTTON_DOUBLE, BUTTON_LONG, BUTTON_RECOVER
};

enum BuddySound : uint8_t { SOUND_NORMAL, SOUND_SLEEP, SOUND_WAKE, SOUND_SPECIAL };
static constexpr uint8_t BUDDY_SOUND_CLIPS = 8;
static uint8_t buddyRandomSoundIndex(uint32_t value) {
  return (uint8_t)(value % BUDDY_SOUND_CLIPS);
}

enum MemoryType : uint8_t {
  MEMORY_BIRTH, MEMORY_STAGE, MEMORY_INTERACTION, MEMORY_COLD,
  MEMORY_HOT, MEMORY_MILESTONE, MEMORY_DEATH
};

struct BuddyMemory {
  uint32_t epoch;
  uint8_t type;
  int8_t intensity;
};

struct BuddyTraits {
  uint8_t curiosity;
  uint8_t sociability;
  uint8_t playfulness;
  uint8_t resilience;
  uint8_t sensitivity;
  uint8_t independence;
};

struct PriorLife {
  uint16_t generation;
  uint16_t livedDays;
  uint16_t finalBond;
  uint8_t dominantBehavior;
  BuddyTraits traits;
  uint32_t deathEpoch;
};

// Frozen v1 disk layout. Never edit: migration must retain the original life.
struct BuddyStateV1 {
  uint32_t magic;
  uint8_t version;
  bool alive;
  bool soundEnabled;
  bool memorialShown;
  uint16_t generation;
  uint32_t seed;
  uint32_t rngState;
  uint32_t birthEpoch;
  uint32_t lastUpdateEpoch;
  uint32_t deathEpoch;
  uint32_t lastLoggedDay;
  BuddyTraits traits;
  uint16_t energy;
  uint16_t health;
  uint16_t social;
  uint16_t play;
  uint16_t curiosityNeed;
  uint16_t security;
  uint16_t comfort;
  uint16_t mood;
  uint16_t bond;
  int16_t behaviorLearning[BEHAVIOR_COUNT];
  uint32_t behaviorCounts[BEHAVIOR_COUNT];
  uint8_t currentBehavior;
  uint8_t currentStage;
  uint8_t memoryHead;
  uint8_t memoryCount;
  BuddyMemory memories[16];
  uint32_t totalInteractions;
  uint32_t comfortableHours;
  uint32_t coldHours;
  uint32_t hotHours;
  PriorLife prior;
};

enum WorldObject : uint8_t {
  OBJECT_STONE, OBJECT_PLANT, OBJECT_NOOK, OBJECT_TOY,
  OBJECT_GROVE, OBJECT_FLOWERS, OBJECT_JOURNAL, OBJECT_INSTRUMENT,
  OBJECT_COUNT
};
static const uint8_t OLD_OBJECT_COUNT = 4;
enum LifeEvent : uint8_t { EVENT_BORN, EVENT_FOUND, EVENT_VISITED, EVENT_TENDED,
  EVENT_PLAYED, EVENT_RESTED, EVENT_ATTENTION, EVENT_GREW, EVENT_INHERITED,
  EVENT_WATERED, EVENT_POLISHED, EVENT_MENDED, EVENT_TIDIED, EVENT_INVENTED,
  EVENT_PRACTICED, EVENT_GARDENED, EVENT_SCULPTED, EVENT_BUILT_FORT,
  EVENT_CHARTED, EVENT_COLLECTED, EVENT_TOLD_STORY, EVENT_DECORATED,
  EVENT_MEDITATED, EVENT_PROJECT_STARTED, EVENT_PROJECT_WORKED,
  EVENT_PROJECT_FINISHED, EVENT_HOUSE_STARTED, EVENT_HOUSE_PROGRESS,
  EVENT_HOUSE_FINISHED, EVENT_HOUSE_REPAIRED, EVENT_HOUSE_COLLAPSED,
  EVENT_HOUSE_INHERITED, EVENT_MET_BUDDY, EVENT_SHARED_ACTIVITY,
  EVENT_FAMILY_CUSTOM, EVENT_AMBITION_STARTED, EVENT_AMBITION_FULFILLED,
  EVENT_AMBITION_RELEASED, EVENT_LEGACY_LOST,
  EVENT_NATURE, EVENT_FLOWERS, EVENT_JOURNAL, EVENT_MUSIC, EVENT_COUNT };
struct BuddyProject {
  uint8_t object;
  uint8_t steps;
  uint32_t startedEpoch;
};
enum BuddyMoment : uint8_t {
  MOMENT_STRETCH, MOMENT_WANDER, MOMENT_HUM, MOMENT_TOY, MOMENT_PLANT,
  MOMENT_STONE, MOMENT_NOOK, MOMENT_WATCH, MOMENT_DAYDREAM, MOMENT_GROOM,
  MOMENT_DANCE, MOMENT_LISTEN, MOMENT_VISIT, MOMENT_NAP, MOMENT_WARM,
  MOMENT_COOL, MOMENT_WATER, MOMENT_POLISH, MOMENT_MEND, MOMENT_TIDY,
  MOMENT_BUILD, MOMENT_EXERCISE, MOMENT_STUDY, MOMENT_PATROL,
  MOMENT_GARDEN, MOMENT_SCULPT, MOMENT_TRICK, MOMENT_FORT, MOMENT_SORT,
  MOMENT_MEDITATE, MOMENT_STARGAZE, MOMENT_SING, MOMENT_SKETCH, MOMENT_MAP,
  MOMENT_COLLECT, MOMENT_STORY, MOMENT_DECORATE, MOMENT_REPAIR_NOOK,
  MOMENT_BALANCE, MOMENT_CELEBRATE,
  MOMENT_NATURE, MOMENT_SMELL_FLOWERS, MOMENT_JOURNAL, MOMENT_MUSIC,
  MOMENT_COUNT
};
struct WorldMemory {
  uint32_t epoch;
  uint8_t event, object, behavior;
  int8_t pleasure;
};
// Frozen v2 layout (four world anchors). Old slots must remain readable.
struct BuddyStateV2 : BuddyStateV1 {
  uint8_t worldVersion;
  bool stayAwake;
  uint8_t possessions;
  uint8_t favoriteObject;
  uint16_t affection[OLD_OBJECT_COUNT];
  uint16_t visits[OLD_OBJECT_COUNT];
  uint8_t plantGrowth;
  uint8_t inheritedObject;
  uint8_t familyEar, familyMarking;
  uint8_t eventHead, eventCount;
  WorldMemory events[24];
  BuddyTraits birthTraits;
  int16_t development[6];
  uint32_t lastDevelopmentDay;
  uint32_t lastWorldEpoch;
  uint32_t lastAttentionEpoch;
};
struct BuddyState : BuddyStateV1 {
  uint8_t worldVersion;
  bool stayAwake;
  uint8_t possessions;
  uint8_t favoriteObject;
  uint16_t affection[OBJECT_COUNT];
  uint16_t visits[OBJECT_COUNT];
  uint8_t plantGrowth;
  uint8_t inheritedObject;
  uint8_t familyEar, familyMarking;
  uint8_t eventHead, eventCount;
  WorldMemory events[24];
  BuddyTraits birthTraits;
  int16_t development[6];
  uint32_t lastDevelopmentDay;
  uint32_t lastWorldEpoch;
  uint32_t lastAttentionEpoch;
};
static_assert(sizeof(BuddyStateV1) == 284, "Original ESP32 Buddy save ABI must remain unchanged");
static_assert(sizeof(BuddyStateV2) == 536, "Four-anchor v2 save ABI must remain unchanged");

static const uint32_t BUDDY_MAGIC = 0x42554459;
static const uint8_t BUDDY_STATE_VERSION = 3;
static const uint16_t BUDDY_MEAN_LIFESPAN_DAYS = 365;

// The two identically distributed halves cancel in expectation. A life lasts
// 305-425 days, with most individuals nearer the 365-day mean. Deriving this
// from the committed seed avoids a new save field or a reroll after reboot.
static uint16_t buddyLifespanDays(uint32_t seed) {
  uint32_t mixed = seed ^ 0xA5F15EEDUL;
  mixed ^= mixed >> 16;
  mixed *= 0x7FEB352DUL;
  mixed ^= mixed >> 15;
  mixed *= 0x846CA68BUL;
  mixed ^= mixed >> 16;
  return BUDDY_MEAN_LIFESPAN_DAYS + (mixed & 0xFFFFU) % 61U -
    ((mixed >> 16) & 0xFFFFU) % 61U;
}
