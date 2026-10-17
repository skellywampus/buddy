void setBuddyLed(bool on) {
  digitalWrite(STATUS_LED_PIN, (STATUS_LED_ACTIVE_LOW ? !on : on) ? HIGH : LOW);
}

void blinkBuddyLed(uint8_t count) {
  for (uint8_t i=0; i<count; i++) {
    setBuddyLed(true); delay(55); setBuddyLed(false);
    if (i+1<count) delay(55);
  }
}

// Keep sampling while the main loop is blocked by e-paper, audio or storage.
// Only the loop task handles the resulting gestures and changes Buddy state.
void buddyButtonTask(void *) {
  BuddyButtonRecognizer recognizer;
  recognizer.stableHigh = recognizer.candidateHigh = digitalRead(BOOT_BUTTON_PIN) != LOW;
  if (!recognizer.stableHigh) {
    recognizer.pressedSince = millis();
    recognizer.longSent = true; // a held button across reboot is not a new hold
  }
  bool ledPulseOn = false;
  uint32_t ledOffAtMs = 0;
  for (;;) {
    uint32_t now = millis();
    bool wasHigh = recognizer.stableHigh;
    ButtonEvent event = recognizer.poll(digitalRead(BOOT_BUTTON_PIN) != LOW, now);
    buttonDown = !recognizer.stableHigh;
    if (wasHigh && !recognizer.stableHigh) {
      viewOpenedMs = now; // activity starts on press, not after gesture classification
      if (buddyLedFeedbackEnabled) {
        setBuddyLed(true);
        ledPulseOn = true;
        ledOffAtMs = now + 55;
      }
    }
    if (ledPulseOn && (!buddyLedFeedbackEnabled || (int32_t)(now - ledOffAtMs) >= 0)) {
      setBuddyLed(false);
      ledPulseOn = false;
    }
    if (event != BUTTON_NONE) xQueueSend(buddyButtonEvents, &event, 0);
    vTaskDelay(pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1);
  }
}

ButtonEvent readBuddyButton() {
  if (buddyButtonEvents) {
    ButtonEvent event = BUTTON_NONE;
    xQueueReceive(buddyButtonEvents, &event, 0);
    return event;
  }
  // A failed task/queue allocation must not leave BOOT unusable.
  bool wasHigh = buddyButton.stableHigh;
  ButtonEvent event = buddyButton.poll(digitalRead(BOOT_BUTTON_PIN) != LOW, millis());
  buttonDown = !buddyButton.stableHigh;
  if (wasHigh && buttonDown) viewOpenedMs = millis();
  return event;
}

void releaseBuddyPowerHolds() {
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(EPD_PWR_PIN); gpio_hold_dis(AUDIO_PWR_PIN); gpio_hold_dis(AUDIO_PA_PIN);
  gpio_hold_dis(VBAT_PWR_PIN); gpio_hold_dis(EPD_TP_RST_PIN); gpio_hold_dis(STATUS_LED_PIN);
}

#include "buddy_power_setup.inc"

void setupBuddyDisplay() {
  custom_lcd_spi_t cfg = {};
  cfg.cs=EPD_CS_PIN; cfg.dc=EPD_DC_PIN; cfg.rst=EPD_RST_PIN; cfg.busy=EPD_BUSY_PIN;
  cfg.mosi=EPD_MOSI_PIN; cfg.scl=EPD_SCK_PIN; cfg.spi_host=EPD_SPI_NUM; cfg.buffer_len=5000;
  display = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, cfg);
  display->EPD_Init();
  display->EPD_Clear();
}

static uint32_t buddyNextIdleWorkMs(uint32_t nowMs) {
  BuddyWorkSchedule work = {};
  work.simulationLastMs = lastSimulationMs;
  work.simulationPeriodMs = SIMULATION_INTERVAL_MS;
  work.climateLastMs = lastEnvironmentMs;
  work.climatePeriodMs = buddyClimateInterval(latestClimateValid, climateConsecutiveFailures);
  work.storageLastMs = lastStorageRetryMs;
  work.momentActive = buddy.alive && !clockFault && momentScheduled;
  work.momentAtMs = nextMomentMs;
  work.lowBatteryConfirm = criticalBattery.lowReadings != 0;
  work.batteryLastMs = lastCriticalCheckMs;
  work.powerLastMs = lastPowerSampleMs;
  work.powerPeriodMs = BUDDY_POWER_INTERVAL_MS;
  work.micRetryPending = buddyListeningWanted(buddyListeningWindow, buddy.soundEnabled) &&
    !buddyMicActive && !buddyRxChannel && !buddyTxChannel && !nearbyRadioActive && !cableFriendsActive();
  work.micRetryLastMs = buddyMicRetryMs;
  work.menuOpen = currentView != VIEW_BUDDY;
  work.menuLastActionMs = viewOpenedMs;
  work.voiceMessageUntilMs = buddyVoiceUntilMs;
  work.voiceCommandUntilMs = buddyVoiceCommandUntilMs;
  work.listeningWindowOpen = buddyListeningWindow.open;
  work.listeningWindowAtMs = buddyListeningWindow.deadlineMs;
  work.cableDeadlineActive = cableFriendsActive();
  work.cableAtMs = cableSession.phase == CABLE_WAITING ?
    cableSession.startedMs + BUDDY_CABLE_WAIT_MS :
    cableSession.lastPeerMs + BUDDY_CABLE_SILENCE_MS;
  bool rapidClockRepair = buddyCableRapidClockCheck(cableClockRecoveryPending,
    clockFault,bootResetReason == ESP_RST_SW,nowMs - awakeBootMs);
  bool fastClockCheck = clockFault || rapidClockRepair;
  work.clockRepairPending = buddyCableClockRepairAllowed(cableFriendsActive()) &&
    (cableClockRecoveryPending || clockFault);
  work.clockRepairLastMs = fastClockCheck ? lastCableClockCheckMs : lastClockServiceMs;
  work.clockRepairPeriodMs = buddyClockCheckIntervalMs(clockFault, rapidClockRepair);
  work.cableReturnRepaintPending = cableReturnRepaintPending;
  work.cableReturnRepaintAtMs = cableReturnRepaintAtMs;
  return buddyNextWorkDelay(nowMs, work);
}

void reportBuddyStatus() {
  portENTER_CRITICAL(&buddyVoiceLock);
  BuddyVoiceDetector voiceSnapshot = buddyVoiceDetector;
  uint16_t micPeak = buddyMicPeak;
  uint32_t micAbove = buddyMicAboveFrames;
  buddyMicPeak = 0;
  buddyMicAboveFrames = 0;
  portEXIT_CRITICAL(&buddyVoiceLock);
  // Temporary incident probe: raw PCF85063 registers before Buddy's
  // validation/recovery logic interprets them. This does not write the RTC.
  uint8_t rtcRaw[11] = {};
  bool rtcRawOk = i2cReadRegister(I2C_RTC_ADDR, 0x00, rtcRaw, sizeof(rtcRaw));
  Serial.printf("RTC_RAW read=%u regs=", rtcRawOk);
  if (rtcRawOk) for (uint8_t value : rtcRaw) Serial.printf("%02x", value);
  Serial.printf(" trusted=%u last_trusted=%lu trusted_ms=%lu saved=%lu fault=%u\n",
    clockTrusted, (unsigned long)clockLastEpoch, (unsigned long)clockLastMs,
    (unsigned long)buddy.lastUpdateEpoch, clockFault);
  Serial.printf("CLOCK_BUS clear_attempts=%u clear_failures=%u init_failures=%u before_sda=%u before_scl=%u\n",
    clockBusClearAttempts,clockBusClearFailures,clockBusReinitFailures,
    clockBusLinesBefore & 1,(clockBusLinesBefore >> 1) & 1);
  uint32_t now = buddyNow();
  Serial.printf("FIRMWARE %s\n", BUDDY_IMAGE_TAG);
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  Serial.printf("LIFE_TEST mode=6H real_birth=%lu simulated_birth=%lu lifespan=%u generation=%u clock_saved=%u\n",
    (unsigned long)testLifeTime.realBirth, (unsigned long)testLifeTime.simulatedBirth,
    testLifeTime.lifespanDays, testLifeTime.generation, testClockReady);
#endif
  Serial.printf("BUDDY v%u gen=%u seed=%08lx day=%lu alive=%u clock_ok=%u save_ok=%u recovery=%u sd=%u stay_awake=%u sound=%u audio_power=%u audio_pa=%u mic_i2s=%u audio_error=%u audio_reg=%02x mic=%u mic_error=%u mic_level=%u mic_floor=%lu mic_threshold=%lu mic_peak=%u mic_above=%lu mic_voiced_ms=%u mic_heard=%lu wake=%u pins=%llx\n",
    buddy.version,buddy.generation,(unsigned long)buddy.seed,(unsigned long)buddyAgeDays(now),
    buddy.alive,!clockFault,!saveFailed,storageLocked,sdAvailable,buddy.stayAwake,
    buddy.soundEnabled,digitalRead(AUDIO_PWR_PIN),digitalRead(AUDIO_PA_PIN),buddyRxChannel != nullptr,
    buddyAudioError,buddyAudioRegister,buddyMicActive,buddyMicError,
    voiceSnapshot.lastLevel,(unsigned long)(voiceSnapshot.noise/8),
    (unsigned long)voiceSnapshot.threshold(),micPeak,
    (unsigned long)micAbove,voiceSnapshot.voicedMs,
    (unsigned long)buddyMicHeardCount,
    (unsigned)esp_sleep_get_wakeup_cause(),(unsigned long long)esp_sleep_get_ext1_wakeup_status());
  Serial.printf("HOME phase=%u work=%u condition=%u builder=%u cycles=%u save_ok=%u recovery=%u\n",
    buddyHouse.phase,buddyHouse.work,buddyHouse.condition,buddyHouse.builderGeneration,
    buddyHouse.cycles,!houseSaveFailed,houseLocked);
  Serial.printf("AMBITION kind=%u progress=%u legacy=%u outcome=%u save_ok=%u recovery=%u\n",
    buddyAmbition.kind,buddyAmbition.progress,buddyAmbition.legacyKind,
    buddyAmbition.lastOutcome,!ambitionSaveFailed,ambitionLocked);
  Serial.printf("LEGACY marks=%u,%u,%u,%u,%u,%u,%u,%u save_ok=%u recovery=%u\n",
    buddyLegacy.condition[0],buddyLegacy.condition[1],buddyLegacy.condition[2],
    buddyLegacy.condition[3],buddyLegacy.condition[4],buddyLegacy.condition[5],
    buddyLegacy.condition[6],buddyLegacy.condition[7],!legacySaveFailed,legacyLocked);
  Serial.printf("CABLE active=%u phase=%u error=%u sent=%lu received=%lu bad=%lu queue_drop=%lu save_ok=%u recovery=%u known=%u visible=%u rtc_marker=%u marker_clear_failed=%u clock_fault=%u clock_reason=%u display_override=%u watchdog_stage=%u\n",
    cableFriendsActive(),cableSession.phase,cableModeError,
    (unsigned long)cableFramesSent,(unsigned long)cableFramesReceived,
    (unsigned long)cableBadFrames,(unsigned long)cableQueueOverflows,
    !nearbySaveFailed && !nearbySettingSaveFailed,
    nearbyStorageLocked,nearbySeenCount(),nearbySceneVisible(),
    cableClockRecoveryPending,cableMarkerClearFailed,clockFault,clockFaultReason,
    cableReturnActivityOverride,cableLastWatchdogStage);
  Serial.printf("CABLE_TRACE last_op=%s last_error=%ld last_phase=%lu last_resources=%08lx last_rtc=%lu current_op=%s current_error=%ld phase=%u queue=%u pads=%u uart=%u reserved=%u worker=%u worker_running=%u armed=%u startup=%u restart_requested=%u reset=%u\n",
    buddyCableOperationText((BuddyCableOperation)cableLastTrace.operation),
    (long)cableLastTrace.error,(unsigned long)cableLastTrace.phase,
    (unsigned long)cableLastTrace.resources,(unsigned long)cableLastTrace.rtcRecovery,
    buddyCableOperationText((BuddyCableOperation)cableTrace.operation),
    (long)cableTrace.error,cableSession.phase,cableRxQueue != nullptr,
    cablePadsClaimed,cableUartReady,0,
    cableTaskHandle() != nullptr,
    cableLoadFlag(&cableTaskRunning),cableLoadFlag(&cableLinkArmed),
    cableLoadFlag(&cableStartupComplete),cableRestartRequested,bootResetReason);
  Serial.printf("SOUND mode=%u save_retry=%u legacy_enabled=%u\n",
    buddySoundMode,buddySoundSaveFailed,buddy.soundEnabled);
  Serial.printf("CLIMATE valid=%u samples=%u failures=%lu last_good_ms=%lu\n",
    latestClimateValid,climateSampleCount,(unsigned long)climateReadFailures,
    (unsigned long)climateLastGoodMs);
  Serial.printf("POWER battery_mv=%u usb=%u charge_trend=%u rise=%u fall=%u orientation=%u rotated=%u\n",
    latestBatteryV > 0.0f ? (unsigned)(latestBatteryV * 1000.0f) : 0,
    usbHostPowerPresent,batteryChargeTrend,risingBatterySamples,fallingBatterySamples,
    orientationMode,displayRotation180);
  Serial.printf("SPEECH active=%u resident=%u paused=%u model=%d error=%u\n",
    buddySpeechActive && !buddySpeechPaused,buddySpeechActive,buddySpeechPaused,
    buddySpeechModelStatus,buddySpeechError);
  uint32_t nowMs = millis();
  uint32_t listenRemaining = buddyListeningWanted(buddyListeningWindow, buddy.soundEnabled) &&
    !buddyListeningDue(buddyListeningWindow, nowMs) ? buddyListeningWindow.deadlineMs - nowMs : 0;
  Serial.printf("LISTEN open=%u remaining_ms=%lu mic_start_ms=%lu starts=%lu stops=%lu heap=%lu psram=%lu tasks=%lu\n",
    buddyListeningWanted(buddyListeningWindow, buddy.soundEnabled),
    (unsigned long)listenRemaining, (unsigned long)buddyMicStartupMs,
    (unsigned long)buddyMicStarts, (unsigned long)buddyMicStops,
    (unsigned long)ESP.getFreeHeap(), (unsigned long)ESP.getFreePsram(),
    (unsigned long)uxTaskGetNumberOfTasks());
  Serial.printf("SCHED next_ms=%lu sleeps=%lu requested_ms=%lu elapsed_ms=%lu wake=%u\n",
    (unsigned long)buddyNextIdleWorkMs(nowMs), (unsigned long)buddyLightSleepCount,
    (unsigned long)buddyLastSleepRequestedMs, (unsigned long)buddyLastSleepElapsedMs,
    buddyLastLightWake);
  Serial.printf("POWER_SERVICE age_ms=%lu valid=%u battery_latch=%u wake_ready=%u sleep_failures=%lu\n",
    (unsigned long)(nowMs-lastPowerSampleMs),buddyBatteryVoltageValid(latestBatteryV),
    digitalRead(VBAT_PWR_PIN),buddyLightSleepWakeReady,(unsigned long)buddyLightSleepRetry.failures);
  Serial.printf("INTRO required=%u step=%u cursor=%u save_ok=%u recovery=%u\n",
    introRequired,buddyIntro.step,buddyIntro.cursor,!introSaveFailed,introLocked);
  Serial.printf("RECOVERY seq=%lu reset=%u reason=%u flags=%u floor=%lu bad=%lu slot=%u life_seq=%lu raw=",
    (unsigned long)lastRecovery.sequence,lastRecovery.resetReason,lastRecovery.clockReason,
    lastRecovery.flags,(unsigned long)lastRecovery.savedFloor,
    (unsigned long)lastRecovery.badRtcEpoch,lastRecovery.selectedSlot,
    (unsigned long)lastRecovery.selectedSequence);
  for (uint8_t value : lastRecovery.rtcRaw) Serial.printf("%02x", value);
  Serial.printf(" current_reason=%u repair=%u rollback=%u wake_attempt=%u wake_ok=%u wdt=%u\n",
    clockFaultReason,clockRepairAttempts,lifeRollbackDetected,
    wakeAudioAttempted,wakeAudioOk,watchdogReady);
}


#include "buddy_climate.inc"

#include "buddy_battery_sample.inc"

void sampleBuddyEnvironment() { sampleBuddyClimate(); sampleBuddyPower(); }

bool updateDisplayOrientation() {
  bool rotated = orientationMode == ORIENTATION_FLIPPED ||
    (orientationMode == ORIENTATION_AUTO && (cableFriendsActive() || buddyExternalPowerPresent()));
  if (rotated == displayRotation180) return false;
  displayRotation180 = rotated;
  cleanPartialRequested = true;
  return true;
}

void enterBuddySleep(bool manual, bool critical) {
  buddyLedFeedbackEnabled = false;
  buddyListeningClose(buddyListeningWindow);
  stopCableFriends(); // release USB pins before either board can sleep
  stopNearbyRadio();
  uint32_t now = buddyNow();
  updateBuddySimulation(now);
  appendDailyJournal(now);
  saveBuddy();
  if (critical && saveFailed) saveBuddy(); // retain the previous valid slot if both attempts fail
  currentView = VIEW_BUDDY;
  stopBuddyMic();
  stopBuddySpeech(); // Deep sleep releases the retained recognition engine.
  sleepSdCardPresent = sdAvailable;
  suspendBuddyStorageForSleep();
  renderingDoze = !manual && !critical;
  if (critical) {
    renderSleepFace();
    buddyFill(3,143,194,51);
    buddyCentered(150,"LOW BATTERY",2,true);
    buddyCentered(171,"CHARGE THEN WAKE",1);
    if (sleepSdCardPresent) buddyCentered(187,"SD SAFE TO SWAP",1);
    display->EPD_Init();
    display->EPD_Display();
  } else if (manual) {
    renderSleepFace();
    display->EPD_Init();
    display->EPD_Display();
  } else {
    updateBuddyMoment(now, true);
    renderBuddy(true);
  }
  renderingDoze = false;
  // Manual sleep chimes after the face is visible; a critical shutdown is silent.
  if (!critical && buddySleepSoundDue(manual, hourlyDozeWake)) playBuddySound(SOUND_SLEEP);
  delay(180);
  stopBuddyAudio();
  shutdownNearbyStackForSleep();
  digitalWrite(AUDIO_PWR_PIN, HIGH); digitalWrite(AUDIO_PA_PIN, LOW);
  setBuddyLed(false);
  digitalWrite(VBAT_PWR_PIN, LOW); digitalWrite(EPD_TP_RST_PIN, LOW); digitalWrite(EPD_PWR_PIN, HIGH);
  gpio_hold_en(EPD_PWR_PIN); gpio_hold_en(AUDIO_PWR_PIN); gpio_hold_en(AUDIO_PA_PIN);
  gpio_hold_en(VBAT_PWR_PIN); gpio_hold_en(EPD_TP_RST_PIN); gpio_hold_en(STATUS_LED_PIN);
  gpio_deep_sleep_hold_en();
  disableLoopWDT(); // A deliberate button hold must never be mistaken for a hang.
  uint32_t releaseStartedMs = millis();
  while (buddyWaitForSleepRelease(critical, digitalRead(BOOT_BUTTON_PIN) == LOW,
                                  millis()-releaseStartedMs)) delay(10);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PWR_BUTTON_PIN, INPUT_PULLUP);
  rtc_gpio_pulldown_dis(BOOT_BUTTON_PIN);
  rtc_gpio_pullup_en(BOOT_BUTTON_PIN);
  rtc_gpio_pulldown_dis(PWR_BUTTON_PIN);
  rtc_gpio_pullup_en(PWR_BUTTON_PIN);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  // BOOT is the interaction button. Keep its RTC pull-up active so a brief
  // press reaches EXT1 during deep sleep; PWR remains an alternative wake.
  // Critical shutdown must not stay awake or immediately reboot because a
  // child, pocket or failed button is holding a wake input low.
  uint64_t wakePins = buddySleepWakePins(critical, digitalRead(BOOT_BUTTON_PIN) != LOW,
    digitalRead(PWR_BUTTON_PIN) != LOW, 1ULL << (uint8_t)BOOT_BUTTON_PIN,
    1ULL << (uint8_t)PWR_BUTTON_PIN);
  if (wakePins) ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(wakePins,ESP_EXT1_WAKEUP_ANY_LOW));
  // Only manual/critical sleep reaches this path. No timer means no battery
  // drain from hourly reboot, and SD remains unmounted until a real wake.
  if (!critical && !storageLocked) armBuddyWakeSound(preferences);
  esp_deep_sleep_start();
}

static bool persistCableClockRecoveryMarker(bool pending) {
  Preferences marker;
  if (!marker.begin("buddy-cable", false)) return false;
  bool ok = buddyCableWriteRecoveryMarker(marker, pending);
  marker.end();
  return ok;
}

// A post-cable restart has finished normal setup by the time this runs. Give
// the clock and sensors a fresh, coordinated sample, then catch Buddy's life
// up only if that RTC sample is trusted. Never infer elapsed age from millis.
static uint32_t refreshBuddyAfterCableReturn() {
  uint32_t now = buddyNow();
  sampleBuddyEnvironment();
  lastEnvironmentMs = millis();
  if (now && !clockFault && !storageLocked) {
    updateBuddySimulation(now);
    updateBuddyMoment(now, true);
  }
  return now;
}

void exitCableFriends() {
  bool met = cableSession.phase == CABLE_CONNECTED && cableScenePeer.sender;
  uint32_t resources = (cableRxQueue ? 1u : 0u) |
    (cablePadsClaimed ? 2u : 0u) | (cableUartReady ? 4u : 0u) |
    (cableTaskHandle() ? 8u : 0u) | (cableLoadFlag(&cableLinkArmed) ? 16u : 0u);
  buddyCableTraceSnapshot(cableTrace, resources, cableSession.phase,
                          cableClockRecoveryPending);
  // Stop IRQ and worker before the long, blocking e-paper waveform. Keep the
  // peer portrait/session until the farewell has finished drawing.
  quiesceCableLink();
  buddyCableTraceStep(cableTrace, CABLE_OP_FAREWELL);
  if (cableBootFailure == CABLE_ERROR_NONE) renderCableFarewell(met);
  stopCableFriends();
  buddyCableNoticeSet(cableRestartNotice, cableBootFailure);
  cableClockRecoveryPending = true;
  bool markerSaved = persistCableClockRecoveryMarker(true);
  // Keep the marker across restart. The RTC/I2C bus can look healthy for an
  // immediate read after GPIO19/20 are released, then fault as normal services
  // resume; startup clears the marker only after a settled, repeated check.
  Serial.printf("CABLE_EXIT recovery_marker=%u rebooting=1\n", markerSaved);
  // The BOOT hold that selected Exit must not be interpreted as a fresh hold
  // by the new session. This also keeps the strapping pin released at reset.
  disableLoopWDT();
  while (digitalRead(BOOT_BUTTON_PIN) == LOW) delay(10);
  delay(80);
  ESP.restart();
}

void handleBuddyButton(ButtonEvent event) {
  uint32_t now = buddyNow();
  lastUserMs = millis();
  viewOpenedMs = millis();
  if (hourlyDozeWake) {
    hourlyDozeWake = false;
    resumeBuddyStorageAfterWake();
    serviceBuddyStorage(now);
  }
  if (introRequired && buddy.magic != BUDDY_MAGIC) {
    handleBuddyIntroButton(event);
    if (buddyIntro.step == INTRO_COMPLETE && birthBuddyFromIntro(now)) {
      setupBuddyHouse(now);
      setupBuddyAmbition(now);
      setupBuddyLegacy(now);
      setupNearbyStorage();
      updateBuddyMoment(now, true);
      playBuddySound(SOUND_SPECIAL);
      touchBuddyListeningWindow();
      cleanPartialRequested = true;
      needsRender = true;
    }
    return;
  }
  if (storageLocked || buddy.magic != BUDDY_MAGIC) {
    if (event == BUTTON_RECOVER) {
      stopCableFriends();
      recordBuddyRecovery(8 | (storageLocked ? 64 : 0), clockFaultReason);
      ESP.restart();
    }
    if (event == BUTTON_LONG) enterBuddySleep(true);
    return;
  }
  if (event == BUTTON_RECOVER) {
    stopCableFriends();
    stopNearbyRadio();
    stopBuddyMic();
    if (!clockFault) saveBuddy();
    recordBuddyRecovery(8, clockFaultReason);
    SD_MMC.end();
    Serial.flush();
    ESP.restart();
    return;
  }
  // The recognized gesture is handled first; microphone initialization runs
  // after the screen response, so its ~1.2 s warm-up does not delay the click.
  touchBuddyListeningWindow();
  BuddyClickAction click = buddyClickAction(currentView, event);
  if (click == BUDDY_CLICK_INTERACT) {
    interactWithBuddy(now);
    updateBuddyMoment(now, true);
    cleanPartialRequested = true;
    needsRender = true;
    buddyInteractionSoundPending = buddy.soundEnabled;
  } else if (click == BUDDY_CLICK_OPEN_MENU) {
    currentView = VIEW_ABOUT;
    cleanPartialRequested = true;
    needsRender = true;
  } else if (click == BUDDY_CLICK_ADVANCE_MENU) {
    BuddyView previousView = currentView;
    if (currentView == VIEW_ABOUT) { currentView = VIEW_SETTINGS; settingsSelection = 0; }
    else if (settingsSelection + 1 < SETTINGS_COUNT) settingsSelection++;
    else currentView = VIEW_BUDDY;
    // A changed highlight is already drawn over the previous image plane.
    // Reserve the white clean pass for a real page change.
    if (currentView != previousView) cleanPartialRequested = true;
    needsRender = true;
    if (!buddyButtonEvents) blinkBuddyLed(1); // allocation-failure fallback
  } else if (click == BUDDY_CLICK_RETURN_HOME) {
    currentView = VIEW_BUDDY;
    cleanPartialRequested = true;
    needsRender = true;
  } else if (event == BUTTON_LONG) {
    if (currentView == VIEW_SETTINGS) {
      if (settingsSelection == SETTINGS_SOUND) {
        bool changed = setBuddySoundMode(buddyNextSoundMode(buddySoundMode));
        needsRender = true;
        renderBuddy(false);
        if (changed && buddy.soundEnabled) buddyInteractionSoundPending = true;
      } else if (settingsSelection == SETTINGS_CABLE_FRIENDS) {
        if (cableFriendsActive()) exitCableFriends();
        else if (!startCableFriends()) { needsRender = true; renderBuddy(false); }
      } else if (settingsSelection == SETTINGS_ORIENTATION) {
        orientationMode = (orientationMode + 1) % ORIENTATION_COUNT;
        updateDisplayOrientation();
        needsRender = true;
        renderBuddy(false);
        preferences.putUChar("orient", orientationMode);
        saveBuddy();
      }
    } else if (cableFriendsActive()) {
      exitCableFriends(); // a hold on the home face leaves the trade hall
    } else enterBuddySleep(true);
  }
}

void setup() {
  releaseBuddyPowerHolds();
  setupBuddyPower(); // keep battery power before USB, delays, sensors or saves
  Serial.begin(115200);
  delay(250);
  bootResetReason = (uint8_t)esp_reset_reason();
  cableLastTrace = buddyCableTraceTake(cableTrace);
  cableLastWatchdogStage = buddyCableTakeWatchdogMark(cableWatchdogMark,
    bootResetReason == ESP_RST_INT_WDT || bootResetReason == ESP_RST_TASK_WDT ||
    bootResetReason == ESP_RST_WDT);
  cableWatchdogFresh = cableLastWatchdogStage != CABLE_STAGE_OFF;
  // No BLE controller is initialized in the wired-only release. Keep old
  // contact saves untouched, but never classify a future panic as BLE here.
  // A pre-update automatic doze can still deliver one legacy timer wake.
  // Resume normally without rewriting the life or making a wake sound.
  bool legacyTimerWake = esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER;
  hourlyDozeWake = false;
  sdSuspended = false;
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(PWR_BUTTON_PIN, INPUT_PULLUP);
  lastButtonLevel = digitalRead(BOOT_BUTTON_PIN);
  buddyButton.stableHigh = buddyButton.candidateHigh = lastButtonLevel;
  if (!lastButtonLevel) buddyButton.pressedSince = millis();
  buddyButton.longSent = !lastButtonLevel; // ignore a hold carried through reset
  analogReadResolution(12);
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  // Load the life before trusting RTC time. An early RTC read would mark the
  // clock trusted without the save floor, then misclassify a normal boot gap
  // as a 120-second in-session jump.
  uint32_t now = 0;
  setupBuddyStorage();
  loadOrCreateBuddy(now);
  loadBuddyLifeClockOffset();
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  if (!storageLocked && !buddyTestClockLoad()) storageLocked = true;
#endif
  if (buddy.magic == BUDDY_MAGIC) loadBuddySoundMode();
  else buddySoundMode = SOUND_LOW;
  loadBuddyRecovery();
  Preferences cableMarker;
  if (cableMarker.begin("buddy-cable", true)) {
    cableClockRecoveryPending = cableMarker.getBool("rtc-check", false);
    if (!cableWatchdogFresh) {
      cableLastWatchdogStage = buddyCableLoadWatchdogStage(cableMarker);
    }
    cableMarker.end();
  }
  if (cableWatchdogFresh) {
    Preferences cableDiagnostic;
    if (cableDiagnostic.begin("buddy-cable", false)) {
      buddyCablePersistWatchdogStage(cableDiagnostic, cableLastWatchdogStage);
      cableDiagnostic.end();
    }
  }
  cableRestartRequested = bootResetReason == ESP_RST_SW &&
    cableRestartNotice.magic == BUDDY_CABLE_NOTICE_MAGIC &&
    cableRestartNotice.inverse == ~BUDDY_CABLE_NOTICE_MAGIC;
  cableBootFailure = (BuddyCableError)buddyCableNoticeTake(cableRestartNotice,
    bootResetReason == ESP_RST_SW, CABLE_ERROR_UART);
  if (cableBootFailure != CABLE_ERROR_NONE) cableModeError = cableBootFailure;
  else if (cableWatchdogFresh)
    cableModeError = CABLE_ERROR_WATCHDOG;
  bool wakeSoundArmed = !storageLocked && takeBuddyWakeSound(preferences);
  now = buddyNow();
  if (cableClockRecoveryPending) {
    // USB GPIO19/20 and I2C GPIO47/48 are separate. A post-reset RTC check
    // is still warranted; the source of intermittent I2C faults is unproven.
    delay(1000);
    cableClockStableReads = 0;
    for (uint8_t attempt = 0; attempt < 12 && cableClockStableReads < 3; ++attempt) {
      delay(250);
      now = buddyNow();
      if (!clockFault && now) {
        cableClockStableReads++;
        if (cableClockStableReads < 3) delay(250);
      } else cableClockStableReads = 0;
    }
    if (cableClockStableReads >= 3 && !clockFault) {
      cableClockStableSinceMs = millis();
      lastCableClockCheckMs = millis();
    } else {
      lastClockRepairMs = 0;
      lastCableClockCheckMs = millis();
      cableClockStableSinceMs = 0;
    }
  }
  if (!storageLocked && buddy.magic != BUDDY_MAGIC && !clockFault && now) {
    if (introRequired && buddyIntro.step == INTRO_COMPLETE) birthBuddyFromIntro(now);
    else if (!introRequired) createBuddyLife(now, false);
  }
  setupBuddyHouse(now);
  setupBuddyAmbition(now);
  setupBuddyLegacy(now);
  recoverBuddyCompletion();
  setupNearbyStorage();
  orientationMode = preferences.getUChar("orient", ORIENTATION_AUTO);
  if (orientationMode >= ORIENTATION_COUNT) orientationMode = ORIENTATION_AUTO;
  now = buddyNow();
  serviceBuddyRecovery();
  if (!clockRecoveryPending &&
      (lifeRollbackDetected || storageLocked || nearbyGuardJustSet ||
       bootResetReason == ESP_RST_TASK_WDT || bootResetReason == ESP_RST_WDT ||
       bootResetReason == ESP_RST_INT_WDT || bootResetReason == ESP_RST_BROWNOUT)) {
    uint8_t flags = (lifeRollbackDetected ? 4 : 0) | (storageLocked ? 64 : 0) |
      ((bootResetReason == ESP_RST_TASK_WDT || bootResetReason == ESP_RST_WDT ||
        bootResetReason == ESP_RST_INT_WDT) ? 16 : 0) |
      (bootResetReason == ESP_RST_BROWNOUT ? 32 : 0) |
      (nearbyGuardJustSet ? 128 : 0);
    recordBuddyRecovery(flags, clockFaultReason);
  }
  sampleBuddyEnvironment();
  lastCriticalCheckMs = millis();
  buddyCriticalBatteryReading(criticalBattery, latestBatteryV,
                              usbHostPowerPresent, lastCriticalCheckMs);
  updateDisplayOrientation();
  updateBuddySimulation(now);
  updateBuddyMoment(now, true);
  writeBirthRecord();
  appendDailyJournal(now);
  setupBuddyDisplay();
  serviceBuddyStorage(now);
  lastStorageRetryMs = lastPowerSampleMs; // align the ordinary minute deadlines
  externalPowerWasPresent = buddyExternalPowerPresent();
  currentView = cableModeError == CABLE_ERROR_NONE ? VIEW_BUDDY : VIEW_SETTINGS;
  lastUserMs = millis();
  lastSimulationMs = millis();
  lastEnvironmentMs = millis();
  // The farewell e-paper frame survives the software reset. Keep it through
  // normal startup, then perform a bounded post-start state/display refresh.
  if ((cableClockRecoveryPending || (bootResetReason == ESP_RST_SW && clockFault)) &&
      cableModeError == CABLE_ERROR_NONE) {
    cableReturnRepaintPending = true;
    needsRender = false;
  } else renderBuddy(true);
  if (buddy.soundEnabled && buddyWakeSoundDue(legacyTimerWake, wakeSoundArmed,
                         esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1, false)) {
    wakeAudioAttempted = true;
    wakeAudioOk = playBuddySound(SOUND_WAKE);
  }
  if (buddyUnpromptedSoundAllowed(hourlyDozeWake) && buddySpecialPending &&
      now >= buddySpecialEpoch && now - buddySpecialEpoch <= 3600UL && buddy.alive)
    playBuddySound(SOUND_SPECIAL);
  buddySpecialPending = false;
  // Cable Friends is deliberately off after every reset, even if the older
  // wireless preference was ON. Both owners must enter it manually.
  // Sound ON permits output, but boot alone does not start continuous input.
  if (!legacyTimerWake) blinkBuddyLed(1);
  awakeBootMs = millis();
  if (cableReturnRepaintPending)
    cableReturnRepaintAtMs = awakeBootMs + 3000UL;
  esp_task_wdt_config_t wdt = {};
  wdt.timeout_ms = 30000;
  wdt.idle_core_mask = 0;
  wdt.trigger_panic = true;
  watchdogReady = esp_task_wdt_reconfigure(&wdt) == ESP_OK;
  if (watchdogReady) enableLoopWDT();
  buddyButtonEvents = xQueueCreate(16, sizeof(ButtonEvent));
  if (buddyButtonEvents && xTaskCreatePinnedToCore(buddyButtonTask, "buddy-button", 2048,
                                                  nullptr, 6, nullptr, 0) != pdPASS) {
    vQueueDelete(buddyButtonEvents);
    buddyButtonEvents = nullptr;
  }
  buddyLightSleepWakeReady = gpio_wakeup_enable(BOOT_BUTTON_PIN, GPIO_INTR_LOW_LEVEL) == ESP_OK &&
    esp_sleep_enable_gpio_wakeup() == ESP_OK;
  reportBuddyStatus();
  if (!finishBuddySdUpdate() && buddyStartupHealthy()) checkBuddySdUpdate();
}

void loop() {
  // Read-only USB diagnostic; useful after a migration without touching buttons.
  if (Serial.available()) {
    int command = Serial.read();
    if (command == '?') reportBuddyStatus();
    else if (command == 'v') reportBuddyUpdateValidation();
    else if (command == 'm' || command == 'M') {
      // USB-only recovery/diagnostic path, not a spoken or on-screen command.
      // Uses the same persisted power transition as Settings and speech.
      if (!storageLocked && buddy.alive) setBuddySoundEnabled(command == 'M');
      Serial.printf("SOUND_SET requested=%u actual=%u power=%u mic=%u speech=%u\n",
        command == 'M',buddy.soundEnabled,digitalRead(AUDIO_PWR_PIN),
        buddyMicActive,buddySpeechActive && !buddySpeechPaused);
    }
    else if (command == '!' || (command >= '1' && command <= '8') ||
             command == 's' || command == 'w' || command == 'u') {
      BuddySound clip = command == 's' ? SOUND_SLEEP : command == 'w' ? SOUND_WAKE :
        command == 'u' ? SOUND_SPECIAL : SOUND_NORMAL;
      int8_t variant = command >= '1' && command <= '8' ? command - '1' : -1;
      bool ok = playBuddySound(clip, true, variant);
      Serial.printf("AUDIO_TEST %c %s error=%u detail=%02x\n",command,ok ? "OK" : "FAILED",
                    buddyAudioError,buddyAudioRegister);
    }
    else if (command == 't') {
      bool ok = playBuddyTune();
      Serial.printf("TUNE_TEST %s error=%u detail=%02x\n",ok ? "OK" : "FAILED",
                    buddyAudioError,buddyAudioRegister);
    }
  }
  // Drain gestures accumulated during a slow display refresh, then draw once.
  for (uint8_t handled = 0; handled < 16; ++handled) {
    ButtonEvent event = readBuddyButton();
    if (event == BUTTON_NONE) break;
    handleBuddyButton(event);
  }
  serviceBuddyMic();
  if (buddyVoiceUntilMs && (int32_t)(millis() - buddyVoiceUntilMs) >= 0) {
    buddyVoiceUntilMs = 0;
    updateBuddyMoment(buddyNow(), true);
    needsRender = true;
  }
  if (buddyVoiceCommandUntilMs && (int32_t)(millis() - buddyVoiceCommandUntilMs) >= 0) {
    buddyVoiceCommandUntilMs = 0;
    updateBuddyMoment(buddyNow(), true);
    needsRender = true;
  }

  // Refresh safety input before any simulation milestone can play audio.
  if (millis() - lastPowerSampleMs >= BUDDY_POWER_INTERVAL_MS ||
      (criticalBattery.lowReadings && millis() - lastCriticalCheckMs >= BUDDY_CRITICAL_CONFIRM_MS)) {
    lastCriticalCheckMs = millis();
    sampleBuddyPower();
    if (buddyCriticalBatteryReading(criticalBattery, latestBatteryV,
                                    usbHostPowerPresent, lastCriticalCheckMs))
      enterBuddySleep(false, true);
  }
  if (millis() - lastSimulationMs >= SIMULATION_INTERVAL_MS) {
    lastSimulationMs = millis();
    uint32_t now = buddyNow();
    if (buddy.magic != BUDDY_MAGIC && !storageLocked && now && !clockFault) {
      if (introRequired) birthBuddyFromIntro(now);
      else createBuddyLife(now, false);
      if (buddy.magic == BUDDY_MAGIC) {
        setupBuddyHouse(now);
        setupBuddyAmbition(now);
        setupBuddyLegacy(now);
        setupNearbyStorage();
      }
    }
    if (buddy.magic == BUDDY_MAGIC && buddyHouse.magic != BUDDY_HOUSE_MAGIC &&
        !houseLocked && now && !clockFault) setupBuddyHouse(now);
    updateBuddySimulation(now);
    serviceNearbyLife();
    if (buddySpecialPending) {
      nextMomentMs = millis(); // growth and discoveries should be visible promptly
      if (buddyUnpromptedSoundAllowed(hourlyDozeWake) && now >= buddySpecialEpoch &&
          now - buddySpecialEpoch <= 3600UL && buddy.alive)
        playBuddySound(SOUND_SPECIAL);
      buddySpecialPending = false;
    }
  }
  // Temperature has its own bounded recovery cadence, independent of life.
  uint32_t climateInterval = buddyClimateInterval(latestClimateValid, climateConsecutiveFailures);
  if (millis() - lastEnvironmentMs >= climateInterval) {
    lastEnvironmentMs = millis();
    char before[12]; snprintf(before,sizeof(before),"%s",temperatureFeeling(latestTempC));
    bool wasClimateValid = latestClimateValid;
    sampleBuddyClimate();
    bool feelingChanged = strcmp(before,temperatureFeeling(latestTempC)) != 0;
    if (currentView == VIEW_ABOUT || feelingChanged || (!wasClimateValid && latestClimateValid))
      needsRender = true;
    if (feelingChanged) nextMomentMs = millis();
  }
  // Life accounting above remains on its fixed cadence. Only the visible
  // choice and its associated occasional sound use the activity horizon.
  if (buddy.alive && !clockFault && momentScheduled &&
      (int32_t)(millis() - nextMomentMs) >= 0) {
    uint32_t now = buddyNow();
    BuddyMoment previousMoment = visibleMoment;
    updateBuddyMoment(now);
    if (momentScheduled && (int32_t)(millis() - nextMomentMs) >= 0)
      nextMomentMs = millis() + 2UL * 60UL * 1000UL; // RTC fault fallback
    needsRender = true;
    if (previousMoment != visibleMoment && buddyMomentMakesMusic(visibleMoment) &&
        buddyUnpromptedSoundAllowed(hourlyDozeWake) && buddy.soundEnabled &&
        currentView == VIEW_BUDDY) {
      lastAutonomousSoundMs = millis();
      playBuddyTune();
    } else if (buddyUnpromptedSoundAllowed(hourlyDozeWake) && buddy.soundEnabled &&
               currentView == VIEW_BUDDY && buddy.energy > 450 &&
               visibleBehavior != BEHAVIOR_SLEEP && visibleBehavior != BEHAVIOR_REST &&
               (now / 3600UL) % 24 >= 8 && (now / 3600UL) % 24 < 22 &&
               (momentHash(buddy.seed ^ (now / 600UL)) % 12 == 0) &&
               (!lastAutonomousSoundMs || millis() - lastAutonomousSoundMs >= 30UL * 60UL * 1000UL)) {
      lastAutonomousSoundMs = millis();
      playBuddySound(SOUND_NORMAL);
    }
  }
  if (millis() - lastStorageRetryMs >= 60000UL) {
    lastStorageRetryMs = millis();
    bool hadSd = sdAvailable, hadClockFault = clockFault, hadSaveFault = saveFailed;
    bool hadHouseFault = houseSaveFailed, hadIntroFault = introSaveFailed;
    retryBuddyIntroSave();
    serviceBuddyStorage(buddyNow());
    finishBuddySdUpdate(); // retry acceptance after a recoverable startup fault
    if (nearbySaveFailed) saveNearbyState();
    if (buddySoundSaveFailed) {
      buddySoundSaveFailed = !buddyPersistSoundMode(preferences, buddySoundMode);
    }
    if (cableMarkerClearFailed && !clockFault) {
      cableMarkerClearFailed = !persistCableClockRecoveryMarker(false);
    }
    if (nearbySettingSaveFailed) {
      nearbySettingSaveFailed = preferences.putUChar("near-on", nearbyEnabled ? 1 : 0) != 1;
    }
    if (hadSd != sdAvailable || hadClockFault != clockFault || hadSaveFault != saveFailed ||
        hadHouseFault != houseSaveFailed || hadIntroFault != introSaveFailed) needsRender = true;
  }
  bool rapidClockRepair = buddyCableRapidClockCheck(cableClockRecoveryPending,
    clockFault,bootResetReason == ESP_RST_SW,millis() - awakeBootMs);
  bool fastClockCheck = clockFault || rapidClockRepair;
  uint32_t &clockCheckMs = fastClockCheck ? lastCableClockCheckMs : lastClockServiceMs;
  uint32_t clockCheckInterval = buddyClockCheckIntervalMs(clockFault, rapidClockRepair);
  if (buddyCableClockRepairAllowed(cableFriendsActive()) &&
      (cableClockRecoveryPending || fastClockCheck) &&
      millis() - clockCheckMs >= clockCheckInterval) {
    clockCheckMs = millis();
    bool hadClockFault = clockFault;
    uint32_t recoveredNow = buddyNow();
    if (!clockFault && recoveredNow) {
      if (!cableClockStableSinceMs) cableClockStableSinceMs = millis();
      if (cableClockStableReads < 3) ++cableClockStableReads;
    } else {
      cableClockStableReads = 0;
      cableClockStableSinceMs = 0;
    }
    // Keep monitoring for a full settling window: a quick healthy read can
    // precede an RTC fault once ordinary display/sensor services resume.
    if (cableClockRecoveryPending && cableClockStableReads >= 3 &&
        cableClockStableSinceMs && millis() - cableClockStableSinceMs >= 30000UL) {
      cableMarkerClearFailed = !persistCableClockRecoveryMarker(false);
      cableClockRecoveryPending = false;
      cableClockStableReads = 0;
      cableClockStableSinceMs = 0;
      needsRender = true;
    }
    if (hadClockFault && !clockFault) {
      sampleBuddyClimate(); // restore the temperature alongside the clock
      lastEnvironmentMs = millis();
      cableReturnActivityOverride = false;
      cleanPartialRequested = true;
      needsRender = true;
    } else if (hadClockFault != clockFault) needsRender = true;
  }
  serviceNearbyRadio();
  serviceBuddyRecovery();
  if (!momentScheduled && buddy.alive && !clockFault) {
    updateBuddyMoment(buddyNow(), true); // first valid clock/life after recovery
    needsRender = true;
  }
  if (currentView != VIEW_BUDDY && buddyMenuReturnDue(millis(), viewOpenedMs, buttonDown)) {
    currentView = VIEW_BUDDY;
    cleanPartialRequested = true;
    needsRender = true;
  }
  if (cableReturnRepaintPending && !cableFriendsActive() &&
      (int32_t)(millis() - cableReturnRepaintAtMs) >= 0) {
    bool firstCheck = !cableReturnFaultShown;
    uint32_t checkedNow = refreshBuddyAfterCableReturn();
    bool healthy = checkedNow && !clockFault;
    // Always give Buddy a fresh visible activity at the first deadline.
    // If RTC communication is still faulted, choose from the last saved
    // epoch without advancing life or age, and keep the stale pause footer
    // from obscuring the activity briefly while clock recovery continues.
    if (firstCheck) {
      uint32_t activityNow = checkedNow ? checkedNow : buddy.lastUpdateEpoch;
      if (!activityNow) activityNow = buddy.birthEpoch;
      if (clockFault && buddy.alive && activityNow && !storageLocked)
        updateBuddyMoment(activityNow, true, true);
      cableReturnActivityOverride = !healthy;
      cableReturnOverrideUntilMs = millis() + BUDDY_CABLE_DISPLAY_GRACE_MS;
    } else if (healthy) cableReturnActivityOverride = false;
    else if (cableReturnActivityOverride &&
             !buddyCableDisplayGraceActive(millis(), cableReturnOverrideUntilMs)) {
      cableReturnActivityOverride = false;
      needsRender = true;
    }
    // End the farewell at the first deadline even if the user opened a menu;
    // the selected activity appears when home is shown again.
    if (currentView == VIEW_BUDDY &&
        buddyCableReturnShouldRender(!cableReturnFaultShown, healthy)) renderBuddy(true);
    cableReturnFaultShown = true;
    if (healthy && !clockFault) cableReturnRepaintPending = false;
    else if (!cableReturnActivityOverride) cableReturnRepaintPending = false;
    else cableReturnRepaintAtMs = millis() + 2000UL;
  }
  if (needsRender) {
    if (buddyCableDeferFarewellRepaint(cableReturnRepaintPending,
                                      cableReturnFaultShown, cableFriendsActive()) &&
        currentView == VIEW_BUDDY) needsRender = false; // keep farewell visible
    else renderBuddy(false);
  }
  if (buddyInteractionSoundPending && !needsRender &&
      !cableFriendsActive() && buddy.soundEnabled) {
    buddyInteractionSoundPending = false;
    playBuddySound(SOUND_NORMAL);
  } else if (!buddy.soundEnabled || cableFriendsActive()) buddyInteractionSoundPending = false;
  bool powered = buddyExternalPowerPresent();
  if (updateDisplayOrientation()) needsRender = true;
  if (powered != externalPowerWasPresent) {
    lastUserMs = millis();
    externalPowerWasPresent = powered;
    needsRender = true;
  }
  serviceBuddyListeningWindow();
  // Sleep only to the earliest pending service. GPIO0 can wake before it;
  // audio, cable, USB host, queued gestures and rendering keep the loop awake.
  if (buddyLightSleepWakeReady &&
      buddyLightSleepRetry.allowed(millis()) &&
      buddyCanLightSleep(false, // active input channels below are authoritative
                         buddyMicActive,
                         buddyTxChannel != nullptr || buddyRxChannel != nullptr,
                         cableFriendsActive(), usbHostPowerPresent, buttonDown,
                         buddyButtonEvents && uxQueueMessagesWaiting(buddyButtonEvents), needsRender) &&
      digitalRead(BOOT_BUTTON_PIN) != LOW &&
      millis() - viewOpenedMs >= 1000UL) {
    uint32_t before = millis();
    uint32_t untilWork = buddyNextIdleWorkMs(before);
    if (untilWork) {
      bool slept = false;
      // The loop task cannot be watched as if running during a planned sleep
      // longer than its 30-second watchdog period. Re-arm immediately on wake.
      if (watchdogReady) disableLoopWDT();
      buddyLastSleepRequestedMs = untilWork;
      if (esp_sleep_enable_timer_wakeup((uint64_t)untilWork * 1000ULL) == ESP_OK) {
        esp_err_t result = esp_light_sleep_start();
        slept = result == ESP_OK;
        buddyLastSleepElapsedMs = millis() - before;
        buddyLastLightWake = result == ESP_OK ? (uint8_t)esp_sleep_get_wakeup_cause() : 0;
        if (slept) ++buddyLightSleepCount;
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
      }
      if (watchdogReady) enableLoopWDT();
      buddyLightSleepRetry.finish(slept,millis());
      if (!slept) {
        delay(20); // rejected sleep must not become an unbounded tight loop
      }
    } else delay(1); // already-due work is handled on the next pass
  } else delay(20);
}
