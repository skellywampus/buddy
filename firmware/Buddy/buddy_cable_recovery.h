#pragma once
#include <stdint.h>

static constexpr uint32_t BUDDY_CABLE_DISPLAY_GRACE_MS = 15000UL;
static constexpr uint32_t BUDDY_CABLE_RAPID_REPAIR_MS = 60000UL;
static constexpr uint32_t BUDDY_CLOCK_FAULT_RETRY_MS = 2000UL;

// Clock catch-up belongs to the post-meeting state. It must not run its
// frequent I2C probes concurrently with the timing-sensitive cable worker.
inline bool buddyCableClockRepairAllowed(bool cableActive) {
  return !cableActive;
}

inline bool buddyCableRapidClockCheck(bool markerPending, bool clockFault,
                                      bool softwareReset, uint32_t uptimeMs) {
  return uptimeMs < BUDDY_CABLE_RAPID_REPAIR_MS &&
    (markerPending || (clockFault && softwareReset));
}

// A fault that appears after the initial cable-return settling window still
// needs an automatic probe. BOOT used to clear it only because the gesture
// called buddyNow() immediately, while the idle scheduler waited 30 seconds.
inline uint32_t buddyClockCheckIntervalMs(bool clockFault, bool rapidReturnCheck) {
  return clockFault ? BUDDY_CLOCK_FAULT_RETRY_MS :
    rapidReturnCheck ? 500UL : 30000UL;
}

inline bool buddyCableDisplayGraceActive(uint32_t nowMs, uint32_t untilMs) {
  return (int32_t)(nowMs - untilMs) < 0;
}

inline bool buddyCableDeferFarewellRepaint(bool pending, bool firstCheckShown,
                                          bool cableActive) {
  return pending && !firstCheckShown && !cableActive;
}

template<typename Store> inline bool buddyCableWriteRecoveryMarker(Store &store,
                                                                    bool pending) {
  return store.putBool("rtc-check", pending) == 1 &&
    store.getBool("rtc-check", !pending) == pending;
}
