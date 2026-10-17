// Input is a temporary BOOT/command listening window. Speaker playback has
// its own on-demand lifecycle; neither path records PCM or writes it to SD.
void touchBuddyListeningWindow() {
  if (!buddy.soundEnabled || buddySoundMode == SOUND_OFF) return;
  if (!buddyListeningWindow.open) buddyMicRetryMs = millis() - 30000UL;
  buddyListeningTouch(buddyListeningWindow, millis());
}

bool startBuddyMic() {
  if (buddySoundMode == SOUND_OFF || !buddyAudioPowerAllowed(latestBatteryV,usbHostPowerPresent) ||
      !buddyListeningWanted(buddyListeningWindow, buddy.soundEnabled) ||
      nearbyRadioActive || cableFriendsActive()) return false;
  if (buddyMicActive || buddyTxChannel) return buddyMicActive;
  uint32_t startMs = millis();
  buddyMicError = 0;
  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  if (i2s_new_channel(&channel, nullptr, &buddyRxChannel) != ESP_OK) {
    buddyRxChannel = nullptr; buddyMicError = 1; return false;
  }
  i2s_std_config_t config = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = AUDIO_MCLK_PIN, .bclk = AUDIO_BCLK_PIN, .ws = AUDIO_WS_PIN,
      .dout = I2S_GPIO_UNUSED, .din = AUDIO_DIN_PIN,
      .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
    },
  };
  config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  if (i2s_channel_init_std_mode(buddyRxChannel, &config) != ESP_OK ||
      i2s_channel_enable(buddyRxChannel) != ESP_OK) {
    buddyMicError = 2; stopBuddyMic(); return false;
  }
  digitalWrite(AUDIO_PWR_PIN, LOW);
  digitalWrite(AUDIO_PA_PIN, LOW);
  delay(60);
  // Capture sequence from the V2 board's ES8311 path, with speaker amp off.
  const uint8_t regs[][2] = {
    {0x44,0x08},{0x44,0x08},{0x01,0x30},{0x02,0x00},{0x03,0x10},{0x16,0x24},
    {0x04,0x10},{0x05,0x00},{0x0B,0x00},{0x0C,0x00},{0x10,0x1F},{0x11,0x7F},
    {0x00,0x80},{0x01,0x3F},{0x13,0x10},{0x1B,0x0A},{0x1C,0x6A},{0x44,0x58},
    {0x09,0x4C},{0x0A,0x0C},{0x02,0x00},{0x05,0x00},{0x03,0x10},{0x04,0x20},
    {0x07,0x00},{0x08,0xFF},{0x06,0x03},{0x00,0x80},{0x01,0x3F},{0x17,0xBF},
    {0x0E,0x02},{0x12,0x00},{0x14,0x1A},{0x0D,0x01},{0x15,0x40},{0x16,0x07},
    {0x37,0x08},{0x45,0x00}
  };
  for (const auto &reg : regs) {
    if (!i2cWriteRegister(I2C_ES8311_ADDR, reg[0], reg[1])) {
      buddyMicError = 3; stopBuddyMic(); return false;
    }
  }
  buddyVoiceDetector.reset();
  buddyMicReadyMs = millis() + 1000UL;
  buddyMicActive = true;
  startBuddySpeech(); // Resume resident ESP-SR; presence-only fallback on failure.
  buddyMicStartupMs = millis() - startMs;
  buddyMicStarts++;
  Serial.printf("MIC window start_ms=%lu speech=%u free_heap=%lu\n",
    (unsigned long)buddyMicStartupMs, buddySpeechActive,
    (unsigned long)ESP.getFreeHeap());
  return true;
}

void stopBuddyMic() {
  bool hadInput = buddyMicActive || buddyRxChannel || buddySpeechActive;
  pauseBuddySpeech();
  buddyMicActive = false;
  if (buddyRxChannel) {
    i2s_channel_disable(buddyRxChannel);
    i2s_del_channel(buddyRxChannel);
    buddyRxChannel = nullptr;
  }
  digitalWrite(AUDIO_PA_PIN, LOW);
  // AUDIO_PWR is active-low and supplies the ES8311/microphone rail. Hold it
  // off even when there was no active I2S channel to tear down.
  digitalWrite(AUDIO_PWR_PIN, HIGH);
  if (hadInput) buddyMicStops++;
}

#include "buddy_sound_settings.inc"

void handleBuddyHeard() {
  uint32_t now = buddyNow();
  if (!now || clockFault || !buddy.alive) return;
  lastUserMs = millis();
  hourlyDozeWake = false;
  if (!buddyVoiceRewarded || millis() - buddyVoiceRewardMs >= 5UL * 60UL * 1000UL) {
    interactWithBuddy(now);
    buddyVoiceRewardMs = millis();
    buddyVoiceRewarded = true;
  }
  buddyMicHeardCount++;
  buddyVoiceUntilMs = millis() + 12000UL;
  visibleMoment = MOMENT_LISTEN;
  visibleBehavior = BEHAVIOR_OBSERVE;
  currentView = VIEW_BUDDY;
  cleanPartialRequested = true;
  needsRender = true;
  buddyInteractionSoundPending = buddy.soundEnabled;
}

void handleBuddyVoiceCommand(BuddyVoiceCommand command) {
  if (storageLocked || !buddy.alive || clockFault) return;
  BuddyVoiceAction action = buddyVoiceAction(command);
  if (!action.changeSound && !action.manualSleep) return;
  touchBuddyListeningWindow(); // a valid recognized command renews 60 seconds
  lastUserMs = millis();
  hourlyDozeWake = false;
  buddyVoiceUntilMs = 0;
  buddyVoiceCommandFailed = false;
  if (action.changeSound) buddyVoiceCommandFailed = !setBuddySoundEnabled(action.soundOn);
  buddyLastVoiceCommand = command;
  buddyVoiceCommandUntilMs = millis() + 12000UL;
  currentView = VIEW_BUDDY;
  cleanPartialRequested = true;
  needsRender = true;
  Serial.printf("SPEECH command=%u ok=%u\n", (unsigned)command, !buddyVoiceCommandFailed);
  if (action.manualSleep) enterBuddySleep(true);
}

void serviceBuddyListeningWindow() {
  if (buddySoundMode == SOUND_OFF || !buddy.soundEnabled || storageLocked || !buddy.alive ||
      !buddyAudioPowerAllowed(latestBatteryV,usbHostPowerPresent)) {
    buddyListeningClose(buddyListeningWindow);
    if (buddyMicActive || buddyRxChannel || (buddySpeechActive && !buddySpeechPaused)) stopBuddyMic();
    return;
  }
  if (buddyListeningDue(buddyListeningWindow, millis())) {
    // Complete a held gesture or already-detected voice/command before timing
    // out. The speech queue is consumed by serviceBuddyMic on the next pass.
    bool pendingButton = buttonDown ||
      (buddyButtonEvents && uxQueueMessagesWaiting(buddyButtonEvents));
    bool pendingSpeech = buddyPendingHeardMs || buddyVoicePending() ||
      (buddySpeechQueue && uxQueueMessagesWaiting(buddySpeechQueue));
    if (buddyListeningGraceExpired(buddyListeningWindow, millis()) ||
        (!pendingButton && !pendingSpeech)) {
      buddyListeningClose(buddyListeningWindow);
      stopBuddyMic();
    }
  }
  if (!buddyListeningWanted(buddyListeningWindow, buddy.soundEnabled) ||
      nearbyRadioActive || cableFriendsActive() || buddyTxChannel) return;
  if (!buddyMicActive && millis() - buddyMicRetryMs >= 30000UL) {
    buddyMicRetryMs = millis();
    startBuddyMic();
  }
}

void serviceBuddyMic() {
  if (buddySoundMode == SOUND_OFF || !buddy.soundEnabled ||
      !buddyAudioPowerAllowed(latestBatteryV,usbHostPowerPresent)) {
    if (buddyMicActive || buddyRxChannel || (buddySpeechActive && !buddySpeechPaused)) stopBuddyMic();
    return;
  }
  if (!buddyMicActive || !buddyRxChannel || storageLocked || !buddy.alive) return;
  if (buddySpeechActive) { serviceBuddySpeech(); return; }
  int16_t samples[256];
  for (uint8_t i = 0; i < 4; i++) {
    size_t bytes = 0;
    esp_err_t result = i2s_channel_read(buddyRxChannel, samples, sizeof(samples), &bytes,
                                        pdMS_TO_TICKS(1));
    if (result != ESP_OK && result != ESP_ERR_TIMEOUT) {
      buddyMicError = 4; stopBuddyMic(); return;
    }
    if (bytes < 4) return;
    uint16_t level = buddyVoiceLevel(samples, bytes / 4);
    if ((int32_t)(millis() - buddyMicReadyMs) < 0) continue;
    if (!buddyObserveVoice(level, (uint16_t)(bytes / 64), millis())) continue;
    handleBuddyHeard();
    return;
  }
}
