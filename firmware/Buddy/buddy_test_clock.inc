#ifdef BUDDY_ACCELERATED_LIFE_TEST

static uint32_t buddyClockFloor();
static BuddyAcceleratedTime testLifeTime = {};
static uint32_t testClockSequence = 0;
static int8_t testClockSlot = -1;
static bool testClockReady = false;

static bool buddyTestStoragePreflight(Preferences &lifeStore) {
  uint32_t marker = 0;
  size_t length = lifeStore.getBytesLength("test-mode");
  if (length) {
    return length == sizeof(marker) &&
      lifeStore.getBytes("test-mode", &marker, sizeof(marker)) == sizeof(marker) &&
      marker == BUDDY_TEST_STORAGE_MAGIC;
  }
  // Never adopt an existing production life or even an unfinished intro.
  if (lifeStore.getBytesLength("life-a") || lifeStore.getBytesLength("life-b") ||
      lifeStore.getBytesLength("state") || lifeStore.getBytesLength("intro-a") ||
      lifeStore.getBytesLength("intro-b") || lifeStore.getBytesLength("home-a") ||
      lifeStore.getBytesLength("home-b")) return false;
  marker = BUDDY_TEST_STORAGE_MAGIC;
  uint32_t verify = 0;
  return lifeStore.putBytes("test-mode", &marker, sizeof(marker)) == sizeof(marker) &&
    lifeStore.getBytes("test-mode", &verify, sizeof(verify)) == sizeof(verify) &&
    verify == marker;
}

static bool buddyTestClockSave(const BuddyAcceleratedTime &time) {
  if (!buddyAcceleratedTimeValid(time)) return false;
  Preferences store;
  if (!store.begin("buddy-test", false)) return false;
  int8_t next = testClockSlot == 0 ? 1 : 0;
  const char *key = next ? "clock-b" : "clock-a";
  BuddyTestClockRecord record = buddyMakeTestClockRecord(testClockSequence + 1, time);
  BuddyTestClockRecord check = {};
  bool ok = store.putBytes(key, &record, sizeof(record)) == sizeof(record) &&
    store.getBytesLength(key) == sizeof(check) &&
    store.getBytes(key, &check, sizeof(check)) == sizeof(check) &&
    buddyTestClockValid(check) && check.sequence == record.sequence &&
    check.time.seed == time.seed && check.time.generation == time.generation;
  store.end();
  if (ok) {
    testLifeTime = time;
    testClockSequence = record.sequence;
    testClockSlot = next;
    testClockReady = true;
    Serial.printf("LIFE_TEST gen=%u seed=%08lx lifespan=%u real_birth=%lu sim_birth=%lu\n",
      time.generation, (unsigned long)time.seed, time.lifespanDays,
      (unsigned long)time.realBirth, (unsigned long)time.simulatedBirth);
  }
  return ok;
}

static bool buddyTestClockLoad() {
  testLifeTime = {};
  testClockSequence = 0;
  testClockSlot = -1;
  testClockReady = false;
  Preferences store;
  if (!store.begin("buddy-test", false)) return false;
  BuddyTestClockRecord records[2] = {};
  bool valid[2] = {};
  for (int i = 0; i < 2; ++i) {
    const char *key = i ? "clock-b" : "clock-a";
    valid[i] = store.getBytesLength(key) == sizeof(records[i]) &&
      store.getBytes(key, &records[i], sizeof(records[i])) == sizeof(records[i]) &&
      buddyTestClockValid(records[i]);
  }
  store.end();
  int newest = -1, matching = -1;
  for (int i = 0; i < 2; ++i) if (valid[i]) {
    if (newest < 0 || (int32_t)(records[i].sequence - records[newest].sequence) > 0)
      newest = i;
    if (buddy.magic == BUDDY_MAGIC &&
        records[i].time.seed == buddy.seed &&
        records[i].time.generation == buddy.generation &&
        records[i].time.simulatedBirth == buddy.birthEpoch &&
        (matching < 0 || (int32_t)(records[i].sequence - records[matching].sequence) > 0))
      matching = i;
  }
  if (buddy.magic != BUDDY_MAGIC) {
    testClockReady = newest < 0;
    return testClockReady;
  }
  if (matching >= 0) {
    testLifeTime = records[matching].time;
    testClockSequence = records[matching].sequence;
    testClockSlot = matching;
    testClockReady = true;
    return true;
  }
  if (newest >= 0 && buddy.generation == records[newest].time.generation + 1) {
    testLifeTime = records[newest].time;
    testClockSequence = records[newest].sequence;
    testClockSlot = newest;
    BuddyAcceleratedTime next = {};
    return buddyAcceleratedRebase(testLifeTime, buddy.birthEpoch, buddy.seed,
      buddy.generation, buddyLifespanDays(buddy.seed), next) &&
      buddyTestClockSave(next);
  }
  // The first life can be saved just before its initial clock record. Its
  // birth was at the trusted physical RTC epoch, so reconstruct exactly.
  if (newest < 0 && buddy.generation == 1 && buddy.birthEpoch) {
    BuddyAcceleratedTime first = {buddy.birthEpoch, buddy.birthEpoch,
      buddy.seed, buddy.generation, buddyLifespanDays(buddy.seed)};
    return buddyTestClockSave(first);
  }
  return false;
}

static bool buddyTestClockBindLife() {
  BuddyAcceleratedTime next = {};
  if (buddy.magic != BUDDY_MAGIC) return false;
  if (!buddyAcceleratedTimeValid(testLifeTime)) {
    if (buddy.generation != 1) return false;
    next = {buddy.birthEpoch, buddy.birthEpoch, buddy.seed,
            buddy.generation, buddyLifespanDays(buddy.seed)};
  } else if (!buddyAcceleratedRebase(testLifeTime, buddy.birthEpoch, buddy.seed,
      buddy.generation, buddyLifespanDays(buddy.seed), next)) return false;
  return buddyTestClockSave(next);
}

static uint32_t buddyTestPhysicalFloor() {
  if (!buddyAcceleratedTimeValid(testLifeTime)) return 0;
  uint32_t simulatedFloor = buddyClockFloor();
  uint32_t physicalFloor = testLifeTime.realBirth;
  if (simulatedFloor < testLifeTime.simulatedBirth ||
      !buddyAcceleratedRealAt(testLifeTime, simulatedFloor, physicalFloor))
    return testLifeTime.realBirth;
  return physicalFloor;
}

static uint32_t buddyTestSimulationEpoch(uint32_t physicalEpoch) {
  if (!buddyAcceleratedTimeValid(testLifeTime)) return physicalEpoch;
  uint32_t simulated = 0;
  if (!buddyAcceleratedNow(testLifeTime, physicalEpoch, simulated)) {
    storageLocked = true;
    clockFault = true;
    return buddyClockFloor();
  }
  return simulated;
}

static bool buddyTestMemorialReady(uint32_t physicalNow, uint32_t deathEpoch) {
  uint32_t physicalDeath = 0;
  return buddyAcceleratedRealAt(testLifeTime, deathEpoch, physicalDeath) &&
    physicalNow >= physicalDeath && physicalNow - physicalDeath >= 120UL;
}

#endif
