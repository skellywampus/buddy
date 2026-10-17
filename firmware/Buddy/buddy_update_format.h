#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

// Increment this for every public firmware release. The package builder reads
// this same value, and the device refuses equal/older packages.
#ifndef BUDDY_FIRMWARE_VERSION
#define BUDDY_FIRMWARE_VERSION 2026101701UL
#endif
#define BUDDY_STRINGIFY_INNER(value) #value
#define BUDDY_STRINGIFY(value) BUDDY_STRINGIFY_INNER(value)
#define BUDDY_IMAGE_TAG "BUDDY:WS154V2-BW:" BUDDY_STRINGIFY(BUDDY_FIRMWARE_VERSION)

static const char BUDDY_UPDATE_PATH[] = "/BUDDY.UPD";
static const char BUDDY_UPDATE_TARGET[16] = "WS154V2-BW";
static const uint8_t BUDDY_UPDATE_MAGIC[8] = {'B','U','D','D','Y','U','P','1'};

struct BuddyUpdateHeader {
  uint8_t magic[8];
  uint32_t format;
  char target[16];
  uint32_t release;
  uint32_t imageSize;
  uint8_t imageSha256[32];
};
static_assert(sizeof(BuddyUpdateHeader) == 68, "Update header must remain portable");

enum BuddyUpdateCheck : uint8_t {
  UPDATE_READY, UPDATE_ALREADY_CURRENT, UPDATE_BAD_FILE, UPDATE_BAD_TARGET,
  UPDATE_BAD_SIZE
};

inline BuddyUpdateCheck buddyCheckUpdate(const BuddyUpdateHeader &header,
                                         uint32_t fileSize, uint32_t slotSize) {
  if (memcmp(header.magic, BUDDY_UPDATE_MAGIC, sizeof(header.magic)) || header.format != 1)
    return UPDATE_BAD_FILE;
  if (memcmp(header.target, BUDDY_UPDATE_TARGET, sizeof(header.target)))
    return UPDATE_BAD_TARGET;
  if (!header.release || !header.imageSize ||
      header.imageSize > slotSize || header.imageSize > UINT32_MAX - sizeof(header) ||
      fileSize != sizeof(header) + header.imageSize)
    return UPDATE_BAD_SIZE;
  if (header.release <= BUDDY_FIRMWARE_VERSION) return UPDATE_ALREADY_CURRENT;
  return UPDATE_READY;
}

// Stream the application itself, not just its editable package header. Includes
// the terminating NUL so a longer/version-suffixed marker cannot match.
struct BuddyUpdateImageCheck {
  char marker[48] = {};
  uint8_t first[16] = {};
  uint32_t bytes = 0;
  size_t matched = 0;
  bool found = false;
  explicit BuddyUpdateImageCheck(uint32_t release) {
    snprintf(marker, sizeof(marker), "BUDDY:WS154V2-BW:%luUL", (unsigned long)release);
  }
  void feed(const uint8_t *data, size_t length) {
    size_t markerSize = strlen(marker) + 1;
    for (size_t i = 0; i < length; ++i) {
      uint8_t byte = data[i];
      if (bytes < sizeof(first)) first[bytes] = byte;
      ++bytes;
      if (byte == (uint8_t)marker[matched]) ++matched;
      else matched = byte == (uint8_t)marker[0] ? 1 : 0;
      if (matched == markerSize) { found = true; matched = 0; }
    }
  }
  bool valid() const {
    return bytes >= 64 && first[0] == 0xE9 && first[12] == 9 && first[13] == 0 && found;
  }
};
