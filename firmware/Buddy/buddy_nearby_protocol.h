#pragma once
#include <stdint.h>
#include <stddef.h>

// A public drawing recipe, never the life seed or the hardware MAC address.
struct BuddyVisualGenome {
  uint8_t headShape, earStyle, eyeStyle, markingStyle;
  uint8_t tuftStyle, tailStyle, muzzleStyle;
};

enum NearbyActivity : uint8_t {
  NEAR_NONE, NEAR_PLAY, NEAR_EXPLORE, NEAR_REST,
  NEAR_STORY, NEAR_BUILD, NEAR_GARDEN, NEAR_WATCH
};

struct NearbyPacket {
  uint32_t sender, target;
  BuddyVisualGenome visual;
  uint8_t stage, activity;
  bool confirmed;
  // Cable-only live expression and one of Buddy's complete solo moments.
  // These are never written to the legacy 12-byte Nearby advertisement/save.
  uint8_t moment, expression;
  bool girl;
};

// Twelve bytes of service data plus a private 128-bit service UUID fit in a
// legacy 31-byte advertisement. The version is encoded in the high meta bit.
static const size_t NEAR_PACKET_BYTES = 12;
static const uint32_t NEAR_MAGIC = 0x424E4541UL;
static const uint8_t NEAR_VERSION = 1;

struct NearbyContact {
  uint32_t peerId, lastMeetEpoch;
  uint16_t meetings;
  uint8_t affinity, lastActivity;
};

// Own checksummed NVS slots. BuddyState v2 and the house save remain unchanged.
struct NearbyState {
  uint32_t magic, sequence, checksum, lifeSeed, selfId;
  NearbyContact contacts[4];
  uint8_t version, reserved[3];
};
static_assert(sizeof(NearbyContact) == 12, "Nearby contact ABI");
static_assert(sizeof(NearbyState) == 72, "Nearby save ABI");

enum NearbyPhase : uint8_t { NEAR_IDLE, NEAR_INVITING, NEAR_ACCEPTING, NEAR_SHARED };
struct NearbyEncounter {
  NearbyPacket peer;
  uint32_t startedMs, lastPeerMs, sharedMs;
  uint8_t activity, phase;
  bool sceneClosed;
};
struct NearbySeen {
  NearbyPacket packet;
  uint32_t lastMs;
  int8_t rssi;
};

static bool validNearbyVisual(const BuddyVisualGenome &v) {
  return v.headShape < 4 && v.earStyle < 6 && v.eyeStyle < 5 &&
    v.markingStyle < 6 && v.tuftStyle < 5 && v.tailStyle < 4 && v.muzzleStyle < 4;
}

static void encodeNearbyPacket(const NearbyPacket &packet, uint8_t out[NEAR_PACKET_BYTES]) {
  for (uint8_t i = 0; i < 4; i++) {
    out[i] = (uint8_t)(packet.sender >> (i * 8));
    out[4 + i] = (uint8_t)(packet.target >> (i * 8));
  }
  uint32_t visual = packet.visual.headShape | ((uint32_t)packet.visual.earStyle << 2) |
    ((uint32_t)packet.visual.eyeStyle << 5) | ((uint32_t)packet.visual.markingStyle << 8) |
    ((uint32_t)packet.visual.tuftStyle << 11) | ((uint32_t)packet.visual.tailStyle << 14) |
    ((uint32_t)packet.visual.muzzleStyle << 16);
  out[8] = visual;
  out[9] = visual >> 8;
  out[10] = visual >> 16;
  out[11] = 0x80 | (packet.confirmed ? 0x40 : 0) |
    ((packet.activity & 7) << 3) | (packet.stage & 7);
}

static bool decodeNearbyPacket(const uint8_t *bytes, size_t length, NearbyPacket &packet) {
  if (!bytes || length != NEAR_PACKET_BYTES || !(bytes[11] & 0x80)) return false;
  packet = {};
  for (uint8_t i = 0; i < 4; i++) {
    packet.sender |= (uint32_t)bytes[i] << (i * 8);
    packet.target |= (uint32_t)bytes[4 + i] << (i * 8);
  }
  uint32_t visual = (uint32_t)bytes[8] | ((uint32_t)bytes[9] << 8) |
    ((uint32_t)bytes[10] << 16);
  if (visual & 0xFC0000UL) return false; // unused bits must stay zero
  packet.visual = {(uint8_t)(visual & 3), (uint8_t)((visual >> 2) & 7),
    (uint8_t)((visual >> 5) & 7), (uint8_t)((visual >> 8) & 7),
    (uint8_t)((visual >> 11) & 7), (uint8_t)((visual >> 14) & 3),
    (uint8_t)((visual >> 16) & 3)};
  packet.stage = bytes[11] & 7;
  packet.activity = (bytes[11] >> 3) & 7;
  packet.confirmed = bytes[11] & 0x40;
  return packet.sender && packet.stage <= STAGE_ELDER && validNearbyVisual(packet.visual);
}
