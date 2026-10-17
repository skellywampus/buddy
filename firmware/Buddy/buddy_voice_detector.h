#pragma once
#include <stdint.h>
#include <stddef.h>

// A deliberately small, non-recording speech-presence detector. It detects
// sustained level above an adaptive room floor; it cannot recognize words or
// distinguish a voice from every other voice-like sound.
struct BuddyVoiceDetector {
  uint32_t noise = 0;
  uint16_t voicedMs = 0;
  uint16_t quietMs = 0;
  uint32_t lastAcceptedMs = 0;
  uint16_t lastLevel = 0;

  uint32_t threshold() const {
    // Halve effective detection sensitivity without changing the codec's
    // capture path: speech must now clear twice the previous adaptive gate.
    uint32_t value = ((noise / 8) * 2 + 90) * 2;
    return value < 460 ? 460 : value;
  }

  void reset() { noise = 0; voicedMs = quietMs = 0; lastLevel = 0; }

  bool feed(uint16_t level, uint16_t durationMs, uint32_t nowMs) {
    lastLevel = level;
    if (!noise) noise = (uint32_t)level * 8 + 8;
    bool loud = level > threshold();
    if (!loud) {
      // Follow a changing room slowly, without raising the baseline during a voice.
      noise = (noise * 31 + (uint32_t)level * 8) / 32;
    }
    if (loud) {
      if (voicedMs < 2000) voicedMs += durationMs;
      quietMs = 0;
    } else if (voicedMs) {
      if (quietMs < 2000) quietMs += durationMs;
      if (quietMs >= 240) {
        bool heard = voicedMs >= 480 &&
          (!lastAcceptedMs || (uint32_t)(nowMs - lastAcceptedMs) >= 20UL * 1000UL);
        voicedMs = quietMs = 0;
        if (heard) { lastAcceptedMs = nowMs; return true; }
      }
    }
    return false;
  }
};

static uint16_t buddyVoiceLevel(const int16_t *samples, size_t stereoFrames) {
  if (!samples || !stereoFrames) return 0;
  int32_t leftMean = 0, rightMean = 0;
  for (size_t i = 0; i < stereoFrames; i++) {
    leftMean += samples[2*i]; rightMean += samples[2*i+1];
  }
  leftMean /= (int32_t)stereoFrames; rightMean /= (int32_t)stereoFrames;
  uint32_t left = 0, right = 0;
  for (size_t i = 0; i < stereoFrames; i++) {
    int32_t a = (int32_t)samples[2*i] - leftMean;
    int32_t b = (int32_t)samples[2*i+1] - rightMean;
    left += (uint32_t)(a < 0 ? -a : a);
    right += (uint32_t)(b < 0 ? -b : b);
  }
  uint32_t mean = (left > right ? left : right) / stereoFrames;
  return mean > 65535 ? 65535 : (uint16_t)mean;
}
