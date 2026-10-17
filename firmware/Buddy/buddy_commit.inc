void releaseBuddyMigrationProtection() {
  migrationProtectedLifeSlot = migrationProtectedAmbitionSlot = migrationProtectedLegacySlot = -1;
}

static uint32_t buddyStepCommitHash(BuddyCompletionStepCommit record) {
  record.base.checksum = 0;
  uint32_t hash = 2166136261UL;
  const uint8_t *bytes = (const uint8_t *)&record;
  for (size_t i = 0; i < sizeof(record); ++i) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}

bool readBuddyCompletion(BuddyCompletionCommit &record, BuddyHouseState &house, bool &hasHouse) {
  hasHouse = false;
  size_t size = preferences.getBytesLength("aim-commit");
  if (size == sizeof(BuddyCompletionStepCommit)) {
    BuddyCompletionStepCommit step = {};
    if (preferences.getBytes("aim-commit", &step, sizeof(step)) != sizeof(step) ||
        step.base.magic != 0x42434D32UL || step.base.checksum != buddyStepCommitHash(step) ||
        !validHouse(step.house)) return false;
    record = step.base; house = step.house; hasHouse = true;
  } else if (size != sizeof(record) ||
      preferences.getBytes("aim-commit", &record, sizeof(record)) != sizeof(record) ||
      record.magic != 0x42434D31UL || record.checksum != buddyCompletionHash(record)) return false;
  return validLife(record.life) && record.life.version == BUDDY_STATE_VERSION &&
    validAmbition(record.ambition) && validLegacy(record.legacy);
}

bool persistBuddyCompletion() {
  if (storageLocked || buddyCompletionPending) return false;
  BuddyCompletionCommit record = {};
  record.magic = 0x42434D31UL;
  record.life = buddy;
  record.ambition = buddyAmbition;
  record.ambition.checksum = ambitionHash(record.ambition);
  record.legacy = buddyLegacy;
  record.legacy.checksum = legacyHash(record.legacy);
  if (!validAmbition(record.ambition) || !validLegacy(record.legacy)) return false;
  record.checksum = buddyCompletionHash(record);
  bool ok;
  if (buddySimulationStepActive && buddyHouse.magic == BUDDY_HOUSE_MAGIC) {
    BuddyCompletionStepCommit step = {}, check = {};
    step.base = record; step.base.magic = 0x42434D32UL;
    step.house = buddyHouse; step.house.checksum = houseHash(step.house);
    step.base.checksum = buddyStepCommitHash(step);
    ok = validHouse(step.house) &&
      preferences.putBytes("aim-commit", &step, sizeof(step)) == sizeof(step) &&
      preferences.getBytes("aim-commit", &check, sizeof(check)) == sizeof(check) &&
      !memcmp(&step, &check, sizeof(step));
  } else {
    BuddyCompletionCommit check = {};
    ok = preferences.putBytes("aim-commit", &record, sizeof(record)) == sizeof(record) &&
      preferences.getBytes("aim-commit", &check, sizeof(check)) == sizeof(check) &&
      !memcmp(&record, &check, sizeof(record));
  }
  // A written but unreadable commit must not be superseded by later progress.
  buddyCompletionPending = preferences.getBytesLength("aim-commit") != 0;
  return ok;
}

bool recoverBuddyCompletion() {
  if (!buddyCompletionPending) return true;
  if (storageLocked) return false;
  BuddyCompletionCommit record = {};
  BuddyHouseState house = {}; bool hasHouse = false;
  if (!readBuddyCompletion(record, house, hasHouse) ||
      record.life.seed != buddy.seed || record.life.birthEpoch != buddy.birthEpoch) {
    storageLocked = true;
    return false;
  }
  buddy = record.life;
  // The independently persisted setting is newer than a pending life image.
  uint8_t sound = preferences.getUChar("sound-mode", 255);
  if (buddySoundModeValid(sound)) buddy.soundEnabled = sound != SOUND_OFF;
  buddyAmbition = record.ambition;
  buddyLegacy = record.legacy;
  if (hasHouse) buddyHouse = house;
  buddyCompletionApplying = true;
  saveBuddy();
  saveBuddyAmbition();
  saveBuddyLegacy();
  if (hasHouse) saveBuddyHouse();
  buddyCompletionApplying = false;
  if (saveFailed || ambitionSaveFailed || legacySaveFailed || ambitionLocked || legacyLocked ||
      (hasHouse && (houseSaveFailed || houseLocked)))
    return false;
  if (!preferences.remove("aim-commit")) return false;
  buddyCompletionPending = false;
  return true;
}
