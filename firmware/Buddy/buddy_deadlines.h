#pragma once
#include <stdint.h>

// Every scheduled interval is less than 2^31 ms. Unsigned elapsed time and
// signed absolute-deadline comparisons therefore survive millis() rollover.
static uint32_t buddyIntervalRemaining(uint32_t now, uint32_t last,
                                       uint32_t interval) {
  uint32_t elapsed = now - last;
  return elapsed >= interval ? 0 : interval - elapsed;
}

static uint32_t buddyDeadlineRemaining(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0 ? 0 : deadline - now;
}

struct BuddyNextDeadline {
  uint32_t delayMs = UINT32_MAX;

  void interval(uint32_t now, uint32_t last, uint32_t period) {
    uint32_t delay = buddyIntervalRemaining(now, last, period);
    if (delay < delayMs) delayMs = delay;
  }
  void absolute(uint32_t now, uint32_t deadline) {
    uint32_t delay = buddyDeadlineRemaining(now, deadline);
    if (delay < delayMs) delayMs = delay;
  }
};

struct BuddyWorkSchedule {
  uint32_t simulationLastMs, simulationPeriodMs;
  uint32_t climateLastMs, climatePeriodMs;
  uint32_t storageLastMs;
  bool momentActive;
  uint32_t momentAtMs;
  bool lowBatteryConfirm;
  uint32_t batteryLastMs;
  uint32_t powerLastMs = 0, powerPeriodMs = 0;
  bool micRetryPending = false;
  uint32_t micRetryLastMs = 0;
  bool menuOpen;
  uint32_t menuLastActionMs;
  uint32_t voiceMessageUntilMs;
  uint32_t voiceCommandUntilMs;
  bool listeningWindowOpen;
  uint32_t listeningWindowAtMs;
  bool cableDeadlineActive;
  uint32_t cableAtMs;
  bool clockRepairPending;
  uint32_t clockRepairLastMs;
  uint32_t clockRepairPeriodMs;
  bool cableReturnRepaintPending;
  uint32_t cableReturnRepaintAtMs;
};

static uint32_t buddyNextWorkDelay(uint32_t now, const BuddyWorkSchedule &work) {
  BuddyNextDeadline next;
  next.interval(now, work.simulationLastMs, work.simulationPeriodMs);
  next.interval(now, work.climateLastMs, work.climatePeriodMs);
  next.interval(now, work.storageLastMs, 60000UL);
  if (work.momentActive) next.absolute(now, work.momentAtMs);
  if (work.lowBatteryConfirm) next.interval(now, work.batteryLastMs, 15000UL);
  if (work.powerPeriodMs) next.interval(now, work.powerLastMs, work.powerPeriodMs);
  if (work.micRetryPending) next.interval(now, work.micRetryLastMs, 30000UL);
  if (work.menuOpen) next.interval(now, work.menuLastActionMs, 45000UL);
  if (work.voiceMessageUntilMs) next.absolute(now, work.voiceMessageUntilMs);
  if (work.voiceCommandUntilMs) next.absolute(now, work.voiceCommandUntilMs);
  if (work.listeningWindowOpen) next.absolute(now, work.listeningWindowAtMs);
  if (work.cableDeadlineActive) next.absolute(now, work.cableAtMs);
  if (work.clockRepairPending)
    next.interval(now, work.clockRepairLastMs,
                  work.clockRepairPeriodMs ? work.clockRepairPeriodMs : 30000UL);
  if (work.cableReturnRepaintPending)
    next.absolute(now, work.cableReturnRepaintAtMs);
  return next.delayMs;
}

// Show the home screen at the first post-cable deadline even if the RTC is
// still settling. Afterward, redraw only when a trustworthy clock returns.
static bool buddyCableReturnShouldRender(bool firstCheck, bool clockHealthy) {
  return firstCheck || clockHealthy;
}

// This is a display-decision horizon, not a simulated-life step. The existing
// hourly simulation remains the authority for needs, memories and development.
// A supplied random word makes the duration policy deterministic in host tests.
static uint32_t buddyMomentDurationMs(BuddyMoment moment, BuddyStage stage,
                                      uint16_t energy, uint16_t comfort,
                                      const BuddyTraits &traits,
                                      uint16_t affection, uint8_t routineStrength,
                                      uint32_t randomWord) {
  uint32_t low = 5, high = 16; // minutes: ordinary observation and activity
  switch (moment) {
    case MOMENT_STRETCH: case MOMENT_LISTEN: case MOMENT_GROOM:
    case MOMENT_SMELL_FLOWERS: case MOMENT_CELEBRATE:
      low = 2; high = 7; break;
    case MOMENT_NAP: case MOMENT_NOOK: case MOMENT_DAYDREAM:
    case MOMENT_MEDITATE: case MOMENT_STARGAZE:
      low = 14; high = 34; break;
    case MOMENT_BUILD: case MOMENT_GARDEN: case MOMENT_SCULPT:
    case MOMENT_FORT: case MOMENT_STUDY: case MOMENT_STORY:
    case MOMENT_JOURNAL: case MOMENT_MUSIC: case MOMENT_NATURE:
      low = 9; high = 25; break;
    case MOMENT_WARM: case MOMENT_COOL:
      low = 3; high = 9; break;
    default: break;
  }
  uint32_t minutes = low + randomWord % (high - low + 1);
  bool restful = moment == MOMENT_NAP || moment == MOMENT_NOOK ||
    moment == MOMENT_DAYDREAM || moment == MOMENT_MEDITATE ||
    moment == MOMENT_STARGAZE;
  if (restful) {
    if (energy < 400) minutes += 6;
    if (stage == STAGE_BABY || stage == STAGE_ELDER) minutes += 3;
    if (moment == MOMENT_NOOK && comfort < 350) minutes += 2;
  } else {
    if (energy < 350) minutes = minutes > 3 ? minutes - 3 : 2;
    if (stage == STAGE_ELDER && minutes > 4) minutes -= 2;
    if ((moment == MOMENT_WARM || moment == MOMENT_COOL) && comfort < 300)
      minutes = minutes > 2 ? minutes - 2 : 2;
  }
  // Existing attachment, routine and temperament can sustain chosen work.
  if (affection >= 700) minutes += 2;
  if (routineStrength >= 3) minutes += 2;
  if ((moment == MOMENT_STUDY || moment == MOMENT_NATURE) && traits.curiosity >= 75)
    minutes += 2;
  if (moment == MOMENT_MUSIC && traits.playfulness >= 75) minutes += 2;
  if (minutes < 2) minutes = 2;
  if (minutes > 45) minutes = 45;
  return minutes * 60000UL;
}
