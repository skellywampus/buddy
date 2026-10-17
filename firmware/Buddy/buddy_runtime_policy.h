#pragma once
#include "buddy_battery.h"

// Battery voltage is measured at the cell, not at the regulated 3.3 V rail.
// 3.50 V leaves headroom for the board's buck regulator and for one final NVS
// write. Require three separate median-filtered readings to reject transient sag.
static constexpr float BUDDY_CRITICAL_CELL_V = 3.50f;
static constexpr uint32_t BUDDY_CRITICAL_CONFIRM_MS = 15000UL;
inline bool buddyAudioPowerAllowed(float voltage, bool usbHostPresent) {
  return usbHostPresent || (buddyBatteryVoltageValid(voltage) && voltage>BUDDY_CRITICAL_CELL_V);
}
inline bool buddyWaitForSleepRelease(bool critical, bool buttonDown, uint32_t elapsedMs) {
  return buttonDown && (!critical || elapsedMs<2000UL);
}
inline uint64_t buddySleepWakePins(bool critical, bool bootReleased, bool powerReleased,
                                  uint64_t bootPin, uint64_t powerPin) {
  return critical ? (bootReleased ? bootPin : 0) | (powerReleased ? powerPin : 0) :
    bootPin | powerPin;
}
struct BuddyLightSleepRetry {
  bool pending = false;
  uint32_t atMs = 0, failures = 0;
  bool allowed(uint32_t now) const { return !pending || (int32_t)(now-atMs)>=0; }
  void finish(bool slept, uint32_t now) {
    pending = !slept;
    if (!slept) { if(failures<UINT32_MAX)++failures; atMs=now+2000UL; }
  }
};
inline uint32_t buddyClimateInterval(bool valid, uint8_t failures) {
  return valid || failures>=10 ? 120000UL : failures>=5 ? 30000UL : 2000UL;
}
struct BuddyCriticalBattery {
  uint8_t lowReadings = 0;
  uint32_t lastReadingMs = 0;
};

static bool buddyCriticalBatteryReading(BuddyCriticalBattery &state, float voltage,
                                        bool usbHostPresent, uint32_t nowMs) {
  if (usbHostPresent) {
    state = {};
    return false;
  }
  // Missing/invalid ADC data is neither recovery nor proof of low voltage.
  if (!buddyBatteryVoltageValid(voltage)) return false;
  if (voltage > BUDDY_CRITICAL_CELL_V) { state = {}; return false; }
  if (state.lowReadings && nowMs - state.lastReadingMs < BUDDY_CRITICAL_CONFIRM_MS)
    return false;
  state.lastReadingMs = nowMs;
  if (state.lowReadings < 3) state.lowReadings++;
  return state.lowReadings >= 3;
}

// Explicit light sleep keeps Buddy's state, display and scheduled simulation
// alive. The audio rail must remain awake whenever voice listening is enabled.
static constexpr uint32_t BUDDY_LISTEN_WINDOW_MS = 60000UL;
static constexpr uint32_t BUDDY_LISTEN_GRACE_MS = 3000UL;
struct BuddyListeningWindow {
  bool open = false;
  uint32_t deadlineMs = 0;
};

static void buddyListeningTouch(BuddyListeningWindow &window, uint32_t nowMs) {
  window.open = true;
  window.deadlineMs = nowMs + BUDDY_LISTEN_WINDOW_MS;
}

static void buddyListeningClose(BuddyListeningWindow &window) { window = {}; }

static bool buddyListeningDue(const BuddyListeningWindow &window, uint32_t nowMs) {
  return window.open && (int32_t)(nowMs - window.deadlineMs) >= 0;
}
static bool buddyListeningGraceExpired(const BuddyListeningWindow &window, uint32_t nowMs) {
  return window.open && (int32_t)(nowMs - window.deadlineMs) >= (int32_t)BUDDY_LISTEN_GRACE_MS;
}

static bool buddyListeningWanted(const BuddyListeningWindow &window,
                                 bool soundEnabled) {
  // A due window stays active until the main loop has drained an in-flight
  // gesture or speech event and explicitly closes it.
  return soundEnabled && window.open;
}

static bool buddyCanLightSleep(bool listeningWindow, bool micActive, bool audioActive,
                               bool cableActive, bool usbHostPresent,
                               bool buttonDown, bool queuedButton, bool rendering) {
  return !listeningWindow && !micActive && !audioActive && !cableActive &&
    !usbHostPresent && !buttonDown && !queuedButton && !rendering;
}

static bool buddyMomentMakesMusic(BuddyMoment moment) {
  return moment == MOMENT_HUM || moment == MOMENT_SING || moment == MOMENT_MUSIC;
}
