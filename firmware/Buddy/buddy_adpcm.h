#pragma once
#include <stdint.h>

// Standard IMA ADPCM. Each clip resets its predictor and step index, so
// playback is deterministic and cannot leak state across successive sounds.
struct BuddyAdpcmDecoder {
  int32_t predictor;
  int16_t index;

  explicit BuddyAdpcmDecoder(int16_t first) : predictor(first), index(0) {}

  int16_t next(uint8_t code) {
    static const uint16_t stepTable[89] = {
      7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,
      60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,
      307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,
      1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,
      4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,
      12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767
    };
    static const int8_t indexChange[8] = {-1,-1,-1,-1,2,4,6,8};
    const int32_t step = stepTable[index];
    int32_t change = step >> 3;
    if (code & 4) change += step;
    if (code & 2) change += step >> 1;
    if (code & 1) change += step >> 2;
    predictor += code & 8 ? -change : change;
    if (predictor < -32768) predictor = -32768;
    if (predictor > 32767) predictor = 32767;
    index += indexChange[code & 7];
    if (index < 0) index = 0;
    if (index > 88) index = 88;
    return (int16_t)predictor;
  }
};
