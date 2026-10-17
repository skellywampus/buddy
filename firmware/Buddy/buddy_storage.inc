struct BuddySave {
  uint32_t signature, sequence, checksum;
  BuddyState state;
};
struct BuddySaveV2 {
  uint32_t signature, sequence, checksum;
  BuddyStateV2 state;
};
static uint32_t saveSequence = 0;
static int8_t activeSaveSlot = -1;
static int8_t migrationProtectedLifeSlot = -1;

// A restoration build installs this record before the first RTC validation
// or simulation step. Ordinary firmware only reads it; save layouts do not
// change. Invalid or unwriteable reconciliation fails closed.
static bool loadBuddyLifeClockOffset() {
  buddyLifeClockOffsetSeconds = 0;
  BuddyLifeClockOffset record = {};
  size_t length = preferences.getBytesLength("lifeclock");
  if (length) {
    if (length != sizeof(record) ||
        preferences.getBytes("lifeclock", &record, sizeof(record)) != sizeof(record) ||
        !buddyValidLifeClockOffset(record)) {
      storageLocked = true;
      return false;
    }
#ifdef BUDDY_RESTORE_OFFSET_SECONDS
    if (record.seconds != BUDDY_RESTORE_OFFSET_SECONDS) {
      storageLocked = true;
      return false;
    }
#endif
    buddyLifeClockOffsetSeconds = record.seconds;
    return true;
  }
#ifdef BUDDY_RESTORE_OFFSET_SECONDS
  static_assert(BUDDY_RESTORE_OFFSET_SECONDS > 0,
                "Restoration requires a positive physical-time pause");
  record = buddyMakeLifeClockOffset(BUDDY_RESTORE_OFFSET_SECONDS);
  BuddyLifeClockOffset verify = {};
  if (preferences.putBytes("lifeclock", &record, sizeof(record)) != sizeof(record) ||
      preferences.getBytes("lifeclock", &verify, sizeof(verify)) != sizeof(verify) ||
      !buddyValidLifeClockOffset(verify) || verify.seconds != record.seconds) {
    storageLocked = true;
    return false;
  }
  buddyLifeClockOffsetSeconds = record.seconds;
#endif
  return true;
}

template<typename Save> static uint32_t saveChecksum(const Save &save) {
  uint32_t hash = 2166136261UL;
  const uint8_t *bytes = (const uint8_t *)&save.state;
  for (size_t i = 0; i < sizeof(save.state); i++) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash ^ save.sequence;
}
static bool validLife(const BuddyStateV1 &s) {
  return s.magic == BUDDY_MAGIC && s.generation && s.birthEpoch &&
    s.lastUpdateEpoch >= s.birthEpoch && s.currentBehavior < BEHAVIOR_COUNT &&
    s.currentStage <= STAGE_GONE && s.memoryHead < 16 && s.memoryCount <= 16;
}
static bool validSave(const BuddySave &s) {
  return s.signature == 0x42535633 && s.checksum == saveChecksum(s) &&
    s.state.version == BUDDY_STATE_VERSION && validLife(s.state) &&
    s.state.worldVersion == 2 && s.state.eventCount <= 24 && s.state.eventHead < 24 &&
    s.state.favoriteObject <= OBJECT_COUNT && s.state.inheritedObject <= OBJECT_COUNT &&
    s.state.familyEar < 6 && s.state.familyMarking < 6;
}
static bool validSaveV2(const BuddySaveV2 &s) {
  return s.signature == 0x42535632 && s.checksum == saveChecksum(s) &&
    s.state.version == 2 && validLife(s.state) && s.state.worldVersion == 1 &&
    s.state.eventCount <= 24 && s.state.eventHead < 24 &&
    s.state.favoriteObject <= OLD_OBJECT_COUNT &&
    s.state.inheritedObject <= OLD_OBJECT_COUNT &&
    s.state.familyEar < 6 && s.state.familyMarking < 6;
}
static bool readSave(Preferences &store, const char *key, BuddySave &save) {
  return store.getBytesLength(key) == sizeof(save) &&
    store.getBytes(key, &save, sizeof(save)) == sizeof(save) && validSave(save);
}
static void migrateBuddyV2(const BuddyStateV2 &old, BuddyState &next) {
  next = {};
  static_cast<BuddyStateV1 &>(next) = static_cast<const BuddyStateV1 &>(old);
  next.version = BUDDY_STATE_VERSION;
  next.worldVersion = 2;
  next.stayAwake = old.stayAwake;
  next.possessions = old.possessions;
  next.favoriteObject = old.favoriteObject == OLD_OBJECT_COUNT ? OBJECT_COUNT : old.favoriteObject;
  for (uint8_t i = 0; i < OLD_OBJECT_COUNT; i++) {
    next.affection[i] = old.affection[i];
    next.visits[i] = old.visits[i];
  }
  next.plantGrowth = old.plantGrowth;
  next.inheritedObject = old.inheritedObject == OLD_OBJECT_COUNT ? OBJECT_COUNT : old.inheritedObject;
  next.familyEar = old.familyEar;
  next.familyMarking = old.familyMarking;
  next.eventHead = old.eventHead;
  next.eventCount = old.eventCount;
  memcpy(next.events, old.events, sizeof(old.events));
  for (WorldMemory &event : next.events)
    if (event.object == OLD_OBJECT_COUNT) event.object = OBJECT_COUNT;
  next.birthTraits = old.birthTraits;
  memcpy(next.development, old.development, sizeof(old.development));
  next.lastDevelopmentDay = old.lastDevelopmentDay;
  next.lastWorldEpoch = old.lastWorldEpoch;
  next.lastAttentionEpoch = old.lastAttentionEpoch;
}
static bool readAnyLife(Preferences &store, const char *key, BuddyState &state,
                        uint32_t &sequence, bool &fromV2) {
  BuddySave current = {};
  if (readSave(store, key, current)) {
    state = current.state; sequence = current.sequence; fromV2 = false; return true;
  }
  BuddySaveV2 old = {};
  if (store.getBytesLength(key) != sizeof(old) ||
      store.getBytes(key, &old, sizeof(old)) != sizeof(old) || !validSaveV2(old)) return false;
  migrateBuddyV2(old.state, state);
  sequence = old.sequence; fromV2 = true;
  return true;
}
static BuddySave packedLife() {
  BuddySave save = {};
  save.signature = 0x42535633;
  save.sequence = saveSequence + 1;
  save.state = buddy;
  save.checksum = saveChecksum(save);
  return save;
}
void saveBuddy() {
  if (buddyCompletionPreparing) return;
  if (buddyCompletionPending && !buddyCompletionApplying) { saveFailed = true; return; }
  if (storageLocked || buddy.magic != BUDDY_MAGIC) return;
  BuddySave save = packedLife(), check = {};
  int8_t next = activeSaveSlot == 0 ? 1 : 0;
  if (migrationProtectedLifeSlot >= 0) next = 1 - migrationProtectedLifeSlot;
  const char *key = next == 0 ? "life-a" : "life-b";
  saveFailed = preferences.putBytes(key, &save, sizeof(save)) != sizeof(save) ||
    !readSave(preferences, key, check) || check.sequence != save.sequence;
  if (!saveFailed) { activeSaveSlot = next; saveSequence = save.sequence; }
}

// Eight pending generations remain in flash until their SD exports verify.
// Never overwrite a full queue: wait at the memorial until a card is inserted.
bool queueMemorial() {
  if (buddy.alive || storageLocked) return false;
  uint64_t identity = ((uint64_t)buddy.birthEpoch << 32) | buddy.seed;
  if (preferences.getULong64("exported", 0) == identity) return true;
  Preferences archive;
  if (!archive.begin("buddy-archive", false)) return false;
  int freeSlot = -1;
  for (uint8_t i = 0; i < 8; i++) {
    char key[8]; snprintf(key, sizeof(key), "life%u", i);
    BuddyState old = {}; uint32_t sequence = 0; bool fromV2 = false;
    if (readAnyLife(archive, key, old, sequence, fromV2) &&
        old.seed == buddy.seed && old.birthEpoch == buddy.birthEpoch) {
      archive.end(); return true;
    }
    if (!archive.getBytesLength(key) && freeSlot < 0) freeSlot = i;
  }
  bool ok = false;
  if (freeSlot >= 0) {
    char key[8]; snprintf(key, sizeof(key), "life%u", freeSlot);
    BuddySave save = packedLife(), check = {};
    ok = archive.putBytes(key, &save, sizeof(save)) == sizeof(save) && readSave(archive, key, check);
  }
  archive.end();
  archiveBlocked = !ok;
  return ok;
}
void setupBuddyStorage() {
  if (sdSuspended) return;
  SD_MMC.setPins(39, 41, 40);
  sdAvailable = SD_MMC.begin("/sdcard", true);
  if (sdAvailable && !SD_MMC.exists("/buddy")) sdAvailable = SD_MMC.mkdir("/buddy");
}
// Called only after all SD files (including the daily journal) are closed.
// The sleeping image is drawn after this returns, making it a trustworthy
// visual cue that firmware will not touch the card until a deliberate wake.
void suspendBuddyStorageForSleep() {
  sdSuspended = true;
  SD_MMC.end();
  sdAvailable = false;
}
void resumeBuddyStorageAfterWake() {
  if (!sdSuspended) return;
  sdSuspended = false;
  setupBuddyStorage();
}
static void lifePath(char *path, size_t size, const char *suffix) {
  snprintf(path, size, "/buddy/life_%04u_%08lx_%s", buddy.generation, (unsigned long)buddy.seed, suffix);
}

// Keep the previous complete file as .bak until the replacement commits.
static bool writeVerified(const char *path, const String &data) {
  String temp = String(path) + ".tmp", backup = String(path) + ".bak";
  if (!SD_MMC.exists(path) && SD_MMC.exists(backup)) SD_MMC.rename(backup, path);
  File file = SD_MMC.open(temp, FILE_WRITE);
  if (!file) return false;
  size_t written = file.print(data);
  file.flush(); file.close();
  if (written != data.length()) return false;
  file = SD_MMC.open(temp, FILE_READ);
  bool ok = file && file.size() == data.length();
  for (size_t i = 0; ok && i < data.length(); i++) if (file.read() != (uint8_t)data[i]) ok = false;
  file.close();
  if (!ok) return false;
  if (SD_MMC.exists(path)) {
    if (SD_MMC.exists(backup) && !SD_MMC.remove(backup)) return false;
    if (!SD_MMC.rename(path, backup)) return false;
  }
  if (!SD_MMC.rename(temp, path)) return false;
  if (SD_MMC.exists(backup)) SD_MMC.remove(backup);
  return true;
}
static String traitsJson(const BuddyTraits &traits) {
  char text[180];
  snprintf(text, sizeof(text), "{\"curiosity\":%u,\"sociability\":%u,\"playfulness\":%u,\"resilience\":%u,\"sensitivity\":%u,\"independence\":%u}",
    traits.curiosity, traits.sociability, traits.playfulness, traits.resilience, traits.sensitivity, traits.independence);
  return String(text);
}
void writeBirthRecord() {
  if (!sdAvailable || buddy.magic != BUDDY_MAGIC) return;
  char path[96]; lifePath(path, sizeof(path), "identity.json");
  if (SD_MMC.exists(path)) return;
  String text = "{\"generation\":" + String(buddy.generation) + ",\"birth_epoch\":" + String(buddy.birthEpoch) +
    ",\"epoch_origin\":\"2000-01-01 local time\",\"seed\":" + String(buddy.seed) +
    ",\"sex\":\"" + String(buddySexFromSeed(buddy.seed)==BUDDY_GIRL ? "girl" : "boy") + "\"" +
    ",\"family_ear\":" + String(buddy.familyEar) + ",\"family_marking\":" + String(buddy.familyMarking) +
    ",\"birth_traits\":" + traitsJson(buddy.birthTraits) + "}\n";
  if (!writeVerified(path, text)) sdAvailable = false;
}
void appendDailyJournal(uint32_t epoch) {
  if (!sdAvailable || !buddy.alive || clockFault || buddy.magic != BUDDY_MAGIC) return;
  uint32_t day = buddyAgeDays(epoch);
  if (buddy.lastLoggedDay == day) return;
  char path[96]; lifePath(path, sizeof(path), "daily.csv");
  String backup = String(path) + ".bak";
  if (!SD_MMC.exists(path) && SD_MMC.exists(backup)) SD_MMC.rename(backup, path);
  String text;
  if (SD_MMC.exists(path)) {
    File file = SD_MMC.open(path, FILE_READ);
    if (!file) { sdAvailable = false; return; }
    size_t expected = file.size();
    // A normal <=425-day journal is well below this limit. Do not allow a
    // corrupt/replaced card file to consume the entire speech/runtime heap.
    if (expected > 128UL * 1024UL || !text.reserve(expected + 256)) {
      file.close(); sdAvailable = false; return;
    }
    for (size_t i = 0; i < expected; ++i) {
      int byte = file.read();
      if (byte < 0) break;
      text += (char)byte;
    }
    file.close();
    if (text.length() != expected) { sdAvailable = false; return; }
  } else text = "epoch_since_2000,age_days,stage,mood,energy,health,bond,comfort,temp_f,humidity,behavior,favorite\n";
  String dayToken = "," + String(day) + "," + stageName((BuddyStage)buddy.currentStage) + ",";
  int lastStart = text.lastIndexOf('\n', text.length() - 2) + 1;
  if (text.substring(lastStart).indexOf(dayToken) < 0) {
    char row[220], climate[40];
    if (latestClimateValid) snprintf(climate, sizeof(climate), "%.1f,%.1f", latestTempC*1.8f+32, latestHumidity);
    else snprintf(climate, sizeof(climate), ",");
    snprintf(row, sizeof(row), "%lu,%lu,%s,%u,%u,%u,%u,%u,%s,%s,%s\n", (unsigned long)epoch,
      (unsigned long)day, stageName((BuddyStage)buddy.currentStage), buddy.mood, buddy.energy, buddy.health,
      buddy.bond, buddy.comfort, climate, behaviorName((BuddyBehavior)buddy.currentBehavior), objectName(buddy.favoriteObject));
    text += row;
    if (!writeVerified(path, text)) { sdAvailable = false; return; }
  }
  buddy.lastLoggedDay = day;
  saveBuddy();
}
static bool exportMemorial() {
  char path[96]; lifePath(path, sizeof(path), "memorial.json");
  String text = "{\"generation\":" + String(buddy.generation) + ",\"seed\":" + String(buddy.seed) +
    ",\"sex\":\"" + String(buddySexFromSeed(buddy.seed)==BUDDY_GIRL ? "girl" : "boy") + "\"" +
    ",\"epoch_origin\":\"2000-01-01 local time\",\"birth_epoch\":" + String(buddy.birthEpoch) +
    ",\"death_epoch\":" + String(buddy.deathEpoch) + ",\"lived_days\":" + String(buddy.prior.livedDays) +
    ",\"final_bond\":" + String(buddy.bond) + ",\"interactions\":" + String(buddy.totalInteractions) +
    ",\"favorite_possession\":\"" + objectName(buddy.favoriteObject) + "\",\"favorite_behavior\":\"" +
    behaviorName((BuddyBehavior)buddy.prior.dominantBehavior) + "\",\"traits\":" + traitsJson(buddy.traits) +
    ",\"birth_traits\":" + traitsJson(buddy.birthTraits) + ",\"memories\":[";
  for (uint8_t i = 0; i < buddy.eventCount; i++) {
    const WorldMemory &event = buddy.events[(buddy.eventHead + 24 - buddy.eventCount + i) % 24];
    char description[64]; describeWorldEvent(event, description, sizeof(description));
    if (i) text += ",";
    text += "{\"epoch\":" + String(event.epoch) + ",\"event\":\"" + description + "\"}";
  }
  text += "],\"milestones\":[";
  for (uint8_t i = 0; i < buddy.memoryCount; i++) {
    const BuddyMemory &memory = buddy.memories[(buddy.memoryHead + 16 - buddy.memoryCount + i) % 16];
    if (i) text += ",";
    text += "{\"epoch\":" + String(memory.epoch) + ",\"type\":" + String(memory.type) + "}";
  }
  text += "]}\n";
  if (!writeVerified(path, text)) return false;
  lifePath(path, sizeof(path), "portrait.pbm");
  return exportPortrait(path);
}
void retryBuddyArchives() {
  if (!sdAvailable || storageLocked) return;
  Preferences archive;
  if (!archive.begin("buddy-archive", false)) return;
  for (uint8_t i = 0; i < 8; i++) {
    char key[8]; snprintf(key, sizeof(key), "life%u", i);
    BuddyState saved = {}; uint32_t sequence = 0; bool fromV2 = false;
    if (!readAnyLife(archive, key, saved, sequence, fromV2)) continue;
    BuddyState active = buddy;
    buddy = saved;
    bool ok = exportMemorial();
    buddy = active;
    needsRender = true;
    if (ok) {
      uint64_t identity = ((uint64_t)saved.birthEpoch << 32) | saved.seed;
      // Only the active deceased life needs a marker against repeated queueing.
      if (saved.seed != buddy.seed || saved.birthEpoch != buddy.birthEpoch ||
          preferences.putULong64("exported", identity) == sizeof(identity)) archive.remove(key);
    }
    else { sdAvailable = false; break; }
  }
  archive.end();
}
void archiveLife() { queueMemorial(); }
void serviceBuddyStorage(uint32_t now) {
  if (buddyCompletionPending && !recoverBuddyCompletion()) {
    // Verified old archives may be exported to free NVS space for a pending
    // commit. Do not make a full flash impossible to recover by inserting SD.
    if (!sdSuspended && !storageLocked) {
      if (!sdAvailable) setupBuddyStorage();
      retryBuddyArchives();
      recoverBuddyCompletion();
    }
    if (buddyCompletionPending) return;
  }
  if (sdSuspended || storageLocked || buddy.magic != BUDDY_MAGIC) return;
  if (!sdAvailable) { SD_MMC.end(); setupBuddyStorage(); }
  writeBirthRecord();
  if (!buddy.alive) queueMemorial();
  retryBuddyArchives();
  appendDailyJournal(now);
  if (saveFailed) saveBuddy();
  if (!saveFailed && !ambitionLocked && now && !clockFault &&
      (buddyAmbition.magic != BUDDY_AMBITION_MAGIC ||
       buddyAmbition.lifeSeed != buddy.seed || buddyAmbition.birthEpoch != buddy.birthEpoch ||
       buddyAmbition.generation != buddy.generation)) setupBuddyAmbition(now);
  if (!legacyLocked && buddyLegacy.magic != BUDDY_LEGACY_MAGIC && now && !clockFault)
    setupBuddyLegacy(now);
  if (ambitionSaveFailed) saveBuddyAmbition();
  if (legacySaveFailed) saveBuddyLegacy();
  if (houseSaveFailed) saveBuddyHouse();
}
void loadOrCreateBuddy(uint32_t now) {
  if (!preferences.begin("buddy-life", false)) { storageLocked = true; return; }
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  if (!buddyTestStoragePreflight(preferences)) { storageLocked = true; return; }
#endif
  BuddyState a = {}, b = {};
  uint32_t seqA = 0, seqB = 0;
  bool oldA = false, oldB = false;
  bool hasA = readAnyLife(preferences, "life-a", a, seqA, oldA);
  bool hasB = readAnyLife(preferences, "life-b", b, seqB, oldB);
  migrationProtectedLifeSlot = hasA && oldA ? 0 : hasB && oldB ? 1 : -1;
  lifeRollbackDetected = (hasA && !hasB && preferences.getBytesLength("life-b")) ||
    (hasB && !hasA && preferences.getBytesLength("life-a"));
  if (hasA || hasB) {
    bool useB = hasB && (!hasA || (int32_t)(seqB-seqA) > 0);
    if (useB ? oldB : oldA) migrationProtectedLifeSlot = useB ? 1 : 0;
    buddy = useB ? b : a;
    size_t commitSize = preferences.getBytesLength("aim-commit");
    if (commitSize) {
      BuddyCompletionCommit record = {};
      BuddyHouseState house = {}; bool hasHouse = false;
      if (!readBuddyCompletion(record, house, hasHouse) ||
          record.life.seed != buddy.seed || record.life.birthEpoch != buddy.birthEpoch) {
        storageLocked = true;
        return;
      }
      buddy = record.life;
      buddyCompletionPending = true;
    }
    saveSequence = useB ? seqB : seqA;
    activeSaveSlot = useB ? 1 : 0;
    if (useB ? oldB : oldA) {
      saveBuddy();
      if (!saveFailed) saveBuddy();
    }
    return;
  }
  if (preferences.getBytesLength("life-a") || preferences.getBytesLength("life-b")) {
    storageLocked = true; return;
  }
  size_t legacySize = preferences.getBytesLength("state");
  if (legacySize) {
    BuddyStateV1 legacy = {};
    if (legacySize != sizeof(legacy) || preferences.getBytes("state", &legacy, sizeof(legacy)) != sizeof(legacy) ||
        legacy.version != 1 || !validLife(legacy)) { storageLocked = true; return; }
    memset(&buddy, 0, sizeof(buddy));
    static_cast<BuddyStateV1 &>(buddy) = legacy;
    buddy.version = BUDDY_STATE_VERSION;
    initializeBuddyWorld(false);
    buddy.lastLoggedDay = UINT32_MAX;
    saveBuddy();
    if (!saveFailed) saveBuddy();
    return;
  }
  // A board with no life and no previous home enters mandatory first-run
  // creation. Saved lives and legacy migrations above never see this path.
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  // This disposable test is unattended. A fresh blank NVS begins its own
  // generation-one life after the physical RTC is validated in setup().
  return;
#endif
  if (!preferences.getBytesLength("intro-a") && !preferences.getBytesLength("intro-b") &&
      (preferences.getBytesLength("home-a") || preferences.getBytesLength("home-b"))) {
    if (now && !clockFault) createBuddyLife(now, false);
    return;
  }
  setupBuddyIntro();
  if (buddyIntro.step == INTRO_COMPLETE) birthBuddyFromIntro(now);
}
