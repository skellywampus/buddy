#pragma once
#include <stdint.h>
#include "buddy_types.h"

// Appearance is derived, never saved: existing lives keep their identity and
// an object does not change shape between refreshes or firmware restarts.
inline uint8_t buddyItemStyle(uint32_t lifeSeed, uint16_t generation, uint8_t kind) {
  uint32_t n = lifeSeed ^ (uint32_t(generation) * 0x9e3779b9UL) ^
               (uint32_t(kind + 1) * 0x85ebca6bUL);
  n ^= n >> 16; n *= 0x7feb352dUL; n ^= n >> 15;
  return uint8_t(n % (kind == OBJECT_TOY ? 12 :
                        kind == OBJECT_PLANT || kind == OBJECT_FLOWERS ? 4 : 1));
}
