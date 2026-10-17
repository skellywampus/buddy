#pragma once

// Separate from BuddyState: a firmware update must never rewrite a life just
// to remember that it entered deep sleep. RESET/PWR can reboot the chip rather
// than report an EXT1 wake, so the ESP wake cause alone is not sufficient.
static bool armBuddyWakeSound(Preferences &store) {
  return store.putUChar("wake-arm", 1) == 1 && store.getUChar("wake-arm", 0) == 1;
}

static bool takeBuddyWakeSound(Preferences &store) {
  if (store.getUChar("wake-arm", 0) != 1) return false;
  store.putUChar("wake-arm", 0);
  return true;
}

static bool buddyWakeSoundDue(bool timerWake, bool armed, bool ext1Wake,
                              bool hourlyWakeChime) {
  return timerWake ? hourlyWakeChime : armed || ext1Wake;
}

static bool buddySleepSoundDue(bool manual, bool hourlyDozeWake) {
  return manual || !hourlyDozeWake;
}

// An hourly housekeeping wake must not announce a newly reached milestone or
// play an autonomous activity sound while nobody deliberately woke Buddy.
static bool buddyUnpromptedSoundAllowed(bool hourlyDozeWake) {
  return !hourlyDozeWake;
}
