static uint32_t buddyClockAnchorEpoch = 0, buddyClockAnchorMs = 0;
static bool buddyBcdValid(uint8_t value) {
  return (value & 15) <= 9 && (value >> 4) <= 9;
}

static uint32_t buddyClockFloor() {
  if (buddy.magic != BUDDY_MAGIC) return 0;
  uint32_t floor = buddy.lastUpdateEpoch;
  if (buddy.lastWorldEpoch > floor) floor = buddy.lastWorldEpoch;
  if (buddy.lastAttentionEpoch > floor) floor = buddy.lastAttentionEpoch;
  for (const BuddyMemory &memory : buddy.memories)
    if (memory.epoch > floor) floor = memory.epoch;
  for (const WorldMemory &event : buddy.events)
    if (event.epoch > floor) floor = event.epoch;
  return floor;
}

static bool buddyDateFromEpoch(uint32_t epoch, RtcDateTime &dt) {
  uint32_t day = epoch / 86400UL;
  for (uint16_t year = 2025; year <= 2099; ++year) {
    for (uint8_t month = 1; month <= 12; ++month) {
      uint32_t first = daysFromCivil(year, month, 1);
      uint32_t next = first + daysInMonth(year, month);
      if (day < first || day >= next) continue;
      uint32_t seconds = epoch % 86400UL;
      dt = {year, month, (uint8_t)(day - first + 1),
            (uint8_t)(seconds / 3600UL), (uint8_t)((seconds / 60UL) % 60UL),
            (uint8_t)(seconds % 60UL)};
      return true;
    }
  }
  return false;
}

#ifdef ARDUINO
struct BuddyI2cLines {
  void releaseSda() { gpio_set_level(I2C_SDA_PIN, 1); }
  void releaseScl() { gpio_set_level(I2C_SCL_PIN, 1); }
  void driveSdaLow() { gpio_set_level(I2C_SDA_PIN, 0); }
  void driveSclLow() { gpio_set_level(I2C_SCL_PIN, 0); }
  bool sdaHigh() const { return gpio_get_level(I2C_SDA_PIN) != 0; }
  bool sclHigh() const { return gpio_get_level(I2C_SCL_PIN) != 0; }
  void pause() const { esp_rom_delay_us(10); }
};
#endif

// Only a failed RTC transaction may detach the shared controller. Avoid
// bouncing a healthy bus after every cable restart or during settled checks.
static void recoverBuddyClockBusAfterCommFailure() {
#ifdef ARDUINO
  if (clockBusClearAttempts < UINT16_MAX) ++clockBusClearAttempts;
  Wire.end();
  gpio_config_t lines = {};
  lines.pin_bit_mask = (1ULL << I2C_SDA_PIN) | (1ULL << I2C_SCL_PIN);
  lines.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  lines.pull_up_en = GPIO_PULLUP_ENABLE;
  lines.pull_down_en = GPIO_PULLDOWN_DISABLE;
  bool configured = gpio_config(&lines) == ESP_OK;
  BuddyI2cLines bus;
  clockBusLinesBefore = configured ? (bus.sdaHigh() ? 1 : 0) |
    (bus.sclHigh() ? 2 : 0) : 0;
  bool cleared = configured && buddyClearI2cBus(bus);
  if (!cleared && clockBusClearFailures < UINT16_MAX) ++clockBusClearFailures;
  delay(2);
  if (!Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN) || !Wire.setClock(100000)) {
    if (clockBusReinitFailures < UINT16_MAX) ++clockBusReinitFailures;
  }
#endif
}

struct BuddyClockSample {
  bool communicationOk, calendarOk, oscillatorStop;
  uint32_t epoch;
  uint8_t raw[7];
};

static BuddyClockSample sampleBuddyClock() {
  BuddyClockSample sample = {};
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    if (i2cReadRegister(I2C_RTC_ADDR, 0x04, sample.raw, sizeof(sample.raw))) {
      sample.communicationOk = true;
      break;
    }
    if (attempt == 1) recoverBuddyClockBusAfterCommFailure();
  }
  if (!sample.communicationOk) return sample;
  memcpy(clockLastRaw, sample.raw, sizeof(clockLastRaw));
  sample.oscillatorStop = (sample.raw[0] & 0x80) != 0;
  bool bcdOk = buddyBcdValid(sample.raw[0] & 0x7f) &&
    buddyBcdValid(sample.raw[1] & 0x7f) && buddyBcdValid(sample.raw[2] & 0x3f) &&
    buddyBcdValid(sample.raw[3] & 0x3f) && buddyBcdValid(sample.raw[5] & 0x1f) &&
    buddyBcdValid(sample.raw[6]);
  if (!bcdOk) return sample;
  RtcDateTime dt = { (uint16_t)(2000 + bcdToDec(sample.raw[6])),
    bcdToDec(sample.raw[5] & 0x1f), bcdToDec(sample.raw[3] & 0x3f),
    bcdToDec(sample.raw[2] & 0x3f), bcdToDec(sample.raw[1] & 0x7f),
    bcdToDec(sample.raw[0] & 0x7f) };
  sample.calendarOk = validDate(dt) && dt.day <= daysInMonth(dt.year, dt.month) &&
    dt.hour < 24 && dt.minute < 60 && dt.second < 60;
  if (sample.calendarOk) sample.epoch = toEpoch(dt);
  return sample;
}

// The life save supplies a lower bound, never a fabricated interval of age.
// Only the RTC is written here; BuddyState and its A/B checkpoints are intact.
uint32_t buddyNow() {
  if (storageLocked) {
    clockFault = true;
    rtcAvailable = false;
    return buddyClockFloor();
  }
  uint32_t ticks = millis();
  BuddyClockSample sample = sampleBuddyClock();
  // First-run provisioning only: there is no existing life whose elapsed
  // time could be fabricated. Never seed over a saved or orphaned history.
  if (buddy.magic != BUDDY_MAGIC && introRequired && !introLocked &&
      sample.communicationOk && (!sample.calendarOk || sample.oscillatorStop)) {
    bool fresh = buddyLifeClockOffsetSeconds == 0;
    const char *history[] = {"life-a", "life-b", "state", "aim-commit",
      "home-a", "home-b", "aim-a", "aim-b", "site-a", "site-b", "lifeclock"};
    for (const char *key : history) if (preferences.getBytesLength(key)) fresh = false;
    if (fresh && (!lastClockRepairMs || (int32_t)(ticks - lastClockRepairMs) >= 30000)) {
      lastClockRepairMs = ticks ? ticks : 1;
      RtcDateTime seed = buildDateTime();
      if (validDate(seed) && setRtc(seed)) sample = sampleBuddyClock();
    }
  }
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  uint32_t floor = buddyTestPhysicalFloor();
  uint32_t logicalRtc = sample.epoch;
  bool translated = true;
#else
  uint32_t floor = buddyClockFloor();
  uint32_t logicalRtc = 0;
  bool translated = !sample.calendarOk ||
    buddyLifeEpochFromPhysical(sample.epoch, buddyLifeClockOffsetSeconds, logicalRtc);
#endif
  BuddyClockDecision decision = decideBuddyClock(
    sample.communicationOk, sample.calendarOk && translated, sample.oscillatorStop,
    logicalRtc, clockTrusted ? max(floor, clockLastEpoch) : floor, clockTrusted,
    buddyClockAnchorEpoch, buddyClockAnchorMs, ticks);
  bool wasFault = clockFault;
  if (decision.reason == CLOCK_REASON_NONE) {
    clockFault = false;
    rtcAvailable = true;
    clockFaultReason = CLOCK_REASON_NONE;
    // Keep an independent runtime anchor across samples: accepting every
    // reading as a fresh anchor hid frozen clocks and cumulative fast drift.
    if (!clockTrusted || ticks - buddyClockAnchorMs >= 6UL * 3600UL * 1000UL) {
      buddyClockAnchorEpoch = decision.effective;
      buddyClockAnchorMs = ticks;
    }
    clockTrusted = true;
    clockLastEpoch = decision.effective;
    clockLastMs = ticks;
    if (wasFault) {
      cleanPartialRequested = true; // clear the old AGING PAUSED footer
      needsRender = true;
    }
#ifdef BUDDY_ACCELERATED_LIFE_TEST
    return buddyTestSimulationEpoch(decision.effective);
#else
    return decision.effective;
#endif
  }
  clockFaultReason = decision.reason;
  clockLastBadEpoch = sample.epoch;
  if (!clockIncidentCaptured && floor) {
    clockIncidentCaptured = true;
    clockRecoveryPending = true;
    clockRecoveryReason = decision.reason;
    clockRecoveryFloor = floor;
    memcpy(clockRecoveryRaw, sample.raw, sizeof(clockRecoveryRaw));
  }
  if (decision.repair && (!lastClockRepairMs ||
      (uint32_t)(ticks - lastClockRepairMs) >= 30000UL)) {
    lastClockRepairMs = ticks ? ticks : 1;
    if (clockRepairAttempts < 255) ++clockRepairAttempts;
    RtcDateTime repairDate = {};
    uint32_t physicalRepair = 0;
    if (
#ifdef BUDDY_ACCELERATED_LIFE_TEST
        buddyPhysicalEpochFromLife(decision.effective, 0, physicalRepair) &&
#else
        buddyPhysicalEpochFromLife(decision.effective, buddyLifeClockOffsetSeconds,
                                   physicalRepair) &&
#endif
        buddyDateFromEpoch(physicalRepair, repairDate) && setRtc(repairDate)) {
      BuddyClockSample check = sampleBuddyClock();
      uint32_t logicalCheck = 0;
      if (check.communicationOk && check.calendarOk && !check.oscillatorStop &&
#ifdef BUDDY_ACCELERATED_LIFE_TEST
          buddyLifeEpochFromPhysical(check.epoch, 0, logicalCheck) &&
#else
          buddyLifeEpochFromPhysical(check.epoch, buddyLifeClockOffsetSeconds,
                                     logicalCheck) &&
#endif
          logicalCheck >= decision.effective && logicalCheck - decision.effective <= 2) {
        lastClockRecoveredReason = decision.reason;
        clockFaultReason = CLOCK_REASON_NONE;
        clockFault = false;
        rtcAvailable = true;
        clockTrusted = true;
        clockLastEpoch = logicalCheck;
        clockLastMs = millis();
        buddyClockAnchorEpoch = logicalCheck;
        buddyClockAnchorMs = clockLastMs;
        if (wasFault) cleanPartialRequested = true;
        needsRender = true;
#ifdef BUDDY_ACCELERATED_LIFE_TEST
        return buddyTestSimulationEpoch(logicalCheck);
#else
        return logicalCheck;
#endif
      }
    }
  }
  clockFault = true;
  rtcAvailable = false;
  if (!wasFault) needsRender = true;
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  return buddyTestSimulationEpoch(decision.effective);
#else
  return decision.effective;
#endif
}

// A cable-exit reset can coincide with an unsettled I2C peripheral. The
// USB-C data pins and I2C pins are separate; the cause is not established.
// Retry here when the owner re-enters Cable Friends rather than requiring an
// unrelated interaction to clear a stale clock fault first.
static bool recoverBuddyClockForCable() {
  if (!clockFault) return true;
  for (uint8_t attempt = 0; attempt < 8; ++attempt) {
    delay(150);
    if (buddyNow() && !clockFault) return true;
  }
  return false;
}
