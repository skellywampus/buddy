#pragma once
#include <stdint.h>
// Uses elapsed ticks, not absolute deadlines; safe across timer rollover.
template<class Busy, class Clock, class Pause>
bool epaperWaitIdle(Busy busy, Clock clock, Pause pause, uint32_t timeout) {
  uint32_t start = clock();
  while (busy()) {
    if ((uint32_t)(clock() - start) >= timeout) return false;
    pause();
  }
  return true;
}
