#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "buddy_nearby_protocol.h"

// Private, half-duplex data on the USB-C cable's D- conductor. This is not
// USB signalling. Both ends only pull the line low; the released state is
// supplied by the ESP32's internal pull-up. D+ is never driven in cable mode.
static constexpr uint8_t BUDDY_CABLE_SYNC = 0xA5;
static constexpr size_t BUDDY_CABLE_FRAME_BYTES = NEAR_PACKET_BYTES + 4;
static constexpr uint32_t BUDDY_CABLE_BIT_US = 2000; // 500 bit/s, generous rise time
static constexpr uint32_t BUDDY_CABLE_BYTE_GAP_US = 5 * BUDDY_CABLE_BIT_US;

inline uint8_t buddyCableExpression(uint16_t mood, uint16_t comfort, uint8_t curiosity) {
  uint8_t m = mood >= 1000 ? 7 : (uint32_t)mood * 7 / 1000;
  uint8_t c = comfort >= 1000 ? 7 : (uint32_t)comfort * 7 / 1000;
  uint8_t q = curiosity >= 100 ? 3 : (uint32_t)curiosity * 3 / 100;
  return (m << 5) | (c << 2) | q;
}
inline uint16_t buddyCableMood(uint8_t expression) {
  return ((expression >> 5) & 7) * 1000 / 7;
}
inline uint16_t buddyCableComfort(uint8_t expression) {
  return ((expression >> 2) & 7) * 1000 / 7;
}
inline uint8_t buddyCableCuriosity(uint8_t expression) {
  return (expression & 3) * 100 / 3;
}

inline bool buddyCablePortraitChanged(const NearbyPacket &oldPeer,
                                      const NearbyPacket &newPeer) {
  return !oldPeer.sender || oldPeer.sender != newPeer.sender ||
    oldPeer.stage != newPeer.stage || oldPeer.expression != newPeer.expression ||
    oldPeer.girl != newPeer.girl ||
    memcmp(&oldPeer.visual, &newPeer.visual, sizeof(BuddyVisualGenome)) != 0;
}

inline uint8_t buddyCableCrc(const uint8_t *data, size_t length) {
  uint8_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  }
  return crc;
}

inline void encodeBuddyCableFrame(const NearbyPacket &packet,
                                  uint8_t out[BUDDY_CABLE_FRAME_BYTES]) {
  out[0] = BUDDY_CABLE_SYNC;
  encodeNearbyPacket(packet, out + 1);
  out[NEAR_PACKET_BYTES + 1] = packet.moment | (packet.girl ? 0x80 : 0);
  out[NEAR_PACKET_BYTES + 2] = packet.expression;
  out[BUDDY_CABLE_FRAME_BYTES - 1] = buddyCableCrc(out, BUDDY_CABLE_FRAME_BYTES - 1);
}

inline bool decodeBuddyCableFrame(const uint8_t *data, size_t length,
                                  NearbyPacket &packet) {
  if (!data || length != BUDDY_CABLE_FRAME_BYTES || data[0] != BUDDY_CABLE_SYNC ||
      data[length - 1] != buddyCableCrc(data, length - 1) ||
      (data[NEAR_PACKET_BYTES + 1] & 0x40) ||
      (data[NEAR_PACKET_BYTES + 1] & 0x3f) >= MOMENT_COUNT ||
      !decodeNearbyPacket(data + 1, NEAR_PACKET_BYTES, packet)) return false;
  packet.moment = data[NEAR_PACKET_BYTES + 1] & 0x3f;
  packet.expression = data[NEAR_PACKET_BYTES + 2];
  packet.girl = (data[NEAR_PACKET_BYTES + 1] & 0x80) != 0;
  return true;
}
