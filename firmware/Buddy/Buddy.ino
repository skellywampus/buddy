/**
 * Buddy: a standalone artificial-life companion for the
 * Waveshare ESP32-S3-ePaper-1.54 V2.
 *
 * Buddy lives autonomously for one real year, develops individual traits and
 * learned preferences, responds to ambient temperature, archives lives to an
 * optional microSD card, and uses one button for all optional interaction.
 */
#include <Arduino.h>
#define BUDDY_WIRELESS_LINK_ENABLED 0
#include <Preferences.h>
#include <SD_MMC.h>
#include <Update.h>
#include "esp32-hal-sr.h"
#include "model_path.h"
#include "esp_mn_speech_commands.h"
#include <Wire.h>
#include <math.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_rom_gpio.h"
#include "soc/uart_periph.h"
#include "driver/rtc_io.h"
#include "driver/i2s_std.h"
#include "driver/usb_serial_jtag.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "mbedtls/sha256.h"

#include "../../third_party/waveshare_epaper_display/epaper_driver_bsp.h"

#define EPD_SPI_NUM SPI2_HOST
#define EPD_WIDTH 200
#define EPD_HEIGHT 200
#define EPD_DC_PIN GPIO_NUM_10
#define EPD_CS_PIN GPIO_NUM_11
#define EPD_SCK_PIN GPIO_NUM_12
#define EPD_MOSI_PIN GPIO_NUM_13
#define EPD_RST_PIN GPIO_NUM_9
#define EPD_BUSY_PIN GPIO_NUM_8
#define EPD_TP_INT_PIN GPIO_NUM_21
#define EPD_TP_RST_PIN GPIO_NUM_7
#define EPD_PWR_PIN GPIO_NUM_6
#define AUDIO_PWR_PIN GPIO_NUM_42
#define VBAT_PWR_PIN GPIO_NUM_17 // BAT_Control: whole-board battery latch, NOT ADC enable
#define BOOT_BUTTON_PIN GPIO_NUM_0
#define PWR_BUTTON_PIN GPIO_NUM_18
#define STATUS_LED_PIN GPIO_NUM_3
#define BAT_ADC_PIN 4
#define AUDIO_PA_PIN GPIO_NUM_46
#define AUDIO_MCLK_PIN GPIO_NUM_14
#define AUDIO_BCLK_PIN GPIO_NUM_15
#define AUDIO_DIN_PIN GPIO_NUM_16
#define AUDIO_DOUT_PIN GPIO_NUM_45
#define AUDIO_WS_PIN GPIO_NUM_38
#define I2C_SDA_PIN GPIO_NUM_47
#define I2C_SCL_PIN GPIO_NUM_48
#define I2C_RTC_ADDR 0x51
#define I2C_SHTC3_ADDR 0x70
#define I2C_FT6336_ADDR 0x38
#define I2C_ES8311_ADDR 0x18

static const bool STATUS_LED_ACTIVE_LOW = true;
#ifdef BUDDY_ACCELERATED_LIFE_TEST
static const uint32_t SIMULATION_INTERVAL_MS = 10UL * 1000UL;
#else
static const uint32_t SIMULATION_INTERVAL_MS = 10UL * 60UL * 1000UL;
#endif
static const uint32_t ENVIRONMENT_INTERVAL_MS = 2UL * 60UL * 1000UL;
static const uint16_t PARTIALS_BEFORE_FULL = 12;

// Only new generation-one lives use this salt. Existing saved seeds, and the
// family line derived from them, are never regenerated on a firmware update.
uint32_t buddyDeviceSalt() {
  uint8_t mac[6] = {};
  if (esp_efuse_mac_get_default(mac) != ESP_OK) return 0;
  uint32_t hash = 2166136261UL;
  for (uint8_t byte : mac) hash = (hash ^ byte) * 16777619UL;
  return hash;
}

#include "buddy_types.h"
#include "buddy_item_art.h"
#include "buddy_settings.h"
#include "buddy_runtime_policy.h"
#include "buddy_battery.h"
#include "buddy_deadlines.h"
#include "buddy_intro.h"
#include "buddy_sleep_policy.h"
#include "buddy_clock_policy.h"
#include "buddy_time_domains.h"
#ifdef BUDDY_ACCELERATED_LIFE_TEST
#include "buddy_test_clock.h"
#endif
#include "buddy_button.h"
#include "buddy_house.h"
#include "buddy_inner_life.h"
#include "buddy_ambition.h"
#include "buddy_legacy.h"
#include "buddy_commit.h"
#include "buddy_nearby_protocol.h"
#include "buddy_cable_protocol.h"
#include "buddy_cable_stream.h"
#include "buddy_cable_session.h"
#include "buddy_nearby_schedule.h"
#include "buddy_tune.h"
#include "buddy_sound.h"
#include "buddy_cable_errors.h"
#include "buddy_cable_restart.h"
#include "buddy_cable_watchdog.h"
#include "buddy_cable_trace.h"
#include "buddy_cable_shutdown.h"
#include "buddy_worker_join.h"
#include "buddy_cable_recovery.h"
#include "buddy_i2c_clear.h"
#include "buddy_voice_detector.h"
#include "buddy_speech_commands.h"
#include "buddy_update_format.h"

Preferences preferences;
BuddyState buddy;
uint32_t buddyLifeClockOffsetSeconds = 0;
BuddyIntroState buddyIntro = {};
bool introRequired = false;
bool introLocked = false;
bool introSaveFailed = false;
BuddyHouseState buddyHouse = {};
bool houseLocked = false;
bool houseSaveFailed = false;
BuddyAmbitionState buddyAmbition = {};
bool ambitionLocked = false;
bool ambitionSaveFailed = false;
BuddyLegacyState buddyLegacy = {};
bool legacyLocked = false;
bool legacySaveFailed = false;
NearbyState nearbyState = {};
NearbyEncounter nearbyEncounter = {};
NearbySeen nearbySeen[4] = {};
NearbyPacket cableScenePeer = {};
uint8_t cableSceneActivity = NEAR_WATCH;
uint8_t cableSceneMoment = MOMENT_WATCH;
uint32_t cableNextMomentMs = 0;
uint32_t cableClockStableSinceMs = 0;
bool cableReturnRepaintPending = false;
uint32_t cableReturnRepaintAtMs = 0;
bool cableReturnFaultShown = false;
bool cableReturnActivityOverride = false;
uint32_t cableReturnOverrideUntilMs = 0;
bool nearbyEnabled = false;
BuddyCableSession cableSession; // session-only; never auto-enter after a reset
inline bool cableFriendsActive() { return buddyCableActive(cableSession); }
uint32_t nearbyVoiceSearchUntilMs = 0;
bool nearbyStorageLocked = false;
bool nearbySaveFailed = false;
bool nearbySettingSaveFailed = false;
bool nearbyRadioActive = false;
uint8_t nearbyRadioError = 0;
RTC_DATA_ATTR uint32_t nearbyInitMarker = 0;
static constexpr uint32_t NEARBY_INIT_MARKER = 0x4e494d31UL;
bool nearbyCrashGuard = false;
bool nearbyGuardJustSet = false;
epaper_driver_display *display = nullptr;
bool rtcAvailable = false;
bool touchAvailable = false;
bool sdAvailable = false;
bool sleepSdCardPresent = false;
// An hourly timer check-in must never remount a card behind the sleeping face.
bool sdSuspended = false;
bool needsRender = true;
bool partialReady = false;
bool cleanPartialRequested = false;
uint16_t partialCount = 0;
BuddyView currentView = VIEW_BUDDY;
uint32_t lastUserMs = 0;
uint32_t lastSimulationMs = 0;
uint32_t lastEnvironmentMs = 0;
uint32_t nextMomentMs = 0; // transient; chosen anew after every boot/wake
bool momentScheduled = false;
uint32_t lastClockServiceMs = 0;
uint32_t buddyLightSleepCount = 0;
uint32_t buddyLastSleepRequestedMs = 0;
uint32_t buddyLastSleepElapsedMs = 0;
uint8_t buddyLastLightWake = 0;
volatile uint32_t viewOpenedMs = 0;
float latestTempC = 22.0f;
float latestHumidity = 45.0f;
bool latestClimateValid = false;
float latestBatteryV = 0.0f;
uint8_t climateConsecutiveFailures = 0;
BuddyBatteryNotice batteryNotice;
float previousBatteryV = 0.0f;
bool usbHostPowerPresent = false;
bool batteryChargeTrend = false;
uint8_t risingBatterySamples = 0;
uint8_t fallingBatterySamples = 0;
uint32_t lastUsbPowerCheckMs = 0;
uint32_t lastChargeEvidenceMs = 0;
uint32_t lastPowerSampleMs = 0;
uint32_t lastStorageRetryMs = 0;
bool externalPowerWasPresent = false;
bool storageLocked = false;
bool saveFailed = false;
bool archiveBlocked = false;
bool clockTrusted = false;
bool clockFault = false;
uint32_t clockLastEpoch = 0;
uint32_t clockLastMs = 0;
BuddyClockReason clockFaultReason = CLOCK_REASON_NONE;
BuddyClockReason lastClockRecoveredReason = CLOCK_REASON_NONE;
uint8_t clockLastRaw[7] = {};
uint32_t clockLastBadEpoch = 0;
uint32_t lastClockRepairMs = 0;
uint8_t clockRepairAttempts = 0;
uint16_t clockBusClearAttempts = 0;
uint16_t clockBusClearFailures = 0;
uint16_t clockBusReinitFailures = 0;
uint8_t clockBusLinesBefore = 0;
bool cableClockRecoveryPending = false;
bool cableMarkerClearFailed = false;
uint8_t cableClockStableReads = 0;
uint32_t lastCableClockCheckMs = 0;
bool clockRecoveryPending = false;
bool clockIncidentCaptured = false;
BuddyClockReason clockRecoveryReason = CLOCK_REASON_NONE;
uint32_t clockRecoveryFloor = 0;
uint8_t clockRecoveryRaw[7] = {};
bool lifeRollbackDetected = false;
uint8_t bootResetReason = 0;
bool wakeAudioAttempted = false;
bool wakeAudioOk = false;
bool watchdogReady = false;
uint8_t *portraitPixels = nullptr;
BuddyMoment visibleMoment = MOMENT_WATCH;
BuddyBehavior visibleBehavior = BEHAVIOR_OBSERVE;
uint32_t visibleMomentBucket = UINT32_MAX;
bool lastButtonLevel = true;
volatile bool buttonDown = false;
BuddyButtonRecognizer buddyButton;
QueueHandle_t buddyButtonEvents = nullptr;
volatile bool buddyLedFeedbackEnabled = true;
bool longSent = false;
bool shortPending = false;
uint32_t buttonChangedMs = 0;
uint32_t shortReleasedMs = 0;
i2s_chan_handle_t buddyTxChannel = nullptr;
i2s_chan_handle_t buddyRxChannel = nullptr;
BuddyVoiceDetector buddyVoiceDetector;
static portMUX_TYPE buddyVoiceLock = portMUX_INITIALIZER_UNLOCKED;
bool buddyMicActive = false;
bool buddySpeechActive = false;
bool buddySpeechPaused = false;
uint8_t buddySpeechError = 0;
int8_t buddySpeechModelStatus = -1;
uint32_t buddyPendingHeardMs = 0;
uint8_t buddyMicError = 0;
uint32_t buddyVoiceUntilMs = 0;
uint32_t buddyVoiceCommandUntilMs = 0;
BuddyVoiceCommand buddyLastVoiceCommand = VOICE_NONE;
bool buddyVoiceCommandFailed = false;
uint32_t buddyMicRetryMs = 0;
uint16_t buddyMicPeak = 0;
uint32_t buddyMicAboveFrames = 0;
uint32_t buddyMicHeardCount = 0;
uint32_t buddyMicReadyMs = 0;
uint32_t buddyVoiceRewardMs = 0;
bool buddyVoiceRewarded = false;
uint8_t buddyAudioError = 0;
uint8_t buddyAudioRegister = 0;
float climateTempSamples[5] = {};
float climateHumiditySamples[5] = {};
uint8_t climateSampleCount = 0;
uint8_t climateSampleIndex = 0;
uint32_t climateReadFailures = 0;
uint32_t climateLastGoodMs = 0;
uint8_t settingsSelection = 0;
uint8_t orientationMode = ORIENTATION_AUTO;
bool displayRotation180 = false;
BuddyCableError cableModeError = CABLE_ERROR_NONE;
RTC_NOINIT_ATTR BuddyCableRestartNotice cableRestartNotice;
RTC_NOINIT_ATTR BuddyCableWatchdogMark cableWatchdogMark;
RTC_NOINIT_ATTR BuddyCableTrace cableTrace;
BuddyCableTrace cableLastTrace = {};
BuddyCableStage cableLastWatchdogStage = CABLE_STAGE_OFF;
bool cableWatchdogFresh = false;
bool cableRestartRequested = false;
BuddyCableError cableBootFailure = CABLE_ERROR_NONE;
BuddySoundMode buddySoundMode = SOUND_LOW;
bool buddySoundSaveFailed = false;
bool buddyInteractionSoundPending = false;
bool hourlyDozeWake = false;
uint32_t awakeBootMs = 0;
bool buddySpecialPending = false;
uint32_t buddySpecialEpoch = 0;
bool renderingDoze = false;
uint32_t lastAutonomousSoundMs = 0;
BuddyListeningWindow buddyListeningWindow;
uint32_t buddyMicStartupMs = 0;
uint32_t buddyMicStarts = 0;
uint32_t buddyMicStops = 0;
BuddyCriticalBattery criticalBattery;
uint32_t lastCriticalCheckMs = 0;
bool buddyLightSleepWakeReady = false;
BuddyLightSleepRetry buddyLightSleepRetry;

void archiveLife();
void appendDailyJournal(uint32_t epoch);
void writeBirthRecord();
void saveBuddy();
void setupBuddyIntro();
void saveBuddyIntro();
bool birthBuddyFromIntro(uint32_t now);
void handleBuddyIntroButton(ButtonEvent event);
void setupBuddyHouse(uint32_t now);
void saveBuddyHouse();
void advanceBuddyHouseTo(uint32_t epoch);
void setupBuddyAmbition(uint32_t now);
void saveBuddyAmbition();
void serviceBuddyAmbition(uint32_t epoch);
void buddyAmbitionOnEvent(uint32_t epoch, LifeEvent event, uint8_t object);
void setupBuddyLegacy(uint32_t now);
void saveBuddyLegacy();
void advanceBuddyLegacyTo(uint32_t epoch);
void buddyLegacyComplete(uint32_t epoch, uint8_t object);
void buddyLegacyOnEvent(uint32_t epoch, LifeEvent event, uint8_t object);
BuddyVisualGenome visualGenome();
BuddyVisualGenome nearbyVisualGenome();
NearbyPacket currentNearbyPacket();
void setupNearbyStorage();
void saveNearbyState();
void serviceNearbyLife();
bool setNearbyEnabled(bool enabled);
void observeNearbyPacket(const NearbyPacket &packet, int8_t rssi);
void serviceNearbyEncounter();
bool nearbySceneVisible();
uint8_t nearbySeenCount();
const char *nearbyActivityText(uint8_t activity);
bool startNearbyRadio();
bool startVoiceNearbySearch();
bool nearbyDiscoveryActive();
bool startCableFriends();
void stopCableFriends();
void quiesceCableLink();
void exitCableFriends();
void serviceCableFriends();
void refreshCablePacket();
bool updateDisplayOrientation();
void stopNearbyRadio();
void shutdownNearbyStackForSleep();
void refreshNearbyAdvertisement();
void serviceNearbyRadio();
void enterBuddySleep(bool manual = true, bool critical = false);
void renderBuddy(bool forceFull = false);
bool playBuddySound(BuddySound sound, bool diagnostic = false, int8_t fixedVariant = -1);
bool playBuddyTune();
bool startBuddyMic();
void stopBuddyMic();
bool setBuddySoundEnabled(bool enabled);
bool setBuddySoundMode(BuddySoundMode mode);
void loadBuddySoundMode();
void serviceBuddyMic();
bool startBuddySpeech();
void stopBuddySpeech();
void serviceBuddySpeech();
void handleBuddyVoiceCommand(BuddyVoiceCommand command);
void handleBuddyHeard();
uint32_t buddyNow();
void initializeBuddyWorld(bool inherited);
void liveWorldHour(uint32_t epoch, BuddyBehavior behavior, bool climateKnown);
int memoryBehaviorBias(BuddyBehavior behavior, uint32_t epoch);
uint8_t buddyDailyPursuit(uint32_t epoch);
uint8_t buddyRoutineObject(uint32_t epoch, uint8_t *strength = nullptr);
int buddyFamilyTraditionAffinity();
bool buddyFamilyTraditionWindow(uint32_t epoch);
bool buddyFamilyTraditionRecent(uint32_t epoch);
BuddyProject buddyActiveProject(uint32_t epoch);
uint32_t buddyRecentProjectCompletion(uint32_t epoch);
BuddyBehavior pursuitBehavior(uint8_t object);
void rememberWorldEvent(uint32_t epoch, LifeEvent event, uint8_t object, BuddyBehavior behavior, int pleasure);
const char *objectName(uint8_t object);
void describeWorldEvent(const WorldMemory &event, char *out, size_t length);
bool queueMemorial();
bool exportPortrait(const char *path);
void retryBuddyArchives();
void serviceBuddyStorage(uint32_t now);
void updateBuddyMoment(uint32_t now, bool force = false,
                       bool allowClockFaultForDisplay = false);
const char *buddyMomentText(BuddyMoment requested);
const char *visibleMoodName(uint32_t now);

#include "buddy_hardware_base.inc"
#include "buddy_sensors.inc"
#include "buddy_test_clock.inc"
#include "buddy_clock.inc"
#include "buddy_sim.inc"
#include "buddy_intro.inc"
#include "buddy_world.inc"
#include "buddy_moments.inc"
#include "buddy_storage.inc"
#include "buddy_recovery.inc"
#include "buddy_house.inc"
#include "buddy_ambition.inc"
#include "buddy_legacy.inc"
#include "buddy_commit.inc"
#include "buddy_nearby_logic.inc"
#include "buddy_cable.inc"
#include "buddy_nearby_radio.inc"
#include "buddy_audio.inc"
#include "buddy_speech.inc"
#include "buddy_mic.inc"
#include "buddy_power.inc"
#include "buddy_ui.inc"
#include "buddy_update.inc"
#include "buddy_app.inc"

#include "../../third_party/waveshare_epaper_display/epaper_driver_bsp.cpp"
