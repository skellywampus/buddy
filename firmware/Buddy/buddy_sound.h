#pragma once
#include <stdint.h>

enum BuddySoundMode : uint8_t { SOUND_OFF = 0, SOUND_LOW = 1, SOUND_HIGH = 2 };

// Sleep, wake, milestones and everyday actions share the same eight sounds.
// Only a deliberate USB clip test pins a particular index.
inline uint8_t buddySoundClipIndex(BuddySound event, uint32_t random,
                                   int8_t fixedVariant = -1) {
  (void)event;
  return fixedVariant >= 0 && fixedVariant < BUDDY_SOUND_CLIPS ?
    (uint8_t)fixedVariant : buddyRandomSoundIndex(random);
}

inline bool buddySoundModeValid(uint8_t value) { return value <= SOUND_HIGH; }
inline BuddySoundMode buddySoundFromLegacy(bool enabled) {
  return enabled ? SOUND_LOW : SOUND_OFF;
}
inline BuddySoundMode buddyNextSoundMode(BuddySoundMode mode) {
  return (BuddySoundMode)((mode + 1) % 3);
}
template<typename Store> inline bool buddyPersistSoundMode(Store &store, BuddySoundMode mode) {
  return buddySoundModeValid(mode) && store.putUChar("sound-mode", mode) == 1 &&
    store.getUChar("sound-mode", 255) == mode;
}
template<typename Store> inline BuddySoundMode buddyLoadSoundMode(Store &store,
                                                                  bool legacyEnabled,
                                                                  bool &saveFailed) {
  uint8_t stored = store.getUChar("sound-mode", 255);
  BuddySoundMode mode = buddySoundModeValid(stored) ? (BuddySoundMode)stored :
    buddySoundFromLegacy(legacyEnabled);
  saveFailed = stored != mode && !buddyPersistSoundMode(store, mode);
  return mode;
}
// Keep the codec's previously audible 0xBF setting. HIGH restores the
// supplied signed 16-bit PCM without raising the analog amplifier gain.
inline int16_t buddySoundSample(int16_t sample, BuddySoundMode mode) {
  return mode == SOUND_OFF ? 0 : mode == SOUND_HIGH ? sample : sample / 2;
}
inline int16_t buddySoundMix(int64_t weighted, uint32_t weight) {
  if (!weight) return 0;
  int64_t sample = weighted / weight;
  if (sample > INT16_MAX) sample = INT16_MAX;
  if (sample < INT16_MIN) sample = INT16_MIN;
  return (int16_t)sample;
}
