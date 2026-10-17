#pragma once
#include "buddy_types.h"

// A transient reading of Buddy's durable needs and experiences. No additional
// save record is needed: the underlying needs, traits, attachments, house-loss
// event and learned behavior preferences already survive reboot.
enum BuddyConcern : uint8_t {
  CONCERN_NONE, CONCERN_EXHAUSTED, CONCERN_UNEASY, CONCERN_LONELY,
  CONCERN_RESTLESS, CONCERN_BORED, CONCERN_GRIEVING
};

struct BuddyInnerState {
  BuddyConcern concern;
  uint16_t pressure;
};

inline BuddyInnerState buddyInnerStateOf(const BuddyState &state, uint32_t epoch) {
  BuddyInnerState result = {CONCERN_NONE, 0};
  if (!state.alive) return result;
  auto consider = [&result](BuddyConcern concern, int pressure) {
    if (pressure > result.pressure) {
      result.concern = concern;
      result.pressure = pressure > 1000 ? 1000 : pressure;
    }
  };
  if (state.energy < 580) consider(CONCERN_EXHAUSTED, (580 - state.energy) * 2);
  if (state.health < 350) consider(CONCERN_EXHAUSTED, (350 - state.health) * 2);
  if (state.security < 520) consider(CONCERN_UNEASY, (520 - state.security) * 2);
  int socialThreshold = 300 + state.traits.sociability * 2 - state.traits.independence;
  if (socialThreshold < 260) socialThreshold = 260;
  if (socialThreshold > 500) socialThreshold = 500;
  if (state.social < socialThreshold)
    consider(CONCERN_LONELY, (socialThreshold - state.social) * 2);
  int curiosityThreshold = 340 + state.traits.curiosity * 2;
  if (state.curiosityNeed < curiosityThreshold)
    consider(CONCERN_RESTLESS, (curiosityThreshold - state.curiosityNeed) * 2);
  int playThreshold = 330 + state.traits.playfulness * 2;
  if (state.play < playThreshold)
    consider(CONCERN_BORED, (playThreshold - state.play) * 2);

  // A collapse matters in proportion to his attachment, but the feeling
  // naturally recedes over four days and never replaces immediate survival.
  for (uint8_t i = 0; i < state.eventCount && i < 24; i++) {
    const WorldMemory &event = state.events[i];
    if (event.event != EVENT_HOUSE_COLLAPSED || event.epoch > epoch) continue;
    uint32_t elapsed = epoch - event.epoch;
    static const uint32_t duration = 4UL * 86400UL;
    if (elapsed >= duration) continue;
    int attachment = state.affection[OBJECT_NOOK] / 5;
    int pressure = (int)((uint64_t)(600 + attachment) * (duration - elapsed) / duration);
    consider(CONCERN_GRIEVING, pressure);
  }
  return result;
}

inline int buddyInnerNeed(const BuddyState &state, BuddyConcern concern) {
  switch (concern) {
    case CONCERN_EXHAUSTED: return state.energy < state.health + 230 ?
      state.energy : state.health + 230;
    case CONCERN_UNEASY: return state.security;
    case CONCERN_LONELY: return state.social;
    case CONCERN_RESTLESS: return state.curiosityNeed;
    case CONCERN_BORED: return state.play;
    case CONCERN_GRIEVING: return state.security;
    default: return state.mood;
  }
}

inline bool buddyInnerAnchoredBy(const BuddyState &state, BuddyBehavior behavior) {
  uint8_t object = state.favoriteObject;
  if (object >= OBJECT_COUNT || !(state.possessions & (1 << object))) return false;
  switch (object) {
    case OBJECT_STONE: return behavior == BEHAVIOR_EXPLORE || behavior == BEHAVIOR_OBSERVE;
    case OBJECT_PLANT: return behavior == BEHAVIOR_OBSERVE || behavior == BEHAVIOR_REST;
    case OBJECT_NOOK: return behavior == BEHAVIOR_REST || behavior == BEHAVIOR_SELF_SOOTHE;
    case OBJECT_TOY: return behavior == BEHAVIOR_PLAY;
    case OBJECT_GROVE: return behavior == BEHAVIOR_EXPLORE;
    case OBJECT_FLOWERS: return behavior == BEHAVIOR_OBSERVE;
    case OBJECT_JOURNAL: return behavior == BEHAVIOR_DAYDREAM;
    case OBJECT_INSTRUMENT: return behavior == BEHAVIOR_PLAY;
    default: return false;
  }
}

inline int buddyInnerBehaviorBias(const BuddyState &state, BuddyInnerState inner,
                                  BuddyBehavior behavior) {
  int p = inner.pressure;
  int bias = 0;
  switch (inner.concern) {
    case CONCERN_EXHAUSTED:
      if (behavior == BEHAVIOR_SLEEP) bias = p * 4 / 5;
      if (behavior == BEHAVIOR_REST) bias = p * (state.currentStage == STAGE_ELDER ? 4 : 3) / 5;
      break;
    case CONCERN_UNEASY:
      if (behavior == BEHAVIOR_SELF_SOOTHE) bias = p * 4 / 5;
      if (behavior == BEHAVIOR_REST) bias = p * 2 / 5;
      break;
    case CONCERN_LONELY:
      if (behavior == BEHAVIOR_SELF_SOOTHE) bias = p * (50 + state.traits.independence / 2) / 100;
      if (behavior == BEHAVIOR_DAYDREAM) bias = p * (30 + state.traits.sensitivity / 3) / 100;
      break;
    case CONCERN_RESTLESS:
      if (behavior == BEHAVIOR_EXPLORE) bias = p * (state.currentStage == STAGE_ELDER ? 2 : 4) / 5;
      if (behavior == BEHAVIOR_OBSERVE) bias = p * (state.currentStage == STAGE_ELDER ? 4 : 2) / 5;
      break;
    case CONCERN_BORED:
      if (behavior == BEHAVIOR_PLAY) bias = p * 4 / 5;
      if (behavior == BEHAVIOR_EXPLORE) bias = p * 2 / 5;
      break;
    case CONCERN_GRIEVING:
      if (behavior == BEHAVIOR_SELF_SOOTHE) bias = p * 4 / 5;
      if (behavior == BEHAVIOR_DAYDREAM) bias = p * (state.currentStage == STAGE_ELDER ? 3 : 2) / 5;
      if (behavior == BEHAVIOR_REST) bias = p * 2 / 5;
      break;
    default: break;
  }
  if (inner.concern != CONCERN_NONE && buddyInnerAnchoredBy(state, behavior))
    bias += state.affection[state.favoriteObject] / 5;
  return bias;
}

inline int buddyInnerMomentBias(BuddyInnerState inner, BuddyMoment moment) {
  int p = inner.pressure;
  switch (inner.concern) {
    case CONCERN_EXHAUSTED:
      if (moment == MOMENT_NAP || moment == MOMENT_NOOK) return p * 4 / 5;
      if (moment == MOMENT_STRETCH || moment == MOMENT_GROOM) return p / 2;
      break;
    case CONCERN_UNEASY:
      if (moment == MOMENT_NOOK || moment == MOMENT_MEDITATE || moment == MOMENT_TIDY ||
          moment == MOMENT_SMELL_FLOWERS) return p * 4 / 5;
      if (moment == MOMENT_PATROL || moment == MOMENT_REPAIR_NOOK) return p / 2;
      break;
    case CONCERN_LONELY:
      if (moment == MOMENT_HUM || moment == MOMENT_SING || moment == MOMENT_STORY ||
          moment == MOMENT_JOURNAL || moment == MOMENT_MUSIC ||
          moment == MOMENT_VISIT) return p * 4 / 5;
      if (moment == MOMENT_LISTEN || moment == MOMENT_TOY) return p / 2;
      break;
    case CONCERN_RESTLESS:
      if (moment == MOMENT_WANDER || moment == MOMENT_MAP || moment == MOMENT_STUDY ||
          moment == MOMENT_COLLECT || moment == MOMENT_NATURE) return p * 4 / 5;
      if (moment == MOMENT_WATCH || moment == MOMENT_SKETCH) return p / 2;
      break;
    case CONCERN_BORED:
      if (moment == MOMENT_TOY || moment == MOMENT_DANCE || moment == MOMENT_TRICK ||
          moment == MOMENT_BUILD || moment == MOMENT_MUSIC) return p * 4 / 5;
      if (moment == MOMENT_FORT || moment == MOMENT_BALANCE) return p / 2;
      break;
    case CONCERN_GRIEVING:
      if (moment == MOMENT_NOOK || moment == MOMENT_MEDITATE || moment == MOMENT_STORY ||
          moment == MOMENT_JOURNAL ||
          moment == MOMENT_WATCH) return p * 4 / 5;
      if (moment == MOMENT_DAYDREAM || moment == MOMENT_STARGAZE) return p / 2;
      break;
    default: break;
  }
  return 0;
}

inline int buddyInnerLearningDelta(BuddyInnerState inner, int needBefore,
                                   int needAfter, int moodBefore, int moodAfter) {
  if (inner.concern == CONCERN_NONE) {
    int change = (moodAfter - moodBefore) / 18;
    return change < -2 ? -2 : change > 2 ? 2 : change;
  }
  int relief = needAfter - needBefore;
  if (relief >= 25) return 3;
  if (relief >= 8) return 2;
  if (relief > 0) return 1;
  if (relief <= -20) return -3;
  return -1;
}
