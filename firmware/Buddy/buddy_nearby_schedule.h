#pragma once
#include <stdint.h>

// Wireless discovery is retired. Keep policy helpers for saved encounter and
// host regression tests while the wired transport hardware is unresolved.
static constexpr bool BUDDY_NEARBY_STACK_ENABLED = false;

// BLE and full offline speech take turns using the scarce internal RAM.
static constexpr uint32_t buddyNearbyBurstLimit(bool encounterActive) {
  return encounterActive ? 45000UL : 30000UL;
}
static constexpr uint32_t buddyNearbyBurstInterval(bool encounterActive, bool voiceSearch) {
  return encounterActive || voiceSearch ? 30000UL : 5UL * 60UL * 1000UL;
}
static constexpr uint32_t BUDDY_NEARBY_IDLE_SCAN_INTERVAL_MS = 5UL * 60UL * 1000UL;
static constexpr uint32_t buddyNearbyInitialLastScanMs(uint32_t now) {
  return now - BUDDY_NEARBY_IDLE_SCAN_INTERVAL_MS;
}
static constexpr bool buddyNearbyScanDue(uint32_t now, uint32_t last, uint32_t interval) {
  return now - last >= interval;
}
static constexpr bool buddyNearbyBootGuard(bool interruptedInit,
                                           uint32_t blockedRelease,
                                           uint32_t currentRelease) {
  return interruptedInit || blockedRelease == currentRelease;
}
