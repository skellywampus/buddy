// Independent from BuddyState. Losing this diagnostic never blocks a life load.
struct BuddyRecoveryRecord {
  uint32_t magic, sequence, checksum;
  uint8_t resetReason, clockReason, selectedSlot, flags;
  uint32_t selectedSequence, savedFloor, badRtcEpoch;
  uint8_t rtcRaw[7], audioError, micError, radioError;
};
static BuddyRecoveryRecord lastRecovery = {};
static int8_t recoverySlot = -1;
static int8_t recoveryReportedState = -1;

static uint32_t recoveryChecksum(const BuddyRecoveryRecord &record) {
  BuddyRecoveryRecord copy;
  memcpy(&copy, &record, sizeof(copy));
  copy.checksum = 0;
  const uint8_t *bytes = (const uint8_t *)&copy;
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < sizeof(copy); ++i) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}

static bool readRecovery(Preferences &store, const char *key, BuddyRecoveryRecord &record) {
  return store.getBytesLength(key) == sizeof(record) &&
    store.getBytes(key, &record, sizeof(record)) == sizeof(record) &&
    record.magic == 0x42524331UL && record.checksum == recoveryChecksum(record);
}

static void loadBuddyRecovery() {
  Preferences store;
  if (!store.begin("buddy-recovery", true)) return;
  BuddyRecoveryRecord a = {}, b = {};
  bool hasA = readRecovery(store, "rec-a", a);
  bool hasB = readRecovery(store, "rec-b", b);
  store.end();
  if (!hasA && !hasB) return;
  bool useB = hasB && (!hasA || (int32_t)(b.sequence - a.sequence) > 0);
  lastRecovery = useB ? b : a;
  recoverySlot = useB ? 1 : 0;
}

static bool recordBuddyRecovery(uint8_t flags, BuddyClockReason reason) {
  BuddyRecoveryRecord record;
  memset(&record, 0, sizeof(record));
  record.magic = 0x42524331UL;
  record.sequence = lastRecovery.sequence + 1;
  record.resetReason = bootResetReason;
  record.clockReason = reason;
  record.selectedSlot = activeSaveSlot < 0 ? 255 : activeSaveSlot;
  record.flags = flags;
  record.selectedSequence = saveSequence;
  record.savedFloor = clockRecoveryFloor ? clockRecoveryFloor : buddyClockFloor();
  record.badRtcEpoch = clockLastBadEpoch;
  memcpy(record.rtcRaw, clockRecoveryRaw, sizeof(record.rtcRaw));
  record.audioError = buddyAudioError;
  record.micError = buddyMicError;
  record.radioError = nearbyRadioError;
  record.checksum = recoveryChecksum(record);
  Preferences store;
  if (!store.begin("buddy-recovery", false)) return false;
  int8_t nextSlot = recoverySlot == 0 ? 1 : 0;
  const char *key = nextSlot == 0 ? "rec-a" : "rec-b";
  BuddyRecoveryRecord check = {};
  bool ok = store.putBytes(key, &record, sizeof(record)) == sizeof(record) &&
    readRecovery(store, key, check) && check.sequence == record.sequence;
  store.end();
  if (ok) { lastRecovery = record; recoverySlot = nextSlot; }
  return ok;
}

// Flags: RTC incident=1, repaired=2, life rollback=4, five-click=8,
// watchdog=16, brownout=32, storage lock=64.
static void serviceBuddyRecovery() {
  if (!clockRecoveryPending) return;
  int8_t state = clockFault ? 0 : 1;
  uint8_t resetFlags = (bootResetReason == ESP_RST_TASK_WDT ||
    bootResetReason == ESP_RST_WDT || bootResetReason == ESP_RST_INT_WDT) ? 16 :
    bootResetReason == ESP_RST_BROWNOUT ? 32 : 0;
  if (state != recoveryReportedState &&
      recordBuddyRecovery(1 | (state ? 2 : 0) | (lifeRollbackDetected ? 4 : 0) |
                          resetFlags,
                          clockRecoveryReason)) {
    recoveryReportedState = state;
    if (state) clockRecoveryPending = false;
  }
}
