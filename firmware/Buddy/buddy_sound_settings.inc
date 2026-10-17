bool setBuddySoundMode(BuddySoundMode mode) {
  if (mode == buddySoundMode) {
    buddy.soundEnabled = mode != SOUND_OFF;
    if (mode == SOUND_OFF) { buddyListeningClose(buddyListeningWindow); stopBuddyMic(); }
    else touchBuddyListeningWindow();
    return true;
  }
  if (!buddySoundModeValid(mode) || storageLocked) return false;
  // Apply only after a verified independent NVS write. A failed write leaves
  // both the audible setting and saved-life boolean unchanged.
  buddySoundSaveFailed = !buddyPersistSoundMode(preferences, mode);
  if (buddySoundSaveFailed) return false;
  buddySoundMode = mode;
  buddy.soundEnabled = mode != SOUND_OFF;
  if (mode == SOUND_OFF) {
    buddyListeningClose(buddyListeningWindow);
    stopBuddyMic();
  } else touchBuddyListeningWindow(); // explicit USB/Settings action
  saveBuddy();
  // The main loop starts input after the new Settings/home frame is drawn.
  return true;
}

void loadBuddySoundMode() {
  buddySoundMode = buddyLoadSoundMode(preferences, buddy.soundEnabled,
                                      buddySoundSaveFailed);
  buddy.soundEnabled = buddySoundMode != SOUND_OFF;
}

bool setBuddySoundEnabled(bool enabled) {
  return setBuddySoundMode(enabled ?
    (buddySoundMode == SOUND_HIGH ? SOUND_HIGH : SOUND_LOW) : SOUND_OFF);
}
