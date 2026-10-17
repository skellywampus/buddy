// Two offline commands, recognized as whole phrases. "Buddy" is part of each
// command, not a separately trained wake word. Nothing is recorded or sent.
static const sr_cmd_t buddySrCommands[] = {
  {VOICE_SOUND_OFF, "Buddy sound off"},
  {VOICE_GO_TO_SLEEP, "Buddy go to sleep"},
};
// Generated on this ESP-SR version using flite_g2p(phrase, 1).
static const char *const buddySrPhonemes[] = {
  "BcDm StND eF", "BcDm Gb To SLmP"
};
static_assert(sizeof(buddySrCommands) / sizeof(buddySrCommands[0]) ==
              sizeof(buddySrPhonemes) / sizeof(buddySrPhonemes[0]), "speech phrase mismatch");

static const uint32_t BUDDY_SR_MODEL_BYTES = 3340296UL;
// SHA-256 of esp32s3-libs 3.3.12 / esp_sr / srmodels.bin. The installed
// model must match the linked MultiNet7/WakeNet9/AFE library configuration.
static const uint8_t BUDDY_SR_MODEL_SHA[32] = {
  0xc7,0xea,0x01,0x95,0x4b,0xaa,0xa1,0xfa,0x89,0xef,0x6b,0x88,0xba,0x88,0x19,0xed,
  0xcb,0xf4,0x19,0xb7,0xab,0xa1,0xe0,0x93,0x95,0x0f,0x8b,0xad,0xf1,0xa6,0xc3,0x28
};

enum BuddySpeechSignalType : uint8_t { SPEECH_HEARD = 1, SPEECH_COMMAND = 2 };
struct BuddySpeechSignal { uint8_t type, command; };
static QueueHandle_t buddySpeechQueue = nullptr;
static const char *buddySpeechInputFormat = "MN";

static bool buddyObserveVoice(uint16_t level, uint16_t duration, uint32_t now) {
  portENTER_CRITICAL(&buddyVoiceLock);
  if (level > buddyMicPeak) buddyMicPeak = level;
  if (level > buddyVoiceDetector.threshold()) buddyMicAboveFrames++;
  bool heard = buddyVoiceDetector.feed(level, duration, now);
  portEXIT_CRITICAL(&buddyVoiceLock);
  return heard;
}
static bool buddyVoicePending() {
  portENTER_CRITICAL(&buddyVoiceLock);
  bool pending = buddyVoiceDetector.voicedMs != 0;
  portEXIT_CRITICAL(&buddyVoiceLock);
  return pending;
}

static bool buddySpeechModelValid() {
  if (buddySpeechModelStatus >= 0) return buddySpeechModelStatus == 1;
  const esp_partition_t *model = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "model");
  if (!model || model->size < BUDDY_SR_MODEL_BYTES) {
    buddySpeechError = 1; buddySpeechModelStatus = 0; return false;
  }
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  int error = mbedtls_sha256_starts(&hash, 0);
  uint8_t block[2048];
  for (uint32_t offset = 0; !error && offset < BUDDY_SR_MODEL_BYTES; offset += sizeof(block)) {
    uint32_t amount = min((uint32_t)sizeof(block), BUDDY_SR_MODEL_BYTES - offset);
    if (esp_partition_read(model, offset, block, amount) != ESP_OK) { error = -1; break; }
    error = mbedtls_sha256_update(&hash, block, amount);
    delay(0);
  }
  uint8_t digest[32] = {};
  if (!error) error = mbedtls_sha256_finish(&hash, digest);
  mbedtls_sha256_free(&hash);
  buddySpeechModelStatus = !error && !memcmp(digest, BUDDY_SR_MODEL_SHA, sizeof(digest));
  if (!buddySpeechModelStatus) buddySpeechError = 2;
  return buddySpeechModelStatus == 1;
}

static esp_err_t buddySpeechFill(void *, void *out, size_t length,
                                  size_t *bytesRead, uint32_t) {
  if (!buddyRxChannel || !out || !bytesRead) return ESP_FAIL;
  uint8_t *destination = (uint8_t *)out;
  size_t total = 0;
  uint32_t started = millis();
  while (total < length && millis() - started < 100UL) {
    size_t received = 0;
    esp_err_t error = i2s_channel_read(buddyRxChannel, destination + total,
                                       length - total, &received, pdMS_TO_TICKS(100));
    if (error != ESP_OK && error != ESP_ERR_TIMEOUT) return error;
    total += received;
    if (!received) break;
  }
  *bytesRead = total;
  if (total < length) memset(destination + total, 0, length - total);
  if (total >= 4 && (int32_t)(millis() - buddyMicReadyMs) >= 0) {
    uint16_t level = buddyVoiceLevel((const int16_t *)out, total / 4);
    if (buddyObserveVoice(level, (uint16_t)(total / 64), millis()) && buddySpeechQueue) {
      BuddySpeechSignal signal = {SPEECH_HEARD, 0};
      xQueueSend(buddySpeechQueue, &signal, 0);
    }
  }
  return ESP_OK;
}

static void buddySpeechEvent(void *, sr_event_t event, int commandId, int) {
  if (event == SR_EVENT_COMMAND &&
      (commandId == VOICE_SOUND_OFF || commandId == VOICE_GO_TO_SLEEP) && buddySpeechQueue) {
    BuddySpeechSignal signal = {SPEECH_COMMAND, (uint8_t)commandId};
    xQueueSend(buddySpeechQueue, &signal, 0);
  }
  if (event == SR_EVENT_COMMAND || event == SR_EVENT_TIMEOUT)
    sr_set_mode(SR_MODE_COMMAND); // Continuous recognition of full phrases.
}

static void chooseBuddySpeechChannel() {
  // The ES8311 outputs one effective microphone channel on a stereo I2S
  // stream. Detect which slot is populated, but keep left on ambiguous silence.
  uint32_t left = 0, right = 0;
  int16_t samples[256];
  // The codec initially emits zero frames after its rail rises. Observe a
  // longer warm-up window so a quiet room does not force the wrong channel.
  for (uint8_t block = 0; block < 48; block++) {
    size_t bytes = 0;
    esp_err_t readError = i2s_channel_read(buddyRxChannel, samples, sizeof(samples), &bytes,
                                           pdMS_TO_TICKS(50));
    if ((readError != ESP_OK && readError != ESP_ERR_TIMEOUT) || bytes < 4) continue;
    for (size_t i = 0; i < bytes / 4; i++) {
      left += abs((int)samples[2*i]);
      right += abs((int)samples[2*i+1]);
    }
  }
  buddySpeechInputFormat = right > left * 2 && right > 4000 ? "NM" : "MN";
  Serial.printf("SPEECH input=%s left=%lu right=%lu\n", buddySpeechInputFormat,
                (unsigned long)left, (unsigned long)right);
}

bool startBuddySpeech() {
  if (buddySpeechActive) {
    if (!buddySpeechPaused) return true;
    if (buddySpeechQueue) xQueueReset(buddySpeechQueue);
    buddyPendingHeardMs = 0;
    if (sr_resume() != ESP_OK) {
      buddySpeechError = 5;
      stopBuddySpeech();
      return false;
    }
    buddySpeechPaused = false;
    sr_set_mode(SR_MODE_COMMAND);
    return true;
  }
  if (!buddySpeechModelValid()) return false;
  if (!buddySpeechQueue) buddySpeechQueue = xQueueCreate(8, sizeof(BuddySpeechSignal));
  if (!buddySpeechQueue) { buddySpeechError = 3; return false; }
  xQueueReset(buddySpeechQueue);
  chooseBuddySpeechChannel();
  esp_err_t error = sr_start(buddySpeechFill, nullptr, SR_CHANNELS_STEREO,
                             SR_MODE_COMMAND, buddySpeechInputFormat,
                             buddySrCommands, buddySrPhonemes,
                             sizeof(buddySrCommands) / sizeof(buddySrCommands[0]),
                             buddySpeechEvent, nullptr);
  buddySpeechActive = error == ESP_OK;
  buddySpeechPaused = false;
  buddySpeechError = buddySpeechActive ? 0 : 4;
  Serial.printf("SPEECH start=%u error=%d\n", buddySpeechActive, (int)error);
  return buddySpeechActive;
}

void pauseBuddySpeech() {
  if (!buddySpeechActive || buddySpeechPaused) return;
  if (sr_pause() != ESP_OK) {
    buddySpeechError = 6;
    stopBuddySpeech(); // bounded stop joins/terminates workers before I2S removal
    return;
  }
  buddySpeechPaused = true;
  if (buddySpeechQueue) xQueueReset(buddySpeechQueue);
  buddyPendingHeardMs = 0;
}

void stopBuddySpeech() {
  if (!buddySpeechActive) return;
  buddySpeechActive = false;
  sr_stop();
  buddySpeechPaused = false;
  // Buddy's sr_stop owns registry/model cleanup as well as worker teardown,
  // including partial startup failure. Do not free those allocations twice.
  if (buddySpeechQueue) xQueueReset(buddySpeechQueue);
  buddyPendingHeardMs = 0;
}

void serviceBuddySpeech() {
  if (!buddySpeechActive || buddySpeechPaused || !buddySpeechQueue) return;
  BuddySpeechSignal signal;
  while (xQueueReceive(buddySpeechQueue, &signal, 0) == pdTRUE) {
    if (signal.type == SPEECH_COMMAND) {
      buddyPendingHeardMs = 0;
      handleBuddyVoiceCommand((BuddyVoiceCommand)signal.command);
      if (!buddySpeechActive) return; // Sleep or audio playback stopped it.
    } else if (signal.type == SPEECH_HEARD) {
      // Let command recognition finish before granting generic voice attention.
      buddyPendingHeardMs = millis() + 1500UL;
    }
  }
  if (buddyPendingHeardMs && (int32_t)(millis() - buddyPendingHeardMs) >= 0) {
    buddyPendingHeardMs = 0;
    handleBuddyHeard();
  }
}
