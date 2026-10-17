#pragma once
#include <stdint.h>

// All eight clips are eligible. A tune uses five distinct clips; a new seed
// makes a new phrase.
static constexpr uint8_t BUDDY_TUNE_CLIPS = 5;
struct BuddyTunePlan { uint8_t clip[BUDDY_TUNE_CLIPS]; };

inline BuddyTunePlan buddyTunePlan(uint32_t seed) {
  BuddyTunePlan plan = {};
  uint8_t pool[BUDDY_SOUND_CLIPS] = {0, 1, 2, 3, 4, 5, 6, 7};
  for (uint8_t i = 0; i < BUDDY_TUNE_CLIPS; ++i) {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    uint8_t pick = i + seed % (BUDDY_SOUND_CLIPS - i);
    uint8_t swap = pool[i]; pool[i] = pool[pick]; pool[pick] = swap;
    plan.clip[i] = pool[i];
  }
  return plan;
}

// Start each following clip one second before the previous clip ends, even
// when source lengths change. The incoming and outgoing tails crossfade.
static constexpr uint32_t BUDDY_TUNE_OVERLAP_FRAMES = 16000;
inline uint32_t buddyTuneNextStart(uint32_t previousStart, uint32_t previousFrames) {
  return previousStart + previousFrames -
    (previousFrames < BUDDY_TUNE_OVERLAP_FRAMES ? previousFrames : BUDDY_TUNE_OVERLAP_FRAMES);
}

inline uint32_t buddyTuneWeight(uint32_t local, uint32_t frames,
                                bool first, bool last) {
  uint32_t weight = BUDDY_TUNE_OVERLAP_FRAMES;
  if (!first && local < weight) weight = local;
  if (!last && frames - 1 - local < weight) weight = frames - 1 - local;
  return weight;
}
