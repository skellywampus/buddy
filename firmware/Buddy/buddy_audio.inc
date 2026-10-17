static bool setupBuddyAudio() {
  buddyAudioError = 0;
  buddyAudioRegister = 0;
  i2s_chan_config_t channelConfig = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  channelConfig.auto_clear = true;
  if (i2s_new_channel(&channelConfig, &buddyTxChannel, nullptr) != ESP_OK) {
    buddyTxChannel = nullptr;
    buddyAudioError = 1;
    return false;
  }
  i2s_std_config_t config = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = AUDIO_MCLK_PIN, .bclk = AUDIO_BCLK_PIN, .ws = AUDIO_WS_PIN,
      .dout = AUDIO_DOUT_PIN, .din = I2S_GPIO_UNUSED,
      .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
    },
  };
  config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  if (i2s_channel_init_std_mode(buddyTxChannel, &config) != ESP_OK ||
      i2s_channel_enable(buddyTxChannel) != ESP_OK) {
    i2s_del_channel(buddyTxChannel);
    buddyTxChannel = nullptr;
    buddyAudioError = 2;
    return false;
  }

  digitalWrite(AUDIO_PWR_PIN, LOW);
  digitalWrite(AUDIO_PA_PIN, LOW);
  delay(60);
  const uint8_t registers[][2] = {
    {0x44,0x08},{0x44,0x08},{0x01,0x30},{0x02,0x00},{0x03,0x10},{0x16,0x24},{0x04,0x10},
    {0x05,0x00},{0x0B,0x00},{0x0C,0x00},{0x10,0x1F},{0x11,0x7F},{0x00,0x80},
    {0x01,0x3F},{0x13,0x10},{0x1B,0x0A},{0x1C,0x6A},{0x44,0x58},
    {0x09,0x0C},{0x0A,0x4C},{0x02,0x00},{0x05,0x00},{0x03,0x10},{0x04,0x20},
    {0x07,0x00},{0x08,0xFF},{0x06,0x03},{0x00,0x80},{0x01,0x3F},{0x17,0xBF},
    {0x0E,0x02},{0x12,0x00},{0x14,0x1A},{0x0D,0x01},{0x15,0x40},{0x16,0x07},
    {0x37,0x08},{0x45,0x00},{0x32,0xBF}
  };
  for (const auto &item : registers) {
    if (!i2cWriteRegister(I2C_ES8311_ADDR, item[0], item[1])) {
      buddyAudioError = 3;
      buddyAudioRegister = item[0];
      return false;
    }
  }
  uint8_t volume = 0;
  if (!i2cReadRegister(I2C_ES8311_ADDR, 0x32, &volume, 1) || volume != 0xBF) {
    buddyAudioError = 4;
    buddyAudioRegister = volume;
    return false;
  }
  digitalWrite(AUDIO_PA_PIN, HIGH);
  delay(30);
  return true;
}

static void stopBuddyAudio() {
  digitalWrite(AUDIO_PA_PIN, LOW);
  if (buddyTxChannel) {
    i2s_channel_disable(buddyTxChannel);
    i2s_del_channel(buddyTxChannel);
    buddyTxChannel = nullptr;
  }
  digitalWrite(AUDIO_PWR_PIN, HIGH);
}

#include "sounds_generated.h"
#include "buddy_adpcm.h"

struct BuddyClipData { const uint8_t *data; int16_t first; uint32_t frames; };

static BuddyClipData buddyClipByIndex(uint8_t index) {
  static const BuddyClipData clips[BUDDY_SOUND_CLIPS] = {
    {buddy_adpcm_new_sound_1, buddy_adpcm_new_sound_1_first, buddy_adpcm_new_sound_1_frames},
    {buddy_adpcm_new_sound_2, buddy_adpcm_new_sound_2_first, buddy_adpcm_new_sound_2_frames},
    {buddy_adpcm_new_sound_3, buddy_adpcm_new_sound_3_first, buddy_adpcm_new_sound_3_frames},
    {buddy_adpcm_new_sound_4, buddy_adpcm_new_sound_4_first, buddy_adpcm_new_sound_4_frames},
    {buddy_adpcm_new_sound_5, buddy_adpcm_new_sound_5_first, buddy_adpcm_new_sound_5_frames},
    {buddy_adpcm_new_sound_6, buddy_adpcm_new_sound_6_first, buddy_adpcm_new_sound_6_frames},
    {buddy_adpcm_new_sound_7, buddy_adpcm_new_sound_7_first, buddy_adpcm_new_sound_7_frames},
    {buddy_adpcm_new_sound_8, buddy_adpcm_new_sound_8_first, buddy_adpcm_new_sound_8_frames}
  };
  return clips[index < BUDDY_SOUND_CLIPS ? index : 0];
}

struct BuddyTuneStream {
  BuddyClipData clip;
  BuddyAdpcmDecoder decoder;
  uint32_t frame = 0;
  explicit BuddyTuneStream(BuddyClipData source) : clip(source), decoder(source.first) {}
  int16_t next() {
    if (!frame++) return buddySoundSample(clip.first, buddySoundMode);
    uint32_t codeIndex = frame - 2;
    uint8_t packed = clip.data[codeIndex / 2];
    return buddySoundSample(decoder.next((codeIndex & 1) ? packed >> 4 : packed & 15),
                            buddySoundMode);
  }
};

static bool outputBuddyTune(BuddyTunePlan plan) {
  BuddyTuneStream streams[BUDDY_TUNE_CLIPS] = {
    BuddyTuneStream(buddyClipByIndex(plan.clip[0])),
    BuddyTuneStream(buddyClipByIndex(plan.clip[1])),
    BuddyTuneStream(buddyClipByIndex(plan.clip[2])),
    BuddyTuneStream(buddyClipByIndex(plan.clip[3])),
    BuddyTuneStream(buddyClipByIndex(plan.clip[4]))
  };
  uint32_t begins[BUDDY_TUNE_CLIPS] = {};
  for (uint8_t i = 1; i < BUDDY_TUNE_CLIPS; ++i)
    begins[i] = buddyTuneNextStart(begins[i - 1], streams[i - 1].clip.frames);
  uint32_t end = begins[BUDDY_TUNE_CLIPS - 1] + streams[BUDDY_TUNE_CLIPS - 1].clip.frames;
  int16_t samples[256];
  for (uint32_t frame = 0; frame < end; frame += 128) {
    uint16_t count = min((uint32_t)128, end - frame);
    for (uint16_t i = 0; i < count; ++i) {
      uint32_t time = frame + i;
      int64_t weighted = 0;
      uint32_t totalWeight = 0;
      for (uint8_t part = 0; part < BUDDY_TUNE_CLIPS; ++part) {
        uint32_t start = begins[part];
        if (time < start || time - start >= streams[part].clip.frames) continue;
        uint32_t local = time - start;
        uint32_t weight = buddyTuneWeight(local, streams[part].clip.frames,
                                           part == 0, part == BUDDY_TUNE_CLIPS - 1);
        weighted += (int64_t)streams[part].next() * weight;
        totalWeight += weight;
      }
      int16_t mixed = buddySoundMix(weighted, totalWeight);
      samples[i * 2] = mixed;
      samples[i * 2 + 1] = mixed;
    }
    size_t written = 0;
    if (i2s_channel_write(buddyTxChannel, samples, count * 4, &written,
                          pdMS_TO_TICKS(250)) != ESP_OK || written != count * 4) {
      buddyAudioError = 5;
      return false;
    }
  }
  return true;
}

bool playBuddyTune() {
  if (buddySoundMode == SOUND_OFF || !buddy.soundEnabled || !buddy.alive || hourlyDozeWake ||
      !buddyAudioPowerAllowed(latestBatteryV,usbHostPowerPresent)) return false;
  bool resumeMic = buddyMicActive;
  if (resumeMic) stopBuddyMic();
  bool ready = setupBuddyAudio();
  if (!ready) { stopBuddyAudio(); delay(30); ready = setupBuddyAudio(); }
  if (!ready) { stopBuddyAudio(); if (resumeMic) startBuddyMic(); return false; }
  BuddyTunePlan plan = buddyTunePlan(esp_random());
  bool ok = outputBuddyTune(plan);
  if (ok) delay(45);
  stopBuddyAudio();
  if (resumeMic) startBuddyMic();
  return ok;
}

static bool outputBuddyClip(const uint8_t *clip, int16_t first, uint32_t frames) {
  BuddyAdpcmDecoder decoder(first);
  int16_t samples[256];
  for (uint32_t frame = 0; frame < frames; frame += 128) {
    uint16_t count = min((uint32_t)128, frames - frame);
    for (uint16_t i = 0; i < count; i++) {
      // LOW retains the prior -6 dB digital attenuation; HIGH uses the
      // original signed PCM at the same verified codec register setting.
      uint32_t offset = frame + i;
      uint32_t codeIndex = offset - 1;
      uint8_t packed = offset ? clip[codeIndex / 2] : 0;
      int16_t decoded = offset ? decoder.next((codeIndex & 1) ? packed >> 4 : packed & 15) : first;
      int16_t sample = buddySoundSample(decoded, buddySoundMode);
      samples[i * 2] = sample;
      samples[i * 2 + 1] = sample;
    }
    size_t written = 0;
    if (i2s_channel_write(buddyTxChannel, samples, count * 4, &written,
                          pdMS_TO_TICKS(250)) != ESP_OK || written != count * 4) {
      buddyAudioError = 5;
      return false;
    }
  }
  return true;
}

bool playBuddySound(BuddySound sound, bool diagnostic, int8_t fixedVariant) {
  // Even the USB speaker self-test must not energize the shared audio/mic
  // rail while Sound is OFF. The Settings toggle is the only way back on.
  if (buddySoundMode == SOUND_OFF || !buddy.soundEnabled ||
      !buddyAudioPowerAllowed(latestBatteryV,usbHostPowerPresent)) return false;
  bool preemptedNearby = nearbyRadioActive;
  if (preemptedNearby) stopNearbyRadio();
  // The event still determines *when* to sound; every audible event now
  // chooses independently from the same eight clips. A fixed index is only
  // used by the USB self-test so every asset can be checked deliberately.
  (void)diagnostic;
  uint8_t selection = fixedVariant >= 0 && fixedVariant < BUDDY_SOUND_CLIPS ?
    buddySoundClipIndex(sound, 0, fixedVariant) :
    buddySoundClipIndex(sound, esp_random());
  BuddyClipData chosen = buddyClipByIndex(selection);
  bool resumeMic = buddyMicActive;
  if (resumeMic) stopBuddyMic();
  // The codec can need a clean rail cycle after a deep-sleep wake or an I2C
  // interruption. Retry once; do not repeat a clip after playback starts.
  bool ready = setupBuddyAudio();
  if (!ready) {
    stopBuddyAudio();
    delay(30);
    ready = setupBuddyAudio();
  }
  if (!ready) {
    stopBuddyAudio();
    if (resumeMic) startBuddyMic();
    return false;
  }
  bool ok = outputBuddyClip(chosen.data, chosen.first, chosen.frames);
  // Let the final DMA frames leave the codec before turning its rail off.
  if (ok) delay(45);
  stopBuddyAudio();
  if (resumeMic) startBuddyMic();
  return ok;
}
