#pragma once
#include "buddy_cable_protocol.h"

// UART delivers bytes independent of loop/display scheduling. A partial frame
// expires after a silence longer than the old sender's two-byte yield gap.
struct BuddyCableStream {
  uint8_t bytes[BUDDY_CABLE_FRAME_BYTES] = {};
  size_t count = 0;
  uint32_t lastByteMs = 0;
};
static constexpr uint32_t BUDDY_CABLE_FRAME_GAP_MS = 80;

inline void buddyCableStreamReset(BuddyCableStream &stream) { stream.count = 0; }
inline bool buddyCableStreamPush(BuddyCableStream &stream, uint8_t byte,
                                  uint32_t nowMs, NearbyPacket &packet) {
  if (stream.count && (uint32_t)(nowMs - stream.lastByteMs) > BUDDY_CABLE_FRAME_GAP_MS)
    stream.count = 0;
  stream.lastByteMs = nowMs;
  if (!stream.count && byte != BUDDY_CABLE_SYNC) return false;
  stream.bytes[stream.count++] = byte;
  if (stream.count != BUDDY_CABLE_FRAME_BYTES) return false;
  stream.count = 0;
  return decodeBuddyCableFrame(stream.bytes, BUDDY_CABLE_FRAME_BYTES, packet);
}
