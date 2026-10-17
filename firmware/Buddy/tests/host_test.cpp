// Executes the production simulation, clock, power and storage against fault-
// injectable in-memory peripherals. No connected board or real save is touched.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>
#include <fstream>
#include <iterator>
#include <set>
#include <filesystem>
#include "../buddy_types.h"
#include "../buddy_item_art.h"
#include "../buddy_settings.h"
#include "../buddy_runtime_policy.h"
#include "../buddy_deadlines.h"
#include "../buddy_intro.h"
#include "../buddy_house.h"
#include "../buddy_inner_life.h"
#include "../buddy_ambition.h"
#include "../buddy_legacy.h"
#include "../buddy_commit.h"
#include "../buddy_nearby_protocol.h"
#include "../buddy_cable_protocol.h"
#include "../buddy_cable_stream.h"
#include "../buddy_cable_session.h"
#include "../buddy_nearby_schedule.h"
#include "../buddy_voice_detector.h"
#include "../buddy_update_format.h"
#include "../buddy_speech_commands.h"
#include "../buddy_adpcm.h"
#include "../buddy_tune.h"
#include "../buddy_sound.h"
#include "../buddy_cable_errors.h"
#include "../buddy_cable_restart.h"
#include "../buddy_cable_watchdog.h"
#include "../buddy_cable_trace.h"
#include "../buddy_cable_shutdown.h"
#include "../buddy_cable_recovery.h"
#include "../buddy_i2c_clear.h"
#define BUDDY_WIRELESS_LINK_ENABLED 1 // Exercise retained encounter logic without a radio.
using std::min; using std::max;
static std::string buddyTestTempPath(const char *name) {
  return (std::filesystem::temp_directory_path() / name).string();
}
template<typename T,typename A,typename B> T constrain(T v,A a,B b) { return min<T>(b,max<T>(a,v)); }
class String : public std::string {
public:
  using std::string::string;
  String(const std::string &s):std::string(s){}
  template<typename T, typename = std::enable_if_t<std::is_arithmetic_v<T>>> String(T n):std::string(std::to_string(n)){}
  int lastIndexOf(char c,size_t pos) const { auto p=rfind(c,pos);return p==npos?-1:p; }
  String substring(size_t pos) const {return substr(pos);}
  int indexOf(const String &s) const {auto p=find(s);return p==npos?-1:p;}
  bool reserve(size_t n){std::string::reserve(n);return true;}
};
static std::map<std::string,std::vector<uint8_t>> nvs;
static bool failNvs=false;
static int failNvsAfter=-1;
static bool failNvsRemove=false;
class Preferences {
  std::string space;
public:
  bool begin(const char *s,bool) {space=s;return true;}
  void end() {}
  size_t getBytesLength(const char *key){return nvs[space+"/"+key].size();}
  size_t getBytes(const char *key,void *out,size_t size){auto &v=nvs[space+"/"+key];size=min(size,v.size());memcpy(out,v.data(),size);return size;}
  size_t putBytes(const char *key,const void *in,size_t size){if(failNvs || failNvsAfter==0)return 0; if(failNvsAfter>0)--failNvsAfter; auto *p=(const uint8_t*)in;nvs[space+"/"+key]=std::vector<uint8_t>(p,p+size);return size;}
  uint64_t getULong64(const char *key,uint64_t def){uint64_t v=def;if(getBytesLength(key)==8)getBytes(key,&v,8);return v;}
  size_t putULong64(const char *key,uint64_t v){return putBytes(key,&v,8);}
  uint8_t getUChar(const char *key,uint8_t def){uint8_t v=def;if(getBytesLength(key)==1)getBytes(key,&v,1);return v;}
  size_t putUChar(const char *key,uint8_t v){return putBytes(key,&v,1);}
  bool getBool(const char *key,bool def){return getUChar(key,def?1:0)!=0;}
  size_t putBool(const char *key,bool value){return putUChar(key,value?1:0);}
  bool remove(const char *key){return !failNvsRemove && nvs.erase(space+"/"+key)>0;}
};
#include "../buddy_sleep_policy.h"
#include "../buddy_clock_policy.h"
#include "../buddy_time_domains.h"
#include "../buddy_accelerated_time.h"
#ifdef BUDDY_ACCELERATED_LIFE_TEST
#include "../buddy_test_clock.h"
#endif
#include "../buddy_button.h"
static bool failSd=false;
static bool sdMounted=false;
static unsigned sdMountCalls=0,sdUnmountCalls=0;
static std::map<std::string,std::string> files;
static const char *FILE_READ="r", *FILE_WRITE="w";
class File {
  std::string path; size_t pos=0;bool good=false;
public:
  File()=default;
  File(std::string p,bool g):path(p),good(g){}
  operator bool()const{return good;}
  size_t size(){return files[path].size();}
  size_t print(const String &s){if(!good||failSd)return 0;files[path]+=s;return s.size();}
  int read(){return pos<files[path].size()?(uint8_t)files[path][pos++]:-1;}
  String readString(){return files[path];}
  void flush(){} void close(){}
};
struct FakeSD {
  void setPins(int,int,int){}
  bool begin(const char*,bool){sdMountCalls++;sdMounted=!failSd;return sdMounted;}
  void end(){sdUnmountCalls++;sdMounted=false;}
  bool exists(const String &p){return files.count(p);}
  bool mkdir(const char*){return !failSd;}
  bool remove(const String &p){return !failSd && files.erase(p)>0;}
  bool rename(const String &a,const String &b){if(failSd||!files.count(a)||files.count(b))return false;files[b]=files[a];files.erase(a);return true;}
  File open(const String &p,const char *mode){if(failSd)return {};if(mode==FILE_WRITE)files[p]="";return File(p,files.count(p));}
} SD_MMC;
BuddyState buddy={}; Preferences preferences;
uint32_t buddyLifeClockOffsetSeconds=0;
#ifdef BUDDY_ACCELERATED_LIFE_TEST
struct FakeSerial { void printf(const char *, ...) {} } Serial;
#endif
BuddyIntroState buddyIntro={};bool introRequired=false,introLocked=false,introSaveFailed=false;
BuddyHouseState buddyHouse={};bool houseLocked=false,houseSaveFailed=false;
BuddyAmbitionState buddyAmbition={};bool ambitionLocked=false,ambitionSaveFailed=false;
BuddyLegacyState buddyLegacy={};bool legacyLocked=false,legacySaveFailed=false;
NearbyState nearbyState={};NearbyEncounter nearbyEncounter={};NearbySeen nearbySeen[4]={};
NearbyPacket cableScenePeer={};uint8_t cableSceneActivity=NEAR_WATCH;
uint8_t cableSceneMoment=MOMENT_WATCH;
bool nearbyEnabled=false,nearbyStorageLocked=false,nearbySaveFailed=false;
BuddyCableSession cableSession;
BuddyCableStage cableLastWatchdogStage = CABLE_STAGE_OFF;
inline bool cableFriendsActive(){return buddyCableActive(cableSession);}
BuddyCableError cableModeError=CABLE_ERROR_NONE;
BuddySoundMode buddySoundMode=SOUND_LOW;
bool buddySoundSaveFailed=false;
bool cableReturnActivityOverride=false;
uint32_t cableGreetingUntilMs=0;
bool cableGreetingKnown=false;
bool cableGreetingAwaitingPaint=false;
uint32_t nearbyVoiceSearchUntilMs=0;
bool nearbySettingSaveFailed=false,nearbyRadioActive=false,hourlyDozeWake=false;
uint8_t nearbyRadioError=0;
bool cleanPartialRequested=false;
bool storageLocked=false,saveFailed=false,archiveBlocked=false,clockFault=false,clockTrusted=false,rtcAvailable=false;
bool buddySpecialPending=false;
uint32_t nextMomentMs=0;
bool momentScheduled=false;
uint32_t buddySpecialEpoch=0;
bool sdAvailable=false,sdSuspended=false,needsRender=false,latestClimateValid=true;
float latestTempC=22,latestHumidity=45;
uint32_t clockLastEpoch=0,clockLastMs=0;
BuddyClockReason clockFaultReason=CLOCK_REASON_NONE,lastClockRecoveredReason=CLOCK_REASON_NONE;
BuddyClockReason clockRecoveryReason=CLOCK_REASON_NONE;
uint8_t clockLastRaw[7]={},clockRecoveryRaw[7]={};
uint32_t clockLastBadEpoch=0,lastClockRepairMs=0,clockRecoveryFloor=0;
uint8_t clockRepairAttempts=0;
bool clockRecoveryPending=false;
bool clockIncidentCaptured=false,lifeRollbackDetected=false;
uint8_t bootResetReason=0,buddyAudioError=0,buddyMicError=0;
enum { ESP_RST_TASK_WDT=6, ESP_RST_WDT=7, ESP_RST_INT_WDT=8, ESP_RST_BROWNOUT=9 };
uint32_t ticks=0,rawEpoch=800000000;bool rtcGood=true,rtcOscillator=true,usb=false;
bool rtcSetGood=true;unsigned rtcSetCalls=0;
uint32_t millis(){return ticks;}
void delay(uint32_t ms){ticks+=ms;}
uint32_t fakeRandomState=31415,fakeDeviceSalt=0xB00D1234;
uint32_t esp_random(){return fakeRandomState=fakeRandomState*1664525+1013904223;}
uint32_t buddyDeviceSalt(){return fakeDeviceSalt;}
uint32_t daysFromCivil(uint16_t y,uint8_t m,uint8_t d){
  y-=m<=2;uint16_t era=y/400,yoe=y-era*400;
  uint16_t doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
  return era*146097UL+yoe*365UL+yoe/4-yoe/100+doy-730425UL;
}
bool isLeapYear(uint16_t y){return (y%4==0 && y%100!=0)||y%400==0;}
uint8_t daysInMonth(uint16_t y,uint8_t m){static const uint8_t d[]={31,28,31,30,31,30,31,31,30,31,30,31};return d[m-1]+(m==2&&isLeapYear(y));}
bool validDate(const RtcDateTime &d){return d.year>=2025&&d.year<=2099&&d.month>=1&&d.month<=12&&d.day>=1&&d.day<=31;}
uint8_t bcdToDec(uint8_t x){return x/16*10+x%16;}
static uint8_t fakeBcd(uint8_t x){return x/10*16+x%10;}
uint32_t toEpoch(const RtcDateTime &d){return daysFromCivil(d.year,d.month,d.day)*86400UL+d.hour*3600UL+d.minute*60UL+d.second;}
static RtcDateTime fakeDate(uint32_t epoch){
  uint32_t day=epoch/86400UL;
  for(uint16_t y=2025;y<=2099;y++)for(uint8_t m=1;m<=12;m++){
    uint32_t first=daysFromCivil(y,m,1);
    if(day>=first && day<first+daysInMonth(y,m))return {y,m,(uint8_t)(day-first+1),
      (uint8_t)((epoch%86400UL)/3600UL),(uint8_t)((epoch/60UL)%60UL),(uint8_t)(epoch%60UL)};
  }
  return {};
}
bool i2cReadRegister(int,int,uint8_t *p,size_t n){
  if(!rtcGood)return false;
  auto d=fakeDate(rawEpoch);if(n<7)return false;
  p[0]=fakeBcd(d.second)|(rtcOscillator?0:0x80);
  p[1]=fakeBcd(d.minute);p[2]=fakeBcd(d.hour);p[3]=fakeBcd(d.day);
  p[4]=0;p[5]=fakeBcd(d.month);p[6]=fakeBcd(d.year-2000);return true;
}
bool setRtc(const RtcDateTime &d){rtcSetCalls++;if(!rtcSetGood)return false;rawEpoch=toEpoch(d);rtcOscillator=true;return true;}
RtcDateTime buildDateTime(){return {2026,10,13,12,0,0};}
static const int I2C_RTC_ADDR=0x51;
bool usb_serial_jtag_is_connected(){return usb;}
float previousBatteryV=0;bool usbHostPowerPresent=false,batteryChargeTrend=false;
uint8_t risingBatterySamples=0,fallingBatterySamples=0;
uint32_t lastUsbPowerCheckMs=0,lastChargeEvidenceMs=0;
void archiveLife();void appendDailyJournal(uint32_t);void writeBirthRecord();void saveBuddy();
void setupBuddyIntro();void saveBuddyIntro();bool birthBuddyFromIntro(uint32_t);
void setupBuddyHouse(uint32_t);void saveBuddyHouse();void advanceBuddyHouseTo(uint32_t);
void setupBuddyAmbition(uint32_t);void saveBuddyAmbition();void serviceBuddyAmbition(uint32_t);
void buddyAmbitionOnEvent(uint32_t,LifeEvent,uint8_t);
void setupBuddyLegacy(uint32_t);void saveBuddyLegacy();void advanceBuddyLegacyTo(uint32_t);
void buddyLegacyComplete(uint32_t,uint8_t);void buddyLegacyOnEvent(uint32_t,LifeEvent,uint8_t);
BuddyVisualGenome visualGenome();
BuddyVisualGenome nearbyVisualGenome();
bool startNearbyRadio(){return nearbyRadioActive=true;}
void stopNearbyRadio(){nearbyRadioActive=false;}
void refreshNearbyAdvertisement(){}
void refreshCablePacket(){}
void initializeBuddyWorld(bool);void liveWorldHour(uint32_t,BuddyBehavior,bool);
BuddyProject buddyActiveProject(uint32_t);
uint32_t buddyRecentProjectCompletion(uint32_t);
int memoryBehaviorBias(BuddyBehavior,uint32_t);
uint8_t buddyDailyPursuit(uint32_t);
uint8_t buddyRoutineObject(uint32_t,uint8_t*);
int buddyFamilyTraditionAffinity();
bool buddyFamilyTraditionWindow(uint32_t);
bool buddyFamilyTraditionRecent(uint32_t);
BuddyBehavior pursuitBehavior(uint8_t);
void rememberWorldEvent(uint32_t,LifeEvent,uint8_t,BuddyBehavior,int);
const char *objectName(uint8_t);void describeWorldEvent(const WorldMemory&,char*,size_t);
bool queueMemorial();bool exportPortrait(const char*);
#include "../buddy_test_clock.inc"
#include "../buddy_clock.inc"
#include "../buddy_sim.inc"
#include "../buddy_intro.inc"
#include "../buddy_world.inc"
#include "../buddy_storage.inc"
#include "../buddy_recovery.inc"
#include "../buddy_house.inc"
#include "../buddy_ambition.inc"
#include "../buddy_legacy.inc"
#include "../buddy_commit.inc"
BuddyListeningWindow buddyListeningWindow;
unsigned micStops=0;
void stopBuddyMic(){++micStops;}
void touchBuddyListeningWindow(){
  if(buddySoundMode!=SOUND_OFF && buddy.soundEnabled) buddyListeningTouch(buddyListeningWindow,millis());
}
#include "../buddy_sound_settings.inc"
#include "../buddy_nearby_logic.inc"
#include "../buddy_power.inc"
static const uint8_t DRIVER_COLOR_BLACK=0,DRIVER_COLOR_WHITE=1;
struct FakeDisplay {
  uint8_t buffer[5000]={};
  int fullUpdates=0,partialUpdates=0;
  void EPD_DrawColorPixel(int x,int y,uint8_t color){uint8_t mask=0x80>>(x%8);if(color==0)buffer[y*25+x/8]|=mask;else buffer[y*25+x/8]&=~mask;}
  void EPD_Clear(){memset(buffer,0,sizeof(buffer));}
  void EPD_Init(){} void EPD_Display(){fullUpdates++;} void EPD_Init_Partial(){} void EPD_DisplayPart(){partialUpdates++;}
} fakeDisplay, *display=&fakeDisplay;
uint8_t *portraitPixels=nullptr;
BuddyMoment visibleMoment=MOMENT_WATCH;
BuddyBehavior visibleBehavior=BEHAVIOR_OBSERVE;
uint32_t visibleMomentBucket=UINT32_MAX;
uint8_t batteryPercent(float v){return constrain((v-3.3f)*100/0.9f,0,100);}
float latestBatteryV=4.1f;
BuddyView currentView=VIEW_BUDDY;
uint8_t settingsSelection=0,orientationMode=ORIENTATION_AUTO;
uint32_t buddyVoiceUntilMs=0;
uint32_t buddyVoiceCommandUntilMs=0;
BuddyVoiceCommand buddyLastVoiceCommand=VOICE_NONE;
bool buddyVoiceCommandFailed=false;
bool renderingDoze=false, sleepSdCardPresent=false;
bool displayRotation180=false;
bool partialReady=false;uint16_t partialCount=0;
static const uint16_t PARTIALS_BEFORE_FULL=12;
#include "../buddy_moments.inc"
#include "../buddy_ui.inc"

static void reset() {
  buddyCompletionPending=buddyCompletionPreparing=buddyCompletionApplying=false;
  buddySimulationStepActive=buddySimulationStepCompleted=false;
  buddyListeningWindow={};micStops=0;
  migrationProtectedLifeSlot=migrationProtectedAmbitionSlot=migrationProtectedLegacySlot=-1;
  buddyClockAnchorEpoch=buddyClockAnchorMs=0;
  failNvsAfter=-1;failNvsRemove=false;
  buddy={};buddyHouse={};buddyAmbition={};buddyLegacy={};nvs.clear();files.clear();preferences.begin("buddy-life",false);
  buddyLifeClockOffsetSeconds=0;
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  testLifeTime={};testClockSequence=0;testClockSlot=-1;testClockReady=false;
#endif
  buddyIntro={};introRequired=introLocked=introSaveFailed=false;introSequence=0;activeIntroSlot=-1;
  storageLocked=saveFailed=archiveBlocked=clockFault=clockTrusted=sdAvailable=sdSuspended=false;
  sdMounted=false;sdMountCalls=sdUnmountCalls=0;
  houseLocked=houseSaveFailed=false;activeHouseSlot=-1;houseSequence=0;
  ambitionLocked=ambitionSaveFailed=false;activeAmbitionSlot=-1;ambitionSequence=0;
  legacyLocked=legacySaveFailed=false;activeLegacySlot=-1;legacySequence=0;
  nearbyState={};nearbyEncounter={};cableScenePeer={};cableSceneActivity=NEAR_WATCH;
  memset(nearbySeen,0,sizeof(nearbySeen));
  nearbyEnabled=nearbyStorageLocked=nearbySaveFailed=nearbySettingSaveFailed=nearbyRadioActive=false;
  buddyCableStop(cableSession);cableGreetingKnown=false;cableGreetingUntilMs=0;
  cableModeError=CABLE_ERROR_NONE;buddySoundMode=SOUND_LOW;
  buddySoundSaveFailed=cableReturnActivityOverride=false;
  nearbyVoiceSearchUntilMs=0;
  buddyVoiceCommandUntilMs=0;buddyLastVoiceCommand=VOICE_NONE;buddyVoiceCommandFailed=false;
  nearbyRadioError=0;nearbySequence=0;activeNearbySlot=-1;hourlyDozeWake=false;
  failNvs=failSd=false;clockLastEpoch=clockLastMs=0;activeSaveSlot=-1;saveSequence=0;
  rtcGood=rtcOscillator=rtcSetGood=latestClimateValid=true;rtcSetCalls=0;
  clockRecoveryPending=false;clockRecoveryReason=CLOCK_REASON_NONE;clockRecoveryFloor=0;
  clockIncidentCaptured=lifeRollbackDetected=false;
  lastRecovery={};recoverySlot=-1;recoveryReportedState=-1;
  clockFaultReason=lastClockRecoveredReason=CLOCK_REASON_NONE;
  lastClockRepairMs=clockLastBadEpoch=0;clockRepairAttempts=0;
  latestTempC=22;rawEpoch=800000000;ticks=0;
}
static void testSdUpdatePackage() {
  BuddyUpdateHeader h = {};
  memcpy(h.magic, BUDDY_UPDATE_MAGIC, sizeof(h.magic));
  memcpy(h.target, BUDDY_UPDATE_TARGET, sizeof(h.target));
  h.format = 1;
  h.release = BUDDY_FIRMWARE_VERSION + 1;
  h.imageSize = 1100000;
  assert(buddyCheckUpdate(h, sizeof(h) + h.imageSize, 0x200000) == UPDATE_READY);
  assert(buddyCheckUpdate(h, sizeof(h) + h.imageSize - 1, 0x200000) == UPDATE_BAD_SIZE);
  assert(buddyCheckUpdate(h, sizeof(h) + h.imageSize, 1000000) == UPDATE_BAD_SIZE);
  h.release = BUDDY_FIRMWARE_VERSION;
  assert(buddyCheckUpdate(h, sizeof(h) + h.imageSize, 0x200000) == UPDATE_ALREADY_CURRENT);
  h.release = BUDDY_FIRMWARE_VERSION - 1;
  assert(buddyCheckUpdate(h, sizeof(h) + h.imageSize, 0x200000) == UPDATE_ALREADY_CURRENT);
  h.release = BUDDY_FIRMWARE_VERSION + 1;
  h.target[0] ^= 1;
  assert(buddyCheckUpdate(h, sizeof(h) + h.imageSize, 0x200000) == UPDATE_BAD_TARGET);
  h.target[0] ^= 1;
  h.magic[0] ^= 1;
  assert(buddyCheckUpdate(h, sizeof(h) + h.imageSize, 0x200000) == UPDATE_BAD_FILE);
  h.magic[0] ^= 1;
  h.imageSize = UINT32_MAX;
  assert(buddyCheckUpdate(h, UINT32_MAX, 0x200000) == UPDATE_BAD_SIZE);
}

static void start();
static void testSdSleepHandoff() {
  start();
  setupBuddyStorage();
  assert(sdAvailable && sdMounted && sdMountCalls == 1);
  serviceBuddyStorage(rawEpoch);
  auto beforeSleepFiles = files;
  suspendBuddyStorageForSleep();
  assert(sdSuspended && !sdAvailable && !sdMounted && sdUnmountCalls == 1);
  // Timer check-ins may live another hour, but may not silently remount or
  // touch the card while the e-paper still promises a sleeping face.
  setupBuddyStorage();
  serviceBuddyStorage(rawEpoch + 86400UL);
  appendDailyJournal(rawEpoch + 86400UL);
  assert(sdMountCalls == 1 && files == beforeSleepFiles);
  resumeBuddyStorageAfterWake();
  assert(!sdSuspended && sdAvailable && sdMounted && sdMountCalls == 2);
  serviceBuddyStorage(rawEpoch + 86400UL);
  assert(files != beforeSleepFiles);

  // Removal during sleep and insertion of another valid card are handled at
  // the next deliberate wake; an absent card remains optional and retries.
  suspendBuddyStorageForSleep();
  failSd = true;
  resumeBuddyStorageAfterWake();
  assert(!sdAvailable && !sdMounted);
  failSd = false;
  serviceBuddyStorage(rawEpoch + 2UL * 86400UL);
  assert(sdAvailable && sdMounted);
}
static void testVoiceCommandPolicy() {
  auto off = buddyVoiceAction(VOICE_SOUND_OFF);
  auto sleep = buddyVoiceAction(VOICE_GO_TO_SLEEP);
  auto unknown = buddyVoiceAction(99);
  assert(off.changeSound && !off.soundOn && !off.manualSleep);
  assert(!strcmp(buddyVoiceCommandText(VOICE_SOUND_OFF), "SOUND OFF"));
  assert(!sleep.changeSound && sleep.manualSleep);
  assert(!unknown.changeSound && !unknown.manualSleep);
  assert(!strcmp(buddyVoiceCommandText(2), "")); // retired command ID
  reset();
  nearbyEnabled=true;nearbyVoiceSearchUntilMs=ticks+300000UL;
  assert(!nearbyDiscoveryActive()); // old wireless setting cannot start a link
  buddyCableStart(cableSession,ticks);
  assert(nearbyDiscoveryActive());
  buddyCableStop(cableSession);
  assert(!nearbyDiscoveryActive() && nearbyEnabled);
}

static void testCableSessionLifecycle() {
  // Hardware UART replaces the per-session GPIO ISR and software bit loop.
  std::string sourcePath = __FILE__;
  size_t testSuffix = sourcePath.rfind("tests/host_test.cpp");
  assert(testSuffix != std::string::npos);
  sourcePath.replace(testSuffix, strlen("tests/host_test.cpp"), "buddy_cable.inc");
  std::ifstream cableSource(sourcePath);
  assert(cableSource);
  std::string source((std::istreambuf_iterator<char>(cableSource)),
                     std::istreambuf_iterator<char>());
  assert(source.find("gpio_install_isr_service") == std::string::npos);
  assert(source.find("cableReadByte") == std::string::npos);
  assert(source.find("stopBuddyMic();") < source.find("uart_driver_install"));
  assert(source.find("uart_driver_install") < source.find("xTaskCreatePinnedToCore(cableWorker"));
  assert(source.find("esp_rom_gpio_connect_out_signal") <
         source.find("CABLE_OP_UART_ECHO"));
  assert(source.find("uart_read_bytes(UART_NUM_1, &echo") <
         source.find("xTaskCreatePinnedToCore(cableWorker"));
  assert(std::string(buddyCableOperationText(CABLE_OP_UART_ECHO)) == "UART_ECHO");
  for (unsigned resources = 0; resources < 8; ++resources) {
    std::vector<BuddyCableShutdownStep> actual;
    buddyCableShutdownSequence(resources & 1, resources & 2, resources & 4,
      [&](BuddyCableShutdownStep step) { actual.push_back(step); });
    std::vector<BuddyCableShutdownStep> expected = {CABLE_SHUTDOWN_DISARM};
    if (resources & 2) expected.push_back(CABLE_SHUTDOWN_STOP_WORKER);
    if (resources & 1) {
      expected.push_back(CABLE_SHUTDOWN_STOP_UART);
    }
    if (resources & 4) expected.push_back(CABLE_SHUTDOWN_RELEASE_PADS);
    assert(actual == expected);
  }
  // Two complete mock hardware lifecycles plus an abandoned wait. The
  // display must occur only after every acquired resource is quiesced.
  for (unsigned retry = 0; retry < 3; ++retry) {
    bool armed = true, uart = true, worker = true, pads = true;
    std::vector<BuddyCableShutdownStep> calls;
    buddyCableShutdownSequence(uart, worker, pads, [&](BuddyCableShutdownStep step) {
      calls.push_back(step);
      switch (step) {
        case CABLE_SHUTDOWN_DISARM: armed = false; break;
        case CABLE_SHUTDOWN_STOP_UART: assert(!worker); uart = false; break;
        case CABLE_SHUTDOWN_STOP_WORKER: assert(!armed && uart); worker = false; break;
        case CABLE_SHUTDOWN_RELEASE_PADS: assert(!worker && !uart); pads = false; break;
      }
    });
    assert(calls.size() == 4 && !armed && !uart && !worker && !pads);
    // A fresh activation can claim its own resources, independent of the
    // previous peer, or of whether that activation ever saw a peer.
  }
  for (unsigned acquired = 0; acquired < 16; ++acquired) {
    bool queue = acquired & 1, pads = acquired & 2;
    bool worker = acquired & 4, uart = acquired & 8;
    bool armed = uart, running = worker;
    auto cleanup = [&] {
      buddyCableShutdownSequence(uart, worker, pads, [&](BuddyCableShutdownStep step) {
        switch (step) {
          case CABLE_SHUTDOWN_DISARM: armed = running = false; break;
          case CABLE_SHUTDOWN_STOP_UART: assert(!worker); uart = false; break;
          case CABLE_SHUTDOWN_STOP_WORKER:
            worker = false;
            break;
          case CABLE_SHUTDOWN_RELEASE_PADS: assert(!worker && !uart); pads = false; break;
        }
      });
      if (queue) { assert(!worker); queue = false; }
    };
    cleanup();
    cleanup(); // idempotent cleanup after partial or failed startup
    assert(!queue && !pads && !worker && !uart && !armed);
  }
  assert(BUDDY_CABLE_BIT_US == 2000 && BUDDY_CABLE_BYTE_GAP_US >= 4 * BUDDY_CABLE_BIT_US);
  for (uint8_t code = CABLE_ERROR_USB_HOST; code <= CABLE_ERROR_UART; ++code)
    assert(buddyCableErrorText((BuddyCableError)code)[0]);
  BuddyCableWatchdogMark mark = {};
  assert(!buddyCableClockRepairAllowed(true));
  assert(buddyCableClockRepairAllowed(false));
  assert(std::string(buddyCableStageText(CABLE_STAGE_RECEIVING)) == "RX");
  assert(std::string(buddyCableStageText(CABLE_STAGE_SCENE)) == "SCENE");
  assert(std::string(buddyCableStageText(CABLE_STAGE_AUDIO)) == "AUDIO");
  assert(std::string(buddyCableStageText(CABLE_STAGE_GPIO)) == "GPIO");
  assert(std::string(buddyCableStageText(CABLE_STAGE_WORKER)) == "WORKER");
  assert(std::string(buddyCableStageText(CABLE_STAGE_ISR)) == "IRQ");
  BuddyCableTrace trace = {};
  buddyCableTraceSet(trace, CABLE_OP_ISR_ADD);
  buddyCableTraceFail(trace, CABLE_OP_ISR_ADD, 0x103);
  buddyCableTraceSnapshot(trace, 0x0f, CABLE_WAITING, true);
  buddyCableTraceStep(trace, CABLE_OP_FAREWELL);
  buddyCableTraceFail(trace, CABLE_OP_ISR_REMOVE, 0x102);
  assert(trace.operation == CABLE_OP_ISR_ADD && trace.error == 0x103);
  BuddyCableTrace recovered = buddyCableTraceTake(trace);
  assert(recovered.operation == CABLE_OP_ISR_ADD && recovered.error == 0x103);
  assert(recovered.resources == 0x0f && recovered.phase == CABLE_WAITING &&
         recovered.rtcRecovery == 1);
  assert(buddyCableTraceTake(trace).operation == CABLE_OP_NONE);
  buddyCableTraceSet(trace, CABLE_OP_WAIT);
  assert(buddyCableTraceTake(trace).operation == CABLE_OP_WAIT);
  assert(std::string(buddyCableOperationText(CABLE_OP_ISR_REMOVE)) == "ISR_REMOVE");
  assert(std::string(buddyCableStageText(CABLE_STAGE_DISPLAY)) == "DISPLAY");
  assert(!buddyCableWorkerReadyExpired(false, 1000, 1149));
  assert(buddyCableWorkerReadyExpired(false, 1000, 1150));
  assert(!buddyCableWorkerReadyExpired(true, 1000, 9000));
  assert(buddyCableWorkerReadyExpired(false, UINT32_MAX-20, 129));
  Preferences cableWatchdogStore;
  assert(cableWatchdogStore.begin("test-cable-watchdog", false));
  assert(buddyCableLoadWatchdogStage(cableWatchdogStore) == CABLE_STAGE_OFF);
  assert(buddyCablePersistWatchdogStage(cableWatchdogStore, CABLE_STAGE_SENDING));
  assert(buddyCableLoadWatchdogStage(cableWatchdogStore) == CABLE_STAGE_SENDING);
  assert(buddyCablePersistWatchdogStage(cableWatchdogStore, CABLE_STAGE_DISPLAY));
  assert(buddyCableLoadWatchdogStage(cableWatchdogStore) == CABLE_STAGE_DISPLAY);
  failNvs = true;
  assert(!buddyCablePersistWatchdogStage(cableWatchdogStore, CABLE_STAGE_STOPPING));
  failNvs = false;
  assert(buddyCableLoadWatchdogStage(cableWatchdogStore) == CABLE_STAGE_DISPLAY);
  assert(!buddyCablePersistWatchdogStage(cableWatchdogStore, CABLE_STAGE_OFF));
  cableWatchdogStore.end();
  buddyCableMark(mark, CABLE_STAGE_SENDING);
  assert(buddyCableTakeWatchdogMark(mark, true) == CABLE_STAGE_SENDING);
  assert(buddyCableTakeWatchdogMark(mark, true) == CABLE_STAGE_OFF);
  buddyCableMark(mark, CABLE_STAGE_RECEIVING);
  assert(buddyCableTakeWatchdogMark(mark, false) == CABLE_STAGE_OFF);
  buddyCableMark(mark, CABLE_STAGE_STOPPING);
  mark.inverse ^= 1;
  assert(buddyCableTakeWatchdogMark(mark, true) == CABLE_STAGE_OFF);
  unsigned uninterrupted = 0, longest = 0, yields = 0;
  for (size_t i = 0; i < BUDDY_CABLE_FRAME_BYTES; ++i) {
    uninterrupted++;
    if (buddyCableYieldAfterByte(i, BUDDY_CABLE_FRAME_BYTES)) {
      longest = max(longest, uninterrupted);
      uninterrupted = 0;
      yields++;
    }
  }
  assert(max(longest, uninterrupted) <= 2 && yields >= 7);
  assert(!buddyCableErrorText(CABLE_ERROR_NONE)[0]);
  BuddyCableRestartNotice notice = {};
  buddyCableNoticeSet(notice, CABLE_ERROR_RUNTIME);
  assert(buddyCableNoticeTake(notice, true, CABLE_ERROR_RUNTIME) == CABLE_ERROR_RUNTIME);
  assert(buddyCableNoticeTake(notice, true, CABLE_ERROR_RUNTIME) == CABLE_ERROR_NONE);
  buddyCableNoticeSet(notice, CABLE_ERROR_GPIO);
  assert(buddyCableNoticeTake(notice, false, CABLE_ERROR_RUNTIME) == CABLE_ERROR_NONE);
  buddyCableNoticeSet(notice, CABLE_ERROR_WORKER);
  notice.inverse ^= 1;
  assert(buddyCableNoticeTake(notice, true, CABLE_ERROR_RUNTIME) == CABLE_ERROR_NONE);
  buddyCableNoticeSet(notice, CABLE_ERROR_NONE);
  assert(buddyCableNoticeTake(notice, true, CABLE_ERROR_RUNTIME) == CABLE_ERROR_NONE);
  assert(buddyCableDeferFarewellRepaint(true, false, false));
  assert(!buddyCableDeferFarewellRepaint(true, false, true));
  assert(buddyCableGreetingVisible(true,0,1000));
  assert(buddyCableGreetingVisible(false,7000,6999));
  assert(!buddyCableGreetingVisible(false,7000,7000));
  assert(buddyCableGreetingVisible(false,1000,UINT32_MAX-200));
  BuddyCableSession session = {};
  assert(!buddyCableActive(session));
  assert(buddyCableEndDue(session, UINT32_MAX) == CABLE_CONTINUE);
  assert(!buddyCableObservePeer(session, 22, 11, 100));
  buddyCableStart(session, 1000);
  assert(session.phase == CABLE_WAITING && buddyCableActive(session));
  assert(!buddyCableObservePeer(session, 0, 11, 2000));
  assert(!buddyCableObservePeer(session, 11, 11, 2000));
  assert(buddyCableEndDue(session, 1000 + BUDDY_CABLE_WAIT_MS - 1) == CABLE_CONTINUE);
  assert(buddyCableEndDue(session, 1000 + BUDDY_CABLE_WAIT_MS) == CABLE_WAIT_EXPIRED);
  assert(!buddyCableObservePeer(session, 22, 11, 1000 + BUDDY_CABLE_WAIT_MS));
  buddyCableStop(session);
  assert(!buddyCableActive(session) && !session.peerId && !session.startedMs);

  buddyCableStart(session, 5000);
  assert(buddyCableObservePeer(session, 22, 11, 6000));
  assert(session.phase == CABLE_CONNECTED && session.peerId == 22);
  assert(!buddyCableObservePeer(session, 33, 11, 7000)); // one activation, one peer
  assert(session.lastPeerMs == 6000);
  assert(buddyCableEndDue(session, 6000 + BUDDY_CABLE_SILENCE_MS - 1) == CABLE_CONTINUE);
  assert(buddyCableObservePeer(session, 22, 11, 6000 + BUDDY_CABLE_SILENCE_MS - 1));
  // Heartbeats can continue for hours; the initial two-minute cap is gone.
  uint32_t firstHeartbeat = session.lastPeerMs + 1000;
  for (uint32_t elapsed = 0; elapsed < 4UL * 60UL * 60UL * 1000UL; elapsed += 2000) {
    uint32_t at = firstHeartbeat + elapsed;
    assert(buddyCableObservePeer(session, 22, 11, at));
    assert(buddyCableEndDue(session, at) == CABLE_CONTINUE);
  }
  assert(buddyCableEndDue(session, session.lastPeerMs + BUDDY_CABLE_SILENCE_MS) == CABLE_PEER_LOST);
  uint32_t lastValid = session.lastPeerMs;
  assert(!buddyCableObservePeer(session, 22, 11, lastValid + BUDDY_CABLE_SILENCE_MS));
  assert(session.lastPeerMs == lastValid); // delayed/reconnected packet cannot revive it
  buddyCableStop(session);
  assert(!buddyCableActive(session)); // shutdown cannot fall back to waiting

  buddyCableStart(session, UINT32_MAX - 5000);
  assert(buddyCableEndDue(session, UINT32_MAX - 5000 + BUDDY_CABLE_WAIT_MS - 1) == CABLE_CONTINUE);
  assert(buddyCableEndDue(session, UINT32_MAX - 5000 + BUDDY_CABLE_WAIT_MS) == CABLE_WAIT_EXPIRED);
  buddyCableStop(session);
  buddyCableStart(session, UINT32_MAX - 1000);
  assert(buddyCableObservePeer(session, 22, 11, UINT32_MAX - 500));
  assert(buddyCableEndDue(session, UINT32_MAX - 500 + BUDDY_CABLE_SILENCE_MS) == CABLE_PEER_LOST);
  // Twenty validated frames fit in the 32-entry receive queue while e-paper
  // or audio delays the main loop. Their wire timestamps keep the meeting live.
  buddyCableStop(session);buddyCableStart(session, 1000);
  assert(buddyCableObservePeer(session, 22, 11, 2000));
  for(uint32_t i=0;i<20;i++)
    assert(buddyCableObservePeer(session, 22, 11, 3000+i*1000));
  assert(buddyCableEndDue(session, 23000) == CABLE_CONTINUE);
  assert(buddyCableEndDue(session, 22000+BUDDY_CABLE_SILENCE_MS) == CABLE_PEER_LOST);
  buddyCableStop(session);
  assert(!session.peerId && !session.lastPeerMs && !session.startedMs);
  buddyCableStart(session, 30000);
  assert(session.phase == CABLE_WAITING && !session.peerId);
  assert(buddyCableObservePeer(session, 33, 11, 31000));
  assert(session.peerId == 33); // second activation can meet another peer
  buddyCableStop(session);
  buddyCableStart(session, 40000);
  assert(buddyCableObservePeer(session, 22, 11, 41000)); // or the same peer
  // WAITING -> user BOOT exit -> reboot -> WAITING must also re-arm cleanly.
  buddyCableStop(session);
  for (unsigned retry = 0; retry < 5; ++retry) {
    buddyCableStart(session, 50000 + retry * 10000);
    assert(session.phase == CABLE_WAITING && !session.peerId);
    buddyCableStop(session);
    assert(session.phase == CABLE_OFF && !session.peerId);
  }
}

static void testTwoCableBuddiesReconnect() {
  BuddyCableSession a = {}, b = {};
  NearbyPacket packetA = {}, packetB = {}, decoded = {};
  packetA.sender = 101; packetB.sender = 202;
  packetA.visual = packetB.visual = {1,1,1,1,1,1,1};
  packetA.stage = packetB.stage = STAGE_ADULT;
  packetA.activity = packetB.activity = NEAR_PLAY;
  packetA.moment = packetB.moment = MOMENT_WATCH;
  uint8_t wire[BUDDY_CABLE_FRAME_BYTES];
  for (uint32_t retryDelay : {0u, 1000u, 3000u, 30000u, 60000u}) {
    uint32_t now = 1000;
    for (unsigned round = 0; round < 3; ++round) {
      buddyCableStart(a, now); buddyCableStart(b, now);
      assert(a.phase == CABLE_WAITING && b.phase == CABLE_WAITING);
      now += 1000;
      encodeBuddyCableFrame(packetA, wire);
      assert(decodeBuddyCableFrame(wire, sizeof(wire), decoded));
      assert(buddyCableObservePeer(b, decoded.sender, packetB.sender, now));
      encodeBuddyCableFrame(packetB, wire);
      assert(decodeBuddyCableFrame(wire, sizeof(wire), decoded));
      assert(buddyCableObservePeer(a, decoded.sender, packetA.sender, now));
      assert(a.phase == CABLE_CONNECTED && b.phase == CABLE_CONNECTED);
      now += BUDDY_CABLE_SILENCE_MS;
      assert(buddyCableEndDue(a, now) == CABLE_PEER_LOST);
      assert(buddyCableEndDue(b, now) == CABLE_PEER_LOST);
      buddyCableStop(a); buddyCableStop(b);
      assert(!buddyCableActive(a) && !buddyCableActive(b));
      now += retryDelay;
    }
  }
}
static void testSoundModes() {
  reset();
  bool failed = false;
  assert(buddyLoadSoundMode(preferences, false, failed) == SOUND_OFF && !failed);
  assert(preferences.getUChar("sound-mode", 255) == SOUND_OFF);
  preferences.remove("sound-mode");
  assert(buddyLoadSoundMode(preferences, true, failed) == SOUND_LOW && !failed);
  assert(preferences.getUChar("sound-mode", 255) == SOUND_LOW);
  assert(buddyNextSoundMode(SOUND_OFF) == SOUND_LOW);
  assert(buddyNextSoundMode(SOUND_LOW) == SOUND_HIGH);
  assert(buddyNextSoundMode(SOUND_HIGH) == SOUND_OFF);
  assert(buddyPersistSoundMode(preferences, SOUND_HIGH));
  assert(buddyLoadSoundMode(preferences, false, failed) == SOUND_HIGH && !failed);
  createBuddyLife(rawEpoch, false);
  saveBuddy();
  assert(buddyLoadSoundMode(preferences, buddy.soundEnabled, failed) == SOUND_HIGH);
  preferences.putUChar("sound-mode", 42);
  assert(buddyLoadSoundMode(preferences, true, failed) == SOUND_LOW && !failed);
  failNvs = true;
  assert(!buddyPersistSoundMode(preferences, SOUND_HIGH));
  preferences.remove("sound-mode");
  assert(buddyLoadSoundMode(preferences, false, failed) == SOUND_OFF && failed);
  failNvs = false;
  assert(buddyPersistSoundMode(preferences, SOUND_OFF));
  assert(buddySoundSample(30000, SOUND_LOW) == 15000);
  assert(buddySoundSample(30000, SOUND_HIGH) == 30000);
  assert(buddySoundSample(-32768, SOUND_LOW) == -16384);
  assert(buddySoundSample(-32768, SOUND_HIGH) == -32768);
  assert(buddySoundMix((int64_t)32767 * 16000 * 5, 16000 * 5) == 32767);
  assert(buddySoundMix((int64_t)-32768 * 16000 * 5, 16000 * 5) == -32768);
  assert(buddySoundMix(INT64_MAX, 1) == INT16_MAX);
  assert(buddySoundMix(INT64_MIN, 1) == INT16_MIN);
  assert(buddySoundMix(0, 0) == 0);
}
static void testCableRecoveryMarker() {
  reset();
  assert(buddyCableRapidClockCheck(true,true,true,0));
  assert(buddyCableRapidClockCheck(true,false,false,59999));
  assert(!buddyCableRapidClockCheck(true,true,true,60000));
  assert(!buddyCableRapidClockCheck(false,true,false,100));
  assert(buddyCableRapidClockCheck(false,true,true,100));
  assert(buddyClockCheckIntervalMs(true, false) == 2000);
  assert(buddyClockCheckIntervalMs(true, true) == 2000);
  assert(buddyClockCheckIntervalMs(false, true) == 500);
  assert(buddyClockCheckIntervalMs(false, false) == 30000);
  preferences.begin("buddy-cable", false);
  assert(buddyCableWriteRecoveryMarker(preferences, true));
  assert(preferences.getBool("rtc-check", false));
  failNvs = true;
  assert(!buddyCableWriteRecoveryMarker(preferences, false));
  assert(preferences.getBool("rtc-check", false)); // failed clear stays durable
  failNvs = false;
  assert(buddyCableWriteRecoveryMarker(preferences, false));
  assert(!preferences.getBool("rtc-check", true));
  uint32_t until = UINT32_MAX - 5;
  assert(buddyCableDisplayGraceActive(until - 1, until));
  assert(!buddyCableDisplayGraceActive(until, until));
  assert(!buddyCableDisplayGraceActive(10, until));
  until = 0xfffffff0u + BUDDY_CABLE_DISPLAY_GRACE_MS;
  assert(buddyCableDisplayGraceActive(0xfffffff0u, until));
  assert(!buddyCableDisplayGraceActive(until, until));
}
static void testI2cBusClear() {
  struct Lines {
    int pulses = 0, releaseAfter = 0;
    bool sdaReleased = true, sclReleased = true, sclStuck = false;
    void releaseSda() { sdaReleased = true; }
    void releaseScl() { if (!sclReleased) ++pulses; sclReleased = true; }
    void driveSdaLow() { sdaReleased = false; }
    void driveSclLow() { sclReleased = false; }
    bool sdaHigh() const { return sdaReleased && pulses >= releaseAfter; }
    bool sclHigh() const { return sclReleased && !sclStuck; }
    void pause() const {}
  };
  Lines free;
  assert(buddyClearI2cBus(free) && free.pulses == 1);
  Lines releasedAfterThree;
  releasedAfterThree.releaseAfter = 3;
  assert(buddyClearI2cBus(releasedAfterThree));
  assert(releasedAfterThree.pulses == 4); // three clocks plus STOP
  Lines stuckSda;
  stuckSda.releaseAfter = 100;
  assert(!buddyClearI2cBus(stuckSda) && stuckSda.pulses == 9);
  Lines stuckScl;
  stuckScl.sclStuck = true;
  assert(!buddyClearI2cBus(stuckScl) && stuckScl.pulses == 0);
}
static void testSoundDecoder() {
  BuddyAdpcmDecoder decoder(1234);
  assert(decoder.next(0) == 1234);
  assert(decoder.next(7) == 1245);
  assert(decoder.next(8) == 1243);
  BuddyAdpcmDecoder other(-200);
  assert(other.next(0) == -200); // independent clip predictor
  for (int i = 0; i < 1000; ++i) {
    int16_t value = other.next(7);
    assert(value >= -32768 && value <= 32767);
    assert(other.index >= 0 && other.index <= 88);
  }
}

static void testTunePlan() {
  bool heard[BUDDY_SOUND_CLIPS] = {};
  for (uint32_t seed = 1; seed < 512; ++seed) {
    BuddyTunePlan plan = buddyTunePlan(seed);
    for (uint8_t i = 0; i < BUDDY_TUNE_CLIPS; ++i) {
      assert(plan.clip[i] < BUDDY_SOUND_CLIPS);
      heard[plan.clip[i]] = true;
      for (uint8_t j = 0; j < i; ++j) assert(plan.clip[i] != plan.clip[j]);
    }
  }
  for (bool used : heard) assert(used);
  assert(BUDDY_TUNE_CLIPS==5);
  assert(BUDDY_TUNE_OVERLAP_FRAMES == 16000);
  assert(buddyTuneNextStart(0,24000)==8000);
  assert(buddyTuneNextStart(0,38667)==22667); // one second of overlap
  assert(buddyTuneNextStart(22667,38668)==45335);
  assert(buddyTuneNextStart(0,10000)==0); // never underflow a short clip
  assert(buddyTuneWeight(0,38667,true,false)==16000);
  assert(buddyTuneWeight(0,38667,false,true)==0);
  assert(buddyTuneWeight(38666,38667,false,true)==16000);
}
static void start(){reset();createBuddyLife(rawEpoch,false);setupBuddyHouse(rawEpoch);setupBuddyLegacy(rawEpoch);assert(buddy.alive);}

static void testRecoveryButton() {
  BuddyButtonRecognizer heldAtBoot;
  heldAtBoot.stableHigh = heldAtBoot.candidateHigh = false;
  heldAtBoot.longSent = true; // setup suppresses a held BOOT across reset
  for (uint32_t ms = 0; ms < 2500; ms += 10)
    assert(heldAtBoot.poll(false, ms) == BUTTON_NONE);
  heldAtBoot.poll(true, 2500);
  assert(heldAtBoot.poll(true, 2540) == BUTTON_NONE);
  assert(heldAtBoot.poll(true, 2800) == BUTTON_NONE);
  heldAtBoot.poll(false, 2900);
  heldAtBoot.poll(false, 2940);
  assert(heldAtBoot.poll(false, 4000) == BUTTON_LONG);
  assert(!buddyMenuReturnDue(44999,0,false));
  assert(buddyMenuReturnDue(45000,0,false));
  assert(!buddyMenuReturnDue(50000,0,true)); // a Settings hold cannot become sleep
  assert(!buddyMenuReturnDue(45010,45000,false)); // any new press resets idle time
  assert(!buddyMenuReturnDue(0x10,0xfffffff0u,false));
  assert(buddyMenuReturnDue(0xb000,0xfffffff0u,false));
  auto exercise=[](unsigned count) {
    BuddyButtonRecognizer recognizer;
    std::vector<ButtonEvent> events;
    uint32_t time=0;
    auto run=[&](bool high,uint32_t ms) {
      for(uint32_t elapsed=0;elapsed<ms;elapsed+=10,time+=10){
        ButtonEvent event=recognizer.poll(high,time);
        if(event!=BUTTON_NONE)events.push_back(event);
      }
    };
    for(unsigned i=0;i<count;i++){run(false,100);run(true,140);}
    run(true,400);
    return events;
  };
  assert(exercise(1)==std::vector<ButtonEvent>{BUTTON_SHORT});
  assert(exercise(2)==std::vector<ButtonEvent>{BUTTON_DOUBLE});
  assert(exercise(5)==std::vector<ButtonEvent>{BUTTON_RECOVER});
  assert(exercise(4).empty());
  assert(buddyClickAction(VIEW_BUDDY,exercise(1)[0]) == BUDDY_CLICK_INTERACT);
  assert(buddyClickAction(VIEW_BUDDY,exercise(2)[0]) == BUDDY_CLICK_OPEN_MENU);
  assert(buddyClickAction(VIEW_BUDDY,exercise(5)[0]) == BUDDY_CLICK_NONE);
  assert(buddyClickAction(VIEW_ABOUT,BUTTON_SHORT) == BUDDY_CLICK_ADVANCE_MENU);
  assert(buddyClickAction(VIEW_SETTINGS,BUTTON_SHORT) == BUDDY_CLICK_ADVANCE_MENU);
  assert(buddyClickAction(VIEW_ABOUT,BUTTON_DOUBLE) == BUDDY_CLICK_RETURN_HOME);
  assert(buddyClickAction(VIEW_SETTINGS,BUTTON_DOUBLE) == BUDDY_CLICK_RETURN_HOME);
  // A completed single tap is reported promptly, while an adjacent tap is
  // still recognized as a double-click and not two navigation actions.
  BuddyButtonRecognizer fast;
  fast.poll(false,0);
  fast.poll(false,40);
  fast.poll(true,100);
  fast.poll(true,140);
  assert(fast.poll(true,390)==BUTTON_NONE);
  assert(fast.poll(true,400)==BUTTON_SHORT);
  BuddyButtonRecognizer pair;
  pair.poll(false,0); pair.poll(false,40); pair.poll(true,100); pair.poll(true,140);
  pair.poll(false,330); pair.poll(false,370); pair.poll(true,430); pair.poll(true,470);
  assert(pair.poll(true,730)==BUTTON_DOUBLE);
  BuddyButtonRecognizer recognizer;
  recognizer.poll(false,0);
  recognizer.poll(false,35); // debounced press, still physically held
  assert(recognizer.poll(false,1034)==BUTTON_NONE);
  assert(recognizer.poll(false,1035)==BUTTON_LONG); // 1000 ms before release
  assert(recognizer.poll(false,3000)==BUTTON_NONE); // no repeat while held
  recognizer.poll(true,3010);
  assert(recognizer.poll(true,3045)==BUTTON_NONE); // release never becomes a click
  recognizer.poll(false,3100);
  recognizer.poll(false,3135);
  assert(recognizer.poll(false,4135)==BUTTON_LONG); // release rearmed hold
  // The same event reaches Settings and GOODNIGHT while still held; the
  // production handler renders a changed setting or the sleep face at entry.
  assert(buddyClickAction(VIEW_SETTINGS,BUTTON_LONG)==BUDDY_CLICK_NONE);
  assert(buddyClickAction(VIEW_BUDDY,BUTTON_LONG)==BUDDY_CLICK_NONE);
  BuddyButtonRecognizer wrapped;
  uint32_t nearWrap = 0xfffffff0u;
  wrapped.poll(false,nearWrap);
  wrapped.poll(false,nearWrap+35);
  assert(wrapped.poll(false,nearWrap+1034)==BUTTON_NONE);
  assert(wrapped.poll(false,nearWrap+1035)==BUTTON_LONG);
  assert(wrapped.poll(false,nearWrap+2040)==BUTTON_NONE);
}

static void testClockPolicy() {
  auto boot=decideBuddyClock(true,true,false,800000000,800000100,false,0,0,0);
  assert(boot.reason==CLOCK_REASON_BACKWARD&&boot.repair&&boot.effective==800000100);
  auto stopped=decideBuddyClock(true,true,true,800000000,800000100,false,0,0,0);
  assert(stopped.reason==CLOCK_REASON_OSC_STOP&&stopped.repair);
  auto noBus=decideBuddyClock(false,false,false,0,800000100,false,0,0,0);
  assert(noBus.reason==CLOCK_REASON_COMM&&!noBus.repair);
  auto jump=decideBuddyClock(true,true,false,800000100+46UL*86400,800000100,false,0,0,0);
  assert(jump.reason==CLOCK_REASON_FORWARD&&jump.repair);
  auto normal=decideBuddyClock(true,true,false,800000101,800000100,true,800000100,0,1000);
  assert(normal.reason==CLOCK_REASON_NONE&&normal.effective==800000101);
  uint32_t randomBefore=fakeRandomState;
  start();clockFault=true;clockTrusted=false;cleanPartialRequested=false;
  assert(recoverBuddyClockForCable() && !clockFault && cleanPartialRequested);
  rtcGood=false;clockFault=true;
  assert(!recoverBuddyClockForCable() && clockFault); // a real RTC failure still blocks reentry
  fakeRandomState=randomBefore;
}

static void testRestoredLifeClock() {
  const uint32_t pause = 4UL * 86400UL + 137UL;
  BuddyLifeClockOffset record = buddyMakeLifeClockOffset(pause);
  assert(buddyValidLifeClockOffset(record));
  record.checksum ^= 1;
  assert(!buddyValidLifeClockOffset(record));
  uint32_t shifted = 0;
  assert(!buddyLifeEpochFromPhysical(pause - 1, pause, shifted));
  assert(!buddyPhysicalEpochFromLife(UINT32_MAX, pause, shifted));
  assert(buddyPhysicalEpochFromLife(800000000UL, pause, shifted));
  assert(shifted == 800000000UL + pause);

  start();
  const uint32_t before = buddyNow();
  const uint32_t birth = buddy.birthEpoch;
  const uint32_t last = buddy.lastUpdateEpoch;
  const uint32_t seed = buddy.seed;
  const uint16_t generation = buddy.generation;
  BuddyState original = buddy;
  record = buddyMakeLifeClockOffset(pause);
  assert(preferences.putBytes("lifeclock", &record, sizeof(record)) == sizeof(record));
  // Reboot from the original save while the physical RTC has advanced four
  // days. This must not add four biological days or rewrite the RTC.
  rawEpoch += pause;
  clockTrusted = false;
  clockLastEpoch = clockLastMs = 0;
  buddy = {};
  loadOrCreateBuddy(0);
  assert(loadBuddyLifeClockOffset());
  unsigned repairsBefore = rtcSetCalls;
  assert(buddyNow() == before && !clockFault && rtcSetCalls == repairsBefore);
  updateBuddySimulation(buddyNow());
  assert(buddy.birthEpoch == birth && buddy.lastUpdateEpoch == last &&
         buddy.seed == seed && buddy.generation == generation);
  assert(memcmp(&buddy, &original, sizeof(buddy)) == 0);
  // Real time and logical time advance together after the restoration.
  rawEpoch += 3600;
  ticks += 3600000;
  assert(buddyNow() == before + 3600 && !clockFault);
  updateBuddySimulation(buddyNow());
  assert(buddy.lastUpdateEpoch == last + 3600);
  // Failed physical RTC reads still stop life progression.
  rtcGood = false;
  uint32_t saved = buddy.lastUpdateEpoch;
  assert(buddyNow() == saved && clockFault);
  updateBuddySimulation(saved + 86400UL);
  assert(buddy.lastUpdateEpoch == saved);
  rtcGood = true;
  assert(buddyNow() == saved && !clockFault);
  // Invalid offset storage fails closed before applying an unshifted time.
  record.checksum ^= 1;
  assert(preferences.putBytes("lifeclock", &record, sizeof(record)) == sizeof(record));
  assert(!loadBuddyLifeClockOffset() && storageLocked);
  rawEpoch += 86400UL;
  assert(buddyNow() == saved && clockFault);
}

static void testRawRestoreCatchesUpByRealDays() {
  uint32_t randomBefore = fakeRandomState;
  start();
  uint32_t before = buddyNow();
  uint32_t seed = buddy.seed;
  rawEpoch += 2UL * 86400UL;
  buddy = {};
  clockTrusted = clockFault = false;
  clockLastEpoch = clockLastMs = ticks = 0;
  loadOrCreateBuddy(0);
  assert(buddy.seed == seed && buddyNow() == before + 2UL * 86400UL && !clockFault);
  updateBuddySimulation(buddyNow());
  assert(buddyAgeDays(buddyNow()) == 2 && buddy.lastUpdateEpoch == before + 48UL * 3600UL);
  fakeRandomState = randomBefore;
}

#ifdef BUDDY_RESTORE_OFFSET_SECONDS
static void testRestoreBootstrap() {
  start();
  assert(!preferences.getBytesLength("lifeclock"));
  assert(loadBuddyLifeClockOffset());
  assert(buddyLifeClockOffsetSeconds == BUDDY_RESTORE_OFFSET_SECONDS);
  BuddyLifeClockOffset stored = {};
  assert(preferences.getBytes("lifeclock", &stored, sizeof(stored)) == sizeof(stored));
  assert(buddyValidLifeClockOffset(stored));
  assert(loadBuddyLifeClockOffset()); // reboot uses the same record
  start();
  failNvs = true;
  assert(!loadBuddyLifeClockOffset() && storageLocked);
  failNvs = false;
}
#endif

static void testAcceleratedTimeArithmeticAndLives() {
  uint32_t randomBefore = fakeRandomState;
  uint32_t result = 0;
  for (uint16_t lifespan : {305, 365, 425}) {
    BuddyAcceleratedTime map = {800000000UL, 800000000UL, 1234, 1, lifespan};
    assert(buddyAcceleratedNow(map, map.realBirth, result));
    assert(result == map.simulatedBirth);
    assert(buddyAcceleratedNow(map, map.realBirth + BUDDY_TEST_REAL_LIFE_SECONDS, result));
    assert(result == map.simulatedBirth + (uint32_t)lifespan * 86400UL);
    uint32_t inverse = 0;
    assert(buddyAcceleratedRealAt(map, result, inverse));
    assert(inverse == map.realBirth + BUDDY_TEST_REAL_LIFE_SECONDS);
    assert(!buddyAcceleratedNow(map, map.realBirth - 1, result));
    assert(!buddyAcceleratedNow(map, UINT32_MAX, result));
  }
  start();
  sdAvailable = true;
  uint32_t physicalBirth = rawEpoch;
  uint16_t minimum = 999, maximum = 0;
  for (uint16_t generation = 1; generation <= 20; ++generation) {
    uint16_t lifespan = buddyLifespanDays(buddy.seed);
    BuddyAcceleratedTime map = {physicalBirth, buddy.birthEpoch,
                                buddy.seed, generation, lifespan};
    minimum = min(minimum, lifespan);
    maximum = max(maximum, lifespan);
    uint32_t middle = 0;
    assert(buddyAcceleratedNow(map, physicalBirth + BUDDY_TEST_REAL_LIFE_SECONDS / 2, middle));
    assert(buddyAgeDays(middle) == lifespan / 2);
    uint32_t death = 0;
    assert(buddyAcceleratedNow(map, physicalBirth + BUDDY_TEST_REAL_LIFE_SECONDS, death));
    updateBuddySimulation(death); // the unchanged hourly life simulator
    assert(!buddy.alive && buddy.deathEpoch == death &&
           buddy.prior.livedDays == lifespan);
    retryBuddyArchives();
    uint32_t successorPhysical = physicalBirth + BUDDY_TEST_REAL_LIFE_SECONDS + 300UL;
    uint32_t successorSimulated = 0;
    assert(buddyAcceleratedNow(map, successorPhysical, successorSimulated));
    assert(successorSimulated > buddy.deathEpoch + 86400UL);
    buddy.memorialShown = true; // after the separate real viewing interval
    updateBuddySimulation(successorSimulated);
    assert(buddy.alive && buddy.generation == generation + 1);
    BuddyAcceleratedTime next = {};
    assert(buddyAcceleratedRebase(map, buddy.birthEpoch, buddy.seed,
                                  buddy.generation,
                                  buddyLifespanDays(buddy.seed), next));
    assert(next.realBirth == successorPhysical);
    physicalBirth = next.realBirth;
  }
  assert(minimum < maximum);
  fakeRandomState = randomBefore;
}

static void testNearbySchedule() {
  assert(!BUDDY_NEARBY_STACK_ENABLED);
  assert(buddyNearbyBurstLimit(false)==30000UL);
  assert(buddyNearbyBurstLimit(true)==45000UL);
  assert(buddyNearbyBurstInterval(false,false)==300000UL);
  assert(buddyNearbyBurstInterval(false,true)==30000UL);
  assert(buddyNearbyBurstInterval(true,false)==30000UL);
  uint32_t firstScan = buddyNearbyInitialLastScanMs(1000UL);
  assert(buddyNearbyScanDue(1000UL, firstScan, BUDDY_NEARBY_IDLE_SCAN_INTERVAL_MS));
  assert(!buddyNearbyScanDue(1001UL, 1000UL, BUDDY_NEARBY_IDLE_SCAN_INTERVAL_MS));
  assert(buddyNearbyBootGuard(true,0,2026100501UL));
  assert(buddyNearbyBootGuard(false,2026100501UL,2026100501UL));
  assert(!buddyNearbyBootGuard(false,2026100501UL,2026100502UL));
}

static BuddySaveV2 oldV2Save(const BuddyState &life, uint32_t sequence) {
  BuddySaveV2 old = {};
  old.signature = 0x42535632;
  old.sequence = sequence;
  static_cast<BuddyStateV1 &>(old.state) = static_cast<const BuddyStateV1 &>(life);
  old.state.version = 2;
  old.state.worldVersion = 1;
  old.state.stayAwake = life.stayAwake;
  old.state.possessions = life.possessions & 15;
  old.state.favoriteObject = life.favoriteObject == OBJECT_COUNT ? OLD_OBJECT_COUNT : life.favoriteObject;
  for (uint8_t i=0;i<OLD_OBJECT_COUNT;i++) {
    old.state.affection[i]=life.affection[i];old.state.visits[i]=life.visits[i];
  }
  old.state.plantGrowth=life.plantGrowth;
  old.state.inheritedObject=life.inheritedObject==OBJECT_COUNT?OLD_OBJECT_COUNT:life.inheritedObject;
  old.state.familyEar=life.familyEar;old.state.familyMarking=life.familyMarking;
  old.state.eventHead=life.eventHead;old.state.eventCount=life.eventCount;
  memcpy(old.state.events,life.events,sizeof(old.state.events));
  for(auto &event:old.state.events)if(event.object==OBJECT_COUNT)event.object=OLD_OBJECT_COUNT;
  old.state.birthTraits=life.birthTraits;
  memcpy(old.state.development,life.development,sizeof(old.state.development));
  old.state.lastDevelopmentDay=life.lastDevelopmentDay;
  old.state.lastWorldEpoch=life.lastWorldEpoch;
  old.state.lastAttentionEpoch=life.lastAttentionEpoch;
  old.checksum=saveChecksum(old);
  return old;
}

static void testEightAnchorMigration() {
  uint32_t randomBefore=fakeRandomState;
  start();
  buddy.seed=0x5c95d946UL;buddy.rngState=0xBADB00DUL;
  buddy.birthEpoch=rawEpoch-2UL*86400UL;buddy.lastUpdateEpoch=rawEpoch;
  buddy.favoriteObject=OBJECT_COUNT;buddy.inheritedObject=OBJECT_COUNT;
  buddy.affection[OBJECT_PLANT]=317;buddy.visits[OBJECT_PLANT]=13;
  buddy.traits.sensitivity=82;buddy.totalInteractions=17;
  rememberWorldEvent(rawEpoch,EVENT_ATTENTION,OBJECT_COUNT,BEHAVIOR_OBSERVE,30);
  BuddySaveV2 old=oldV2Save(buddy,41);
  nvs["buddy-life/life-a"]={1};
  nvs["buddy-life/life-b"]=std::vector<uint8_t>((uint8_t*)&old,(uint8_t*)&old+sizeof(old));
  buddy={};activeSaveSlot=-1;saveSequence=0;
  failNvs=true;loadOrCreateBuddy(rawEpoch);
  assert(buddy.version==3 && buddy.worldVersion==2 && buddy.seed==0x5c95d946UL &&
    buddy.birthEpoch==rawEpoch-2UL*86400UL && buddy.totalInteractions==17 &&
    buddy.affection[OBJECT_PLANT]==317 && buddy.visits[OBJECT_PLANT]==13 &&
    buddy.favoriteObject==OBJECT_COUNT && buddy.events[(buddy.eventHead+23)%24].object==OBJECT_COUNT &&
    saveFailed && !storageLocked);
  assert(nvs["buddy-life/life-b"].size()==sizeof(BuddySaveV2));
  failNvs=false;saveBuddy();assert(!saveFailed);
  saveBuddy();assert(nvs["buddy-life/life-b"].size()==sizeof(BuddySaveV2));
  releaseBuddyMigrationProtection();
  saveBuddy();assert(nvs["buddy-life/life-a"].size()==sizeof(BuddySave) &&
    nvs["buddy-life/life-b"].size()==sizeof(BuddySave));
  BuddyState preserved=buddy;buddy={};loadOrCreateBuddy(rawEpoch);
  assert(buddy.seed==preserved.seed && buddy.birthEpoch==preserved.birthEpoch &&
    buddy.affection[OBJECT_PLANT]==317 && buddyLifespanDays(buddy.seed)==buddyLifespanDays(preserved.seed));

  BuddyAmbitionState oldAim=buddyAmbition;
  oldAim.lifeSeed=buddy.seed;oldAim.birthEpoch=buddy.birthEpoch;
  oldAim.generation=buddy.generation;
  oldAim.version=1;oldAim.kind=oldAim.lastKind=oldAim.legacyKind=OLD_OBJECT_COUNT;
  oldAim.checksum=ambitionHash(oldAim);
  nvs["buddy-life/aim-a"]=std::vector<uint8_t>((uint8_t*)&oldAim,(uint8_t*)&oldAim+sizeof(oldAim));
  nvs.erase("buddy-life/aim-b");buddyAmbition={};activeAmbitionSlot=-1;
  setupBuddyAmbition(rawEpoch);
  assert(buddyAmbition.version==2 && buddyAmbition.kind==OBJECT_COUNT &&
    buddyAmbition.lastKind==OBJECT_COUNT && buddyAmbition.legacyKind==OBJECT_COUNT &&
    !ambitionLocked && !ambitionSaveFailed);

  BuddyLegacyStateV1 oldSite={};
  oldSite.magic=BUDDY_LEGACY_MAGIC;oldSite.sequence=8;oldSite.version=1;
  oldSite.lastWeatherDay=rawEpoch/86400UL;
  oldSite.condition[OBJECT_PLANT]=65;oldSite.makerGeneration[OBJECT_PLANT]=1;
  oldSite.lastCareDay[OBJECT_PLANT]=oldSite.lastWeatherDay;
  oldSite.checksum=legacyHash(oldSite);
  nvs["buddy-life/site-a"]=std::vector<uint8_t>((uint8_t*)&oldSite,(uint8_t*)&oldSite+sizeof(oldSite));
  nvs.erase("buddy-life/site-b");buddyLegacy={};activeLegacySlot=-1;
  setupBuddyLegacy(rawEpoch);
  assert(buddyLegacy.version==2 && buddyLegacy.condition[OBJECT_PLANT]==65 &&
    !buddyLegacy.condition[OBJECT_FLOWERS] && !legacyLocked && !legacySaveFailed);
  buddyLegacy={};setupBuddyLegacy(rawEpoch);
  assert(buddyLegacy.condition[OBJECT_PLANT]==65 && !buddyLegacy.condition[OBJECT_GROVE]);

  // A queued v2 memorial remains exportable after the active life migrates.
  Preferences archive;archive.begin("buddy-archive",false);
  archive.putBytes("life0",&old,sizeof(old));archive.end();
  setupBuddyStorage();retryBuddyArchives();
  assert(nvs["buddy-archive/life0"].empty());
  fakeRandomState=randomBefore;
}

static void testEightAnchorLives() {
  uint32_t randomBefore=fakeRandomState;
  uint64_t sum=0;uint16_t shortest=500,longest=0;
  for(uint32_t seed=1;seed<=20000;seed++) {
    uint16_t days=buddyLifespanDays(seed);
    shortest=min(shortest,days);longest=max(longest,days);sum+=days;
  }
  assert(shortest==305 && longest==425 && sum/20000>=364 && sum/20000<=366);
  static const uint8_t anchors[]={OBJECT_GROVE,OBJECT_FLOWERS,OBJECT_JOURNAL,OBJECT_INSTRUMENT};
  static const LifeEvent events[]={EVENT_NATURE,EVENT_FLOWERS,EVENT_JOURNAL,EVENT_MUSIC};
  static const BuddyMoment moments[]={MOMENT_NATURE,MOMENT_SMELL_FLOWERS,MOMENT_JOURNAL,MOMENT_MUSIC};
  for(uint8_t i=0;i<4;i++) {
    start();uint8_t object=anchors[i];
    buddy.possessions=1<<object;buddy.currentStage=STAGE_BABY;
    buddy.lastWorldEpoch=rawEpoch;buddy.energy=700;buddy.health=900;
    uint32_t t=rawEpoch+6UL*3600UL;
    assert(momentAvailable(moments[i],t) == (object!=OBJECT_JOURNAL));
    if(object==OBJECT_JOURNAL)buddy.currentStage=STAGE_CHILD;
    assert(momentAvailable(moments[i],t));
    assert(buddyAmbitionWork(object,events[i],object)==4);
    uint16_t before=object==OBJECT_GROVE?buddy.curiosityNeed:
      object==OBJECT_FLOWERS?buddy.comfort:object==OBJECT_JOURNAL?buddy.social:buddy.play;
    liveWorldHour(t,pursuitBehavior(object),true);
    const WorldMemory &recent=buddy.events[(buddy.eventHead+23)%24];
    assert(recent.event==events[i] && recent.object==object && buddy.visits[object]==1);
    uint16_t after=object==OBJECT_GROVE?buddy.curiosityNeed:
      object==OBJECT_FLOWERS?buddy.comfort:object==OBJECT_JOURNAL?buddy.social:buddy.play;
    assert(after>before);
    buddy.currentStage=STAGE_CHILD;visibleMoment=moments[i];rawEpoch=t;
    const char *phrase=buddyMomentText();assert(phrase && strlen(phrase)>5);
    saveBuddy();uint32_t seed=buddy.seed;buddy={};loadOrCreateBuddy(t);
    assert(buddy.seed==seed && buddy.visits[object]==1 && (buddy.possessions&(1<<object)));
  }
  uint8_t livedAnchors=0, livedEvents=0;
  for(int life=0;life<8;life++) {
    start();uint32_t born=buddy.birthEpoch;
    uint32_t hours=(uint32_t)buddyLifespanDays(buddy.seed)*24UL;
    for(uint32_t hour=1;hour<=hours;hour++) {
      updateBuddySimulation(born+hour*3600UL);
      livedAnchors|=buddy.possessions;
      for(uint8_t e=0;e<buddy.eventCount;e++)
        for(uint8_t k=0;k<4;k++)if(buddy.events[e].event==events[k])livedEvents|=1<<k;
    }
    assert(!buddy.alive && buddy.totalInteractions==0);
  }
  assert((livedAnchors & 0xF0)==0xF0 && livedEvents==15);
  fakeRandomState=randomBefore;
}
static void testSleepWakeSound() {
  start();
  BuddyState before=buddy;
  assert(!takeBuddyWakeSound(preferences));
  assert(armBuddyWakeSound(preferences));
  assert(takeBuddyWakeSound(preferences));
  assert(!takeBuddyWakeSound(preferences));
  assert(!memcmp(&buddy,&before,sizeof(buddy))); // no life-save ABI change
  assert(buddyWakeSoundDue(false,true,false,false)); // RESET/PWR reboot
  assert(buddyWakeSoundDue(false,false,true,false)); // direct button wake
  assert(!buddyWakeSoundDue(false,false,false,false)); // ordinary cold boot
  assert(!buddyWakeSoundDue(true,true,false,false)); // quiet timer wake
  assert(buddyWakeSoundDue(true,true,false,true)); // optional hourly wake chime
  assert(buddySleepSoundDue(true,false)); // manual
  assert(buddySleepSoundDue(false,false)); // first auto-doze
  assert(!buddySleepSoundDue(false,true)); // hourly return is silent
  assert(!buddyUnpromptedSoundAllowed(true)); // milestones and activity stay quiet too
  assert(buddyUnpromptedSoundAllowed(false));
  failNvs=true;assert(!armBuddyWakeSound(preferences));failNvs=false;
}

static void testRuntimePowerAndMusicPolicy() {
  BuddyCriticalBattery cell;
  assert(!buddyCriticalBatteryReading(cell, 3.49f, false, 1000));
  assert(!buddyCriticalBatteryReading(cell, 3.48f, false, 1200));
  assert(cell.lowReadings == 1); // no rapid false confirmation
  assert(!buddyCriticalBatteryReading(cell, 3.51f, false, 16000));
  assert(cell.lowReadings == 0); // voltage recovery clears the sequence
  assert(!buddyCriticalBatteryReading(cell, 3.47f, false, 20000));
  assert(!buddyCriticalBatteryReading(cell, 3.48f, false, 35000));
  assert(buddyCriticalBatteryReading(cell, 3.49f, false, 50000));
  assert(!buddyCriticalBatteryReading(cell, 3.49f, true, 65000));
  assert(cell.lowReadings == 0); // verified USB host inhibits cutoff
  assert(!buddyCriticalBatteryReading(cell, 0.0f, false, 80000)); // ADC fault
  assert(buddyCanLightSleep(false,false,false,false,false,false,false,false));
  assert(!buddyCanLightSleep(true,false,false,false,false,false,false,false));
  assert(!buddyCanLightSleep(false,true,false,false,false,false,false,false));
  assert(!buddyCanLightSleep(false,false,false,true,false,false,false,false));
  assert(!buddyCanLightSleep(false,false,false,false,true,false,false,false));
  assert(!buddyCanLightSleep(false,false,false,false,false,false,true,false));
  assert(buddyMomentMakesMusic(MOMENT_HUM));
  assert(buddyMomentMakesMusic(MOMENT_SING));
  assert(buddyMomentMakesMusic(MOMENT_MUSIC));
  assert(!buddyMomentMakesMusic(MOMENT_DANCE));
  assert(!buddyMomentMakesMusic(MOMENT_TOY));
}

static void testListeningWindowPolicy() {
  BuddyListeningWindow window;
  assert(!buddyListeningWanted(window,true)); // Sound ON alone does not listen
  assert(buddyCanLightSleep(buddyListeningWanted(window,true),false,false,
                            false,false,false,false,false));
  buddyListeningTouch(window,1000); // completed BOOT gesture
  assert(buddyListeningWanted(window,true));
  assert(!buddyListeningDue(window,60999));
  assert(buddyListeningDue(window,61000));
  assert(!buddyCanLightSleep(buddyListeningWanted(window,true),false,false,
                             false,false,false,false,false));
  buddyListeningTouch(window,45000); // another BOOT gesture
  assert(!buddyListeningDue(window,104999));
  buddyListeningTouch(window,104000); // a successful voice command
  assert(!buddyListeningDue(window,163999));
  assert(buddyListeningDue(window,164000));
  BuddyListeningWindow beforeSound = window;
  assert(buddyMomentMakesMusic(MOMENT_HUM));
  BuddyTunePlan plan = buddyTunePlan(123456);
  assert(plan.clip[0] < BUDDY_SOUND_CLIPS);
  assert(window.deadlineMs == beforeSound.deadlineMs); // output doesn't open/renew input
  buddyListeningClose(window);
  assert(!buddyListeningWanted(window,true));
  assert(buddyCanLightSleep(buddyListeningWanted(window,true),false,false,
                            false,false,false,false,false));
  buddyListeningTouch(window,200000);
  assert(!buddyListeningWanted(window,false)); // Sound OFF gates input
  buddyListeningClose(window);
  buddyListeningTouch(window,0xfffffff0u);
  assert(!buddyListeningDue(window,0xfffffff1u));
  assert(buddyListeningDue(window,0xfffffff0u + BUDDY_LISTEN_WINDOW_MS));
  BuddyCriticalBattery cell;
  assert(!buddyCriticalBatteryReading(cell,3.49f,false,0));
  assert(!buddyCriticalBatteryReading(cell,3.49f,false,15000));
  assert(buddyCriticalBatteryReading(cell,3.49f,false,30000));
  assert(buddyListeningWanted(window,true)); // critical shutdown must override this
}

static void testDeadlineScheduler() {
  BuddyWorkSchedule work = {};
  work.simulationLastMs = work.climateLastMs = work.storageLastMs = 1000;
  work.simulationPeriodMs = 600000;
  work.climatePeriodMs = 120000;
  assert(buddyNextWorkDelay(1000,work) == 60000); // storage first, not 200 ms
  work.storageLastMs = 30000;
  assert(buddyNextWorkDelay(59000,work) == 31000);
  work.storageLastMs = 60000;
  work.climateLastMs = 0;
  assert(buddyNextWorkDelay(119999,work) == 1);
  assert(buddyNextWorkDelay(120000,work) == 0);
  work.simulationLastMs = 0;
  assert(buddyNextWorkDelay(600000,work) == 0); // simultaneous overdue work

  work = {};
  work.simulationPeriodMs = 600000;
  work.climatePeriodMs = 120000;
  uint32_t wrap = 0xfffffff0u;
  work.simulationLastMs = work.climateLastMs = work.storageLastMs = wrap;
  work.momentActive = true;
  work.momentAtMs = wrap + 73000;
  assert(buddyNextWorkDelay(wrap,work) == 60000);
  assert(buddyNextWorkDelay(wrap + 59000,work) == 1000);
  assert(buddyNextWorkDelay(wrap + 60001,work) == 0);
  assert(buddyDeadlineRemaining(wrap,wrap + 73000) == 73000);
  assert(buddyDeadlineRemaining(wrap + 73005,wrap + 73000) == 0);
  work = {};
  work.simulationLastMs = work.climateLastMs = work.storageLastMs = 1000;
  work.simulationPeriodMs = 600000; work.climatePeriodMs = 120000;
  work.clockRepairPending = true; work.clockRepairLastMs = 1000;
  work.clockRepairPeriodMs = 500;
  assert(buddyNextWorkDelay(1000,work) == 500); // cable-exit repair wakes itself
  assert(buddyNextWorkDelay(1499,work) == 1);
  assert(buddyNextWorkDelay(1500,work) == 0);
  work.simulationLastMs = work.climateLastMs = work.storageLastMs = 70000;
  work.clockRepairLastMs = 70000;
  work.clockRepairPeriodMs = buddyClockCheckIntervalMs(true, false);
  assert(buddyNextWorkDelay(70000,work) == 2000); // late fault needs no BOOT press
  assert(buddyNextWorkDelay(72000,work) == 0);
  work.clockRepairPending = false;
  work.simulationLastMs = work.climateLastMs = work.storageLastMs = 1000;
  work.cableReturnRepaintPending = true;
  work.cableReturnRepaintAtMs = 4000;
  assert(buddyNextWorkDelay(1000,work) == 3000);
  assert(buddyNextWorkDelay(3999,work) == 1);
  assert(buddyNextWorkDelay(4000,work) == 0);
  work.cableReturnRepaintAtMs = 6000; // retry while the clock is untrusted
  assert(buddyNextWorkDelay(4000,work) == 2000);
  assert(buddyCableReturnShouldRender(true, false));
  assert(buddyCableReturnShouldRender(true, true));
  assert(!buddyCableReturnShouldRender(false, false));
  assert(buddyCableReturnShouldRender(false, true));

  work = {};
  work.simulationLastMs = work.climateLastMs = work.storageLastMs = 5000;
  work.simulationPeriodMs = 600000; work.climatePeriodMs = 120000;
  work.lowBatteryConfirm = true; work.batteryLastMs = 5000;
  assert(buddyNextWorkDelay(5000,work) == 15000);
  assert(buddyNextWorkDelay(20000,work) == 0);
  work.lowBatteryConfirm = false;
  work.listeningWindowOpen = true; work.listeningWindowAtMs = 25000;
  assert(buddyNextWorkDelay(20000,work) == 5000);
  assert(buddyNextWorkDelay(25001,work) == 0);
  work.listeningWindowOpen = false;
  work.menuOpen = true; work.menuLastActionMs = 5000;
  assert(buddyNextWorkDelay(45000,work) == 5000);
  work.menuOpen = false;
  work.cableDeadlineActive = true; work.cableAtMs = 125000;
  assert(buddyNextWorkDelay(120000,work) == 0); // climate due first
  work.climateLastMs = 120000; work.storageLastMs = 120000;
  assert(buddyNextWorkDelay(120000,work) == 5000);
  work.cableAtMs = 130000; // connected-peer silence horizon
  assert(buddyNextWorkDelay(120000,work) == 10000);
  assert(!buddyCanLightSleep(false,false,false,true,false,false,false,false));

  BuddyListeningWindow voice;
  assert(buddyCanLightSleep(buddyListeningWanted(voice,true),false,false,
                            false,false,false,false,false)); // Sound ON idle
  assert(buddyCanLightSleep(buddyListeningWanted(voice,false),false,false,
                            false,false,false,false,false)); // Sound OFF idle
  // A BOOT GPIO wake during a long timer sleep forces gesture servicing;
  // its queued event then forbids re-entering sleep until drained.
  assert(!buddyCanLightSleep(false,false,false,false,false,false,true,false));
  assert(buddyDeadlineRemaining(1050,60000) == 58950);
}

static void testMomentDurations() {
  BuddyTraits traits = {60,60,60,60,60,60};
  for (uint32_t seed = 0; seed < 1000; ++seed) {
    for (uint8_t moment = 0; moment < MOMENT_COUNT; ++moment) {
      uint32_t duration = buddyMomentDurationMs((BuddyMoment)moment,STAGE_ADULT,
        700,700,traits,0,0,seed);
      assert(duration >= 2UL*60000UL && duration <= 45UL*60000UL);
      assert(duration == buddyMomentDurationMs((BuddyMoment)moment,STAGE_ADULT,
        700,700,traits,0,0,seed));
    }
  }
  uint32_t brief = buddyMomentDurationMs(MOMENT_LISTEN,STAGE_ADULT,700,700,traits,0,0,4);
  uint32_t rest = buddyMomentDurationMs(MOMENT_NAP,STAGE_ADULT,700,700,traits,0,0,4);
  assert(rest > brief);
  assert(buddyMomentDurationMs(MOMENT_NAP,STAGE_ELDER,200,700,traits,0,0,4) > rest);
  assert(buddyMomentDurationMs(MOMENT_BUILD,STAGE_ADULT,200,700,traits,0,0,4) <
         buddyMomentDurationMs(MOMENT_BUILD,STAGE_ADULT,700,700,traits,0,0,4));
  assert(buddyMomentDurationMs(MOMENT_MUSIC,STAGE_ADULT,700,700,traits,850,4,4) >
         buddyMomentDurationMs(MOMENT_MUSIC,STAGE_ADULT,700,700,traits,0,0,4));
}
static void testFirstStartAndSex() {
  reset();
  loadOrCreateBuddy(rawEpoch);
  assert(introRequired && buddy.magic!=BUDDY_MAGIC && buddyIntro.step==INTRO_FORM);
  assert(preferences.getBytesLength("intro-a")==sizeof(BuddyIntroState));
  assert(preferences.getBytesLength("intro-b")==sizeof(BuddyIntroState));
  currentView=VIEW_BUDDY;renderBuddy(true);
  uint8_t welcomeFrame[5000];memcpy(welcomeFrame,display->buffer,5000);
  handleBuddyIntroButton(BUTTON_SHORT);assert(buddyIntro.cursor==1);
  handleBuddyIntroButton(BUTTON_DOUBLE);assert(buddyIntro.cursor==0);
  handleBuddyIntroButton(BUTTON_SHORT); // GIRL
  handleBuddyIntroButton(BUTTON_LONG);
  assert(buddyIntro.sex==BUDDY_GIRL && buddyIntro.step==INTRO_GIFT);
  handleBuddyIntroButton(BUTTON_SHORT);
  handleBuddyIntroButton(BUTTON_SHORT); // NOOK
  // Cursor and prior selections survive a power loss in the setup flow.
  buddyIntro={};introSequence=0;activeIntroSlot=-1;
  setupBuddyIntro();
  assert(introRequired && buddyIntro.step==INTRO_GIFT && buddyIntro.cursor==OBJECT_NOOK);
  handleBuddyIntroButton(BUTTON_LONG);
  assert(buddyIntro.gift==OBJECT_NOOK && buddyIntro.step==INTRO_SPARK);
  handleBuddyIntroButton(BUTTON_SHORT); // MAKER
  handleBuddyIntroButton(BUTTON_LONG);
  assert(buddyIntro.spark==SPARK_MAKE && buddyIntro.step==INTRO_COMPLETE);
  assert(buddy.magic!=BUDDY_MAGIC); // no life before all three holds
  assert(birthBuddyFromIntro(rawEpoch));
  uint32_t seed=buddy.seed;
  assert(buddySexFromSeed(seed)==BUDDY_GIRL && buddy.birthEpoch==rawEpoch);
  assert((buddy.possessions & (1<<OBJECT_NOOK)) && buddy.visits[OBJECT_NOOK]==1);
  assert(buddy.traits.resilience>=32 && buddy.traits.independence>=25);
  assert(!introRequired && !saveFailed);
  sdAvailable=true;writeBirthRecord();
  bool sexArchived=false;
  for (const auto &file : files) if (file.first.find("identity.json")!=std::string::npos &&
      file.second.find("\"sex\":\"girl\"")!=std::string::npos) sexArchived=true;
  assert(sexArchived);
  // A saved life always wins, even if the obsolete setup record later fails.
  nvs["buddy-life/intro-a"][0]^=0xff;
  nvs["buddy-life/intro-b"][0]^=0xff;
  buddy={};introRequired=false;
  loadOrCreateBuddy(rawEpoch+86400UL);
  assert(buddy.seed==seed && buddy.birthEpoch==rawEpoch && !introRequired);
  // The same appearance signal travels in the already-supported BLE recipe.
  buddy.currentStage=STAGE_ADULT;buddy.seed=100;
  BuddyVisualGenome boy=visualGenome();assert(boy.headShape==2);
  renderBuddy(true);uint8_t boyFrame[5000];memcpy(boyFrame,display->buffer,5000);
  // A pre-welcome boy with an odd natural head shape must keep that shape.
  buddy.seed=106;
  assert(visualGenome().headShape==1);
  assert(nearbyVisualGenome().headShape==0); // v1 wire parity remains unambiguous
  assert(!(currentNearbyPacket().visual.headShape&1));
  buddy.seed=100;
  // The old home portrait had no sex caption in this otherwise empty corner.
  for (int y=6;y<16;y++) for (int x=6;x<35;x++)
    assert(!(boyFrame[y*25+x/8] & (0x80>>(x%8))));
  buddy.seed=101;
  BuddyVisualGenome girl=visualGenome();assert(girl.headShape&1);
  assert(nearbyVisualGenome().headShape&1);
  renderBuddy(true);uint8_t girlFrame[5000];memcpy(girlFrame,display->buffer,5000);
  assert(memcmp(boyFrame,girlFrame,5000));
  std::ofstream sexes(buddyTestTempPath("buddy-sexes.pbm"));sexes<<"P1\n400 200\n";
  for(int y=0;y<200;y++) {for(int x=0;x<400;x++) {
    const uint8_t *frame=x<200?boyFrame:girlFrame;int xx=x%200;
    sexes<<((frame[y*25+xx/8]>>(7-xx%8))&1)<<' ';
  } sexes<<'\n';} sexes.close();
  std::ofstream welcome(buddyTestTempPath("buddy-welcome.pbm"));welcome<<"P1\n200 200\n";
  for(int y=0;y<200;y++) {
    for(int x=0;x<200;x++) welcome<<((welcomeFrame[y*25+x/8]>>(7-x%8))&1)<<' ';
    welcome<<'\n';
  }
  welcome.close();

  // Committed setup choices and seed survive an interruption before life save.
  reset();loadOrCreateBuddy(rawEpoch);
  handleBuddyIntroButton(BUTTON_LONG); // BOY
  handleBuddyIntroButton(BUTTON_LONG); // STONE
  handleBuddyIntroButton(BUTTON_LONG); // EXPLORER
  buddyIntro.seed=0x12345678;buddyIntro.birthEpoch=rawEpoch;
  saveBuddyIntro();saveBuddyIntro();
  nvs["buddy-life/intro-a"][0]^=0xff;
  buddyIntro={};introRequired=false;introSequence=0;activeIntroSlot=-1;
  loadOrCreateBuddy(rawEpoch+60);
  assert(buddy.seed==0x12345678 && buddy.birthEpoch==rawEpoch && !introRequired);

  // A damaged partial introduction cannot silently create a replacement.
  reset();loadOrCreateBuddy(rawEpoch);
  nvs["buddy-life/intro-a"][0]^=0xff;
  nvs["buddy-life/intro-b"][0]^=0xff;
  buddyIntro={};introRequired=false;
  loadOrCreateBuddy(rawEpoch);
  assert(introRequired && introLocked && buddy.magic!=BUDDY_MAGIC);
  reset();failNvs=true;loadOrCreateBuddy(rawEpoch);
  assert(introRequired && introSaveFailed && buddy.magic!=BUDDY_MAGIC);
  failNvs=false;retryBuddyIntroSave();
  assert(!introSaveFailed && preferences.getBytesLength("intro-a")==sizeof(BuddyIntroState));
}
static void testNearby() {
  start();
  BuddyState originalLife=buddy;
  setupNearbyStorage();
  assert(!nearbyEnabled && !nearbyRadioActive && nearbyState.magic!=NEAR_MAGIC);
  buddyCableStart(cableSession,ticks);newNearbyLife();
  assert(!nearbyEnabled && nearbyState.magic==NEAR_MAGIC);
  assert(preferences.getBytesLength("near-a")==sizeof(NearbyState));
  assert(preferences.getBytesLength("near-b")==sizeof(NearbyState));
  assert(!memcmp(&buddy,&originalLife,sizeof(buddy)));
  nearbyState.selfId=100;buddy.energy=850;buddy.health=850;
  NearbyPacket peer={};peer.sender=200;peer.visual=visualGenome();peer.stage=STAGE_CHILD;
  peer.moment=MOMENT_MUSIC;peer.expression=buddyCableExpression(840,210,95);peer.girl=true;
  uint8_t bytes[NEAR_PACKET_BYTES];encodeNearbyPacket(peer,bytes);
  uint8_t cable[BUDDY_CABLE_FRAME_BYTES];encodeBuddyCableFrame(peer,cable);
  BuddyCableStream stream = {};
  NearbyPacket streamed = {};
  assert(!buddyCableStreamPush(stream, 0x00, 1000, streamed));
  for (size_t i=0;i<sizeof(cable);++i) {
    bool complete=buddyCableStreamPush(stream,cable[i],1020+i*21,streamed);
    assert(complete == (i+1==sizeof(cable)));
  }
  assert(streamed.sender==peer.sender && streamed.moment==peer.moment);
  // A partial packet cannot poison the next valid frame, including wraparound.
  for (size_t i=0;i<5;++i) assert(!buddyCableStreamPush(stream,cable[i],UINT32_MAX-100+i*21,streamed));
  for (size_t i=0;i<sizeof(cable);++i)
    assert(buddyCableStreamPush(stream,cable[i],300+i*21,streamed)==(i+1==sizeof(cable)));
  uint8_t badCable[BUDDY_CABLE_FRAME_BYTES];memcpy(badCable,cable,sizeof(cable));
  badCable[4]^=1;
  for (size_t i=0;i<sizeof(badCable);++i)
    assert(!buddyCableStreamPush(stream,badCable[i],1000+i*21,streamed));
  assert(stream.count==0);
  NearbyPacket fromCable={};
  assert(decodeBuddyCableFrame(cable,sizeof(cable),fromCable));
  assert(fromCable.sender==peer.sender);
  assert(fromCable.moment==MOMENT_MUSIC && fromCable.expression==peer.expression && fromCable.girl);
  assert(buddyCableMood(fromCable.expression)>=700);
  assert(buddyCableComfort(fromCable.expression)<300);
  assert(buddyCableCuriosity(fromCable.expression)>=66);
  assert(buddyCablePortraitChanged(NearbyPacket{},fromCable));
  NearbyPacket samePortrait=fromCable;
  samePortrait.target=42;samePortrait.confirmed=true;
  assert(!buddyCablePortraitChanged(fromCable,samePortrait)); // heartbeat/handshake is not a redraw
  samePortrait.expression^=0x20;
  assert(buddyCablePortraitChanged(fromCable,samePortrait));
  samePortrait=fromCable;samePortrait.girl=false;
  assert(buddyCablePortraitChanged(fromCable,samePortrait));
  cable[NEAR_PACKET_BYTES+1]=MOMENT_COUNT;
  cable[sizeof(cable)-1]=buddyCableCrc(cable,sizeof(cable)-1);
  assert(!decodeBuddyCableFrame(cable,sizeof(cable),fromCable));
  encodeBuddyCableFrame(peer,cable);
  cable[4]^=1;assert(!decodeBuddyCableFrame(cable,sizeof(cable),fromCable));
  encodeBuddyCableFrame(peer,cable);
  assert(!decodeBuddyCableFrame(cable,sizeof(cable)-1,fromCable));
  cable[0]=0;assert(!decodeBuddyCableFrame(cable,sizeof(cable),fromCable));
  encodeBuddyCableFrame(peer,cable);
  assert(decodeBuddyCableFrame(cable,sizeof(cable),fromCable));
  NearbyPacket decoded={};assert(decodeNearbyPacket(bytes,sizeof(bytes),decoded));
  assert(decoded.sender==peer.sender && decoded.target==0 && decoded.stage==peer.stage);
  assert(!memcmp(&decoded.visual,&peer.visual,sizeof(peer.visual)));
  assert(!decodeNearbyPacket(bytes,sizeof(bytes)-1,decoded));
  bytes[10]|=0x04;assert(!decodeNearbyPacket(bytes,sizeof(bytes),decoded));
  encodeNearbyPacket(peer,bytes);bytes[11]=(bytes[11]&~7)|7;
  assert(!decodeNearbyPacket(bytes,sizeof(bytes),decoded));
  // The lower pseudonym invites; an unrelated or unconfirmed beacon cannot
  // create a shared scene or alter Buddy's social state.
  observeNearbyPacket(fromCable,-40);
  assert(nearbyEncounter.phase==NEAR_INVITING && currentNearbyPacket().target==200);
  uint8_t activity=nearbyEncounter.activity;
  uint16_t socialBefore=buddy.social;
  peer.target=999;peer.activity=activity;peer.confirmed=true;
  observeNearbyPacket(peer,-55);assert(nearbyEncounter.phase==NEAR_INVITING);
  assert(buddy.social==socialBefore && !nearbyContact(200));
  peer.target=100;observeNearbyPacket(peer,-55);
  assert(nearbyEncounter.phase==NEAR_SHARED && nearbySceneVisible());
  assert(nearbyContact(200) && nearbyContact(200)->meetings==1);
  assert(buddy.social>socialBefore);
  observeNearbyPacket(peer,-55);assert(nearbyContact(200)->meetings==1);
  currentView=VIEW_BUDDY;renderBuddy(true);
  uint8_t sharedFrame[5000];memcpy(sharedFrame,display->buffer,5000);
  std::ofstream encounterImage(buddyTestTempPath("buddy-encounter.pbm"));
  encounterImage<<"P1\n200 200\n";
  for(int y=0;y<200;y++) {
    for(int x=0;x<200;x++) encounterImage<<((sharedFrame[y*25+x/8]>>(7-x%8))&1)<<' ';
    encounterImage<<'\n';
  }
  encounterImage.close();
  ticks+=NEAR_SCENE_MS+1;serviceNearbyEncounter();
  assert(!nearbySceneVisible() && nearbyEncounter.sceneClosed);
  renderBuddy(true);assert(memcmp(sharedFrame,display->buffer,5000));
  buddyCableStop(cableSession);buddyCableStart(cableSession,ticks);
  assert(buddyCableObservePeer(cableSession,peer.sender,nearbyState.selfId,ticks));
  cableScenePeer=peer;cableSceneActivity=NEAR_STORY;cableSceneMoment=MOMENT_MUSIC;
  cableGreetingAwaitingPaint=true;cableGreetingUntilMs=0;
  assert(nearbySceneVisible());
  renderBuddy(true);
  assert(!cableGreetingAwaitingPaint &&
         buddyCableGreetingVisible(false,cableGreetingUntilMs,ticks));
  uint8_t pairedFrame[5000];memcpy(pairedFrame,display->buffer,5000);
  int localPixels=0,peerPixels=0;
  for(int y=25;y<140;y++)for(int x=2;x<198;x++) {
    if((pairedFrame[y*25+x/8]>>(7-x%8))&1) (x<95?localPixels:peerPixels)++;
  }
  assert(localPixels>400 && peerPixels>400);
  cableScenePeer.visual.earStyle=(cableScenePeer.visual.earStyle+1)%6;
  cableScenePeer.visual.markingStyle=(cableScenePeer.visual.markingStyle+2)%6;
  renderBuddy(true);
  int localChanges=0,peerChanges=0;
  for(int y=25;y<140;y++)for(int x=2;x<198;x++) {
    bool before=(pairedFrame[y*25+x/8]>>(7-x%8))&1;
    bool after=(display->buffer[y*25+x/8]>>(7-x%8))&1;
    if(before!=after) (x<95?localChanges:peerChanges)++;
  }
  assert(!localChanges && peerChanges>50); // peer genome changes only his full portrait
  cableScenePeer=peer;
  cableScenePeer.expression=buddyCableExpression(120,900,20);
  renderBuddy(true);
  localChanges=peerChanges=0;
  for(int y=25;y<140;y++)for(int x=2;x<198;x++) {
    bool before=(pairedFrame[y*25+x/8]>>(7-x%8))&1;
    bool after=(display->buffer[y*25+x/8]>>(7-x%8))&1;
    if(before!=after) (x<95?localChanges:peerChanges)++;
  }
  assert(!localChanges && peerChanges>30); // live peer mood changes his face, not ours
  std::ofstream cableImage(buddyTestTempPath("buddy-cable-pair.pbm"));
  cableImage<<"P1\n200 200\n";
  for(int y=0;y<200;y++) {
    for(int x=0;x<200;x++) cableImage<<((pairedFrame[y*25+x/8]>>(7-x%8))&1)<<' ';
    cableImage<<'\n';
  }
  cableImage.close();
  int fullBefore=display->fullUpdates;
  renderCableFarewell(true);
  assert(display->fullUpdates==fullBefore+1);
  assert(memcmp(pairedFrame,display->buffer,5000)!=0);
  std::ofstream farewellImage(buddyTestTempPath("buddy-cable-farewell.pbm"));
  farewellImage<<"P1\n200 200\n";
  for(int y=0;y<200;y++) {
    for(int x=0;x<200;x++) farewellImage<<((display->buffer[y*25+x/8]>>(7-x%8))&1)<<' ';
    farewellImage<<'\n';
  }
  farewellImage.close();
  cableScenePeer={};cableSceneMoment=MOMENT_WATCH;buddyCableStop(cableSession);
  cableGreetingUntilMs=0;
  uint32_t self=nearbyState.selfId;
  nearbyState={};nearbySequence=0;activeNearbySlot=-1;
  setupNearbyStorage();
  assert(nearbyState.selfId==self && nearbyContact(200)->meetings==1);
  nvs["buddy-life/near-a"][0]^=0xff;
  nearbyState={};setupNearbyStorage();
  assert(!nearbyStorageLocked && nearbyContact(200));
  nvs["buddy-life/near-b"][0]^=0xff;
  nearbyState={};setupNearbyStorage();
  assert(nearbyStorageLocked); // history is paused, never silently replaced
  buddyCableStop(cableSession);assert(!nearbyDiscoveryActive());

  // Opposite side: a larger pseudonym accepts a targeted invitation, but
  // records nothing until the inviter's confirmation returns.
  start();buddyCableStart(cableSession,ticks);newNearbyLife();
  nearbyState.selfId=200;buddy.energy=850;buddy.health=850;
  peer={};peer.sender=100;peer.target=200;peer.visual=visualGenome();
  peer.stage=STAGE_ADULT;peer.activity=NEAR_PLAY;
  observeNearbyPacket(peer,-60);
  assert(nearbyEncounter.phase==NEAR_ACCEPTING && !nearbyContact(100));
  assert(currentNearbyPacket().confirmed && currentNearbyPacket().target==100);
  peer.confirmed=true;observeNearbyPacket(peer,-60);
  assert(nearbyEncounter.phase==NEAR_SHARED && nearbyContact(100)->meetings==1);
  buddyCableStop(cableSession);assert(!nearbyDiscoveryActive());
}

static void testCableMomentRepertoire() {
  start();
  buddy.currentStage=STAGE_ADULT;buddy.energy=850;buddy.health=850;buddy.mood=820;
  buddy.possessions=(1u<<OBJECT_COUNT)-1;buddy.eventCount=5;
  latestClimateValid=true;latestTempC=22;
  uint32_t now=buddyNow();buddy.lastAttentionEpoch=now;
  bool seen[MOMENT_COUNT]={};
  for(uint32_t roll=0;roll<4096;roll++)
    seen[chooseCableMoment(now,roll,MOMENT_GARDEN)]=true;
  uint8_t available=0;
  for(uint8_t i=0;i<MOMENT_COUNT;i++) {
    if(!momentAvailable((BuddyMoment)i,now))continue;
    available++;
    assert(seen[i]);
    assert(cableActivityForMoment((BuddyMoment)i)>NEAR_NONE);
    assert(strlen(buddyMomentText((BuddyMoment)i))>0);
  }
  assert(available>30 && seen[MOMENT_MUSIC] && seen[MOMENT_SMELL_FLOWERS]);
}

static void testInnerLife() {
  start();
  uint32_t now = rawEpoch + 20UL * 86400UL;
  buddy.currentStage = STAGE_ADULT;
  buddy.energy = buddy.health = buddy.security = buddy.play = buddy.curiosityNeed = 800;
  buddy.social = 100;
  buddy.mood = 700;
  buddy.possessions = 0;
  buddy.favoriteObject = OBJECT_COUNT;
  buddy.traits.sociability = 90;
  buddy.traits.independence = 10;
  auto lonely = buddyInnerStateOf(buddy, now);
  assert(lonely.concern == CONCERN_LONELY && lonely.pressure >= 700);
  assert(buddyInnerBehaviorBias(buddy, lonely, BEHAVIOR_SELF_SOOTHE) >
         buddyInnerBehaviorBias(buddy, lonely, BEHAVIOR_PLAY));
  assert(chooseBehavior(12, now) == BEHAVIOR_SELF_SOOTHE);
  visibleMoment = MOMENT_DANCE;
  assert(!strcmp(visibleMoodName(now), "LONELY")); // actual feeling beats a playful pose
  int humming = momentScore(MOMENT_HUM, now, now / 600UL);
  BuddyState lonelyStart = buddy;
  buddy.social = 800;
  assert(humming > momentScore(MOMENT_HUM, now, now / 600UL) + 400);
  buddy = lonelyStart;

  int selfSoothingBefore = buddy.behaviorLearning[BEHAVIOR_SELF_SOOTHE];
  performBehaviorHour(BEHAVIOR_SELF_SOOTHE, now);
  assert(buddy.social > lonelyStart.social);
  assert(buddy.behaviorLearning[BEHAVIOR_SELF_SOOTHE] > selfSoothingBefore);
  buddy = lonelyStart;
  performBehaviorHour(BEHAVIOR_OBSERVE, now);
  assert(buddy.behaviorLearning[BEHAVIOR_OBSERVE] < lonelyStart.behaviorLearning[BEHAVIOR_OBSERVE]);

  // A cherished toy becomes an effective private comfort; without that bond,
  // the same play hour does not relieve the same loneliness.
  buddy = lonelyStart;
  performBehaviorHour(BEHAVIOR_PLAY, now);
  int unattachedSocial = buddy.social;
  buddy = lonelyStart;
  buddy.possessions |= 1 << OBJECT_TOY;
  buddy.favoriteObject = OBJECT_TOY;
  buddy.affection[OBJECT_TOY] = 900;
  performBehaviorHour(BEHAVIOR_PLAY, now);
  assert(buddy.social >= unattachedSocial + 25);
  assert(buddy.behaviorLearning[BEHAVIOR_PLAY] > lonelyStart.behaviorLearning[BEHAVIOR_PLAY]);
  uint32_t identity = buddy.seed;
  int learned = buddy.behaviorLearning[BEHAVIOR_PLAY];
  saveBuddy(); buddy = {}; loadOrCreateBuddy(now);
  assert(buddy.seed == identity && buddy.behaviorLearning[BEHAVIOR_PLAY] == learned &&
         buddy.affection[OBJECT_TOY] == 900); // no save ABI change or lost individuality

  buddy = lonelyStart;
  for (int hour = 0; hour < 20; hour++) performBehaviorHour(BEHAVIOR_SELF_SOOTHE, now + hour * 3600UL);
  assert(buddyInnerStateOf(buddy, now + 20UL * 3600UL).concern == CONCERN_NONE);

  // Personality and age alter what feels urgent and how Buddy responds.
  buddy = lonelyStart; buddy.social = 420;
  assert(buddyInnerStateOf(buddy, now).concern == CONCERN_LONELY);
  buddy.traits.sociability = 20; buddy.traits.independence = 80;
  assert(buddyInnerStateOf(buddy, now).concern == CONCERN_NONE);
  buddy.curiosityNeed = 120; buddy.traits.curiosity = 90;
  auto restless = buddyInnerStateOf(buddy, now);
  assert(restless.concern == CONCERN_RESTLESS);
  assert(buddyInnerBehaviorBias(buddy, restless, BEHAVIOR_EXPLORE) >
         buddyInnerBehaviorBias(buddy, restless, BEHAVIOR_OBSERVE));
  buddy.currentStage = STAGE_ELDER;
  assert(buddyInnerBehaviorBias(buddy, restless, BEHAVIOR_OBSERVE) >
         buddyInnerBehaviorBias(buddy, restless, BEHAVIOR_EXPLORE));

  buddy = lonelyStart; buddy.social = 800; buddy.security = 120;
  auto uneasy = buddyInnerStateOf(buddy, now);
  assert(uneasy.concern == CONCERN_UNEASY);
  assert(buddyInnerBehaviorBias(buddy, uneasy, BEHAVIOR_SELF_SOOTHE) >
         buddyInnerBehaviorBias(buddy, uneasy, BEHAVIOR_EXPLORE));
  buddy.security = 800; buddy.play = 100;
  auto bored = buddyInnerStateOf(buddy, now);
  assert(bored.concern == CONCERN_BORED);
  assert(buddyInnerMomentBias(bored, MOMENT_DANCE) > buddyInnerMomentBias(bored, MOMENT_WATCH));
  buddy.play = 800; buddy.energy = 100;
  auto exhausted = buddyInnerStateOf(buddy, now);
  assert(exhausted.concern == CONCERN_EXHAUSTED);
  assert(buddyInnerBehaviorBias(buddy, exhausted, BEHAVIOR_SLEEP) >
         buddyInnerBehaviorBias(buddy, exhausted, BEHAVIOR_EXPLORE));
  buddy.energy = 800; buddy.health = 200;
  assert(buddyInnerStateOf(buddy, now).concern == CONCERN_EXHAUSTED);
  int illnessNeed = buddyInnerNeed(buddy, CONCERN_EXHAUSTED);
  performBehaviorHour(BEHAVIOR_REST, now);
  assert(buddyInnerNeed(buddy, CONCERN_EXHAUSTED) == illnessNeed);
  assert(buddy.behaviorLearning[BEHAVIOR_REST] < 0); // energy alone did not heal illness

  // Home loss is a finite, attachment-sensitive episode derived from the
  // existing event ring; it shapes both action and expression after reboot.
  buddy = lonelyStart; buddy.social = 800;
  buddy.possessions |= 1 << OBJECT_NOOK;
  buddy.favoriteObject = OBJECT_NOOK;
  buddy.affection[OBJECT_NOOK] = 900;
  rememberWorldEvent(now, EVENT_HOUSE_COLLAPSED, OBJECT_NOOK, BEHAVIOR_REST, -65);
  auto grieving = buddyInnerStateOf(buddy, now);
  assert(grieving.concern == CONCERN_GRIEVING && grieving.pressure >= 700);
  assert(buddyInnerMomentBias(grieving, MOMENT_WATCH) > buddyInnerMomentBias(grieving, MOMENT_DANCE));
  visibleMoment = MOMENT_WATCH;
  assert(!strcmp(visibleMoodName(now), "MISSING HOME"));
  BuddyState beforeReflection = buddy;
  buddy.traits.sensitivity = 90;
  performBehaviorHour(BEHAVIOR_DAYDREAM, now);
  assert(buddy.security > beforeReflection.security &&
         buddy.behaviorLearning[BEHAVIOR_DAYDREAM] > beforeReflection.behaviorLearning[BEHAVIOR_DAYDREAM]);
  buddy = beforeReflection;
  saveBuddy(); buddy = {}; loadOrCreateBuddy(now);
  assert(buddyInnerStateOf(buddy, now).concern == CONCERN_GRIEVING);
  assert(buddyInnerStateOf(buddy, now + 5UL * 86400UL).concern == CONCERN_NONE);
}

static void testInnerLifeUnattended() {
  start();
  uint32_t seed = buddy.seed;
  unsigned concerns[CONCERN_GRIEVING + 1] = {};
  for (uint32_t hour = 1; hour <= 21UL * 24UL; hour++) {
    uint32_t epoch = rawEpoch + hour * 3600UL;
    updateBuddySimulation(epoch);
    BuddyInnerState inner = buddyInnerStateOf(buddy, epoch);
    concerns[inner.concern]++;
  }
  unsigned differentConcerns = 0;
  for (unsigned i = 1; i <= CONCERN_GRIEVING; i++) if (concerns[i]) differentConcerns++;
  assert(concerns[CONCERN_NONE] > 0 && differentConcerns >= 2);
  assert(buddy.alive && buddy.seed == seed && buddy.totalInteractions == 0);
}

static void testAmbitions() {
  start();
  assert(ambitionForCurrentLife() && buddyAmbition.kind == OBJECT_COUNT &&
         !ambitionLocked && !ambitionSaveFailed);
  assert(buddyAmbitionWork(OBJECT_PLANT, EVENT_WATERED, OBJECT_PLANT) == 4);
  assert(!buddyAmbitionWork(OBJECT_PLANT, EVENT_PLAYED, OBJECT_TOY));
  uint32_t t = rawEpoch + 16UL * 86400UL;
  buddy.currentStage = STAGE_CHILD;
  buddy.possessions = 1 << OBJECT_PLANT;
  buddy.visits[OBJECT_PLANT] = 8;
  buddy.affection[OBJECT_PLANT] = 650;
  buddy.energy = buddy.health = 850;
  serviceBuddyAmbition(t);
  assert(buddyAmbition.kind == OBJECT_PLANT && buddyAmbition.startedEpoch == t);
  int goalMoment = momentScore(MOMENT_GARDEN,t,t/600UL);
  buddyAmbition.kind = OBJECT_COUNT;
  assert(goalMoment - momentScore(MOMENT_GARDEN,t,t/600UL) == 160);
  buddyAmbition.kind = OBJECT_PLANT;
  visibleMoment = MOMENT_GARDEN;
  rawEpoch = t;
  assert(!strcmp(buddyMomentText(),"GROWING A LITTLE GARDEN"));
  uint16_t before = buddy.mood;
  rememberWorldEvent(t, EVENT_WATERED, OBJECT_PLANT, BEHAVIOR_OBSERVE, 40);
  assert(buddyAmbition.progress == 4);
  rememberWorldEvent(t+3600UL, EVENT_GARDENED, OBJECT_PLANT, BEHAVIOR_OBSERVE, 40);
  assert(buddyAmbition.progress == 4); // Actual work, at most once a day.
  saveBuddy();
  uint32_t seed = buddy.seed;
  buddy = {}; buddyAmbition = {};
  loadOrCreateBuddy(t); setupBuddyAmbition(t);
  assert(buddy.seed == seed && buddyAmbition.kind == OBJECT_PLANT && buddyAmbition.progress == 4);
  for (uint32_t day = 1; day < 24; day++)
    rememberWorldEvent(t+day*86400UL, EVENT_WATERED, OBJECT_PLANT, BEHAVIOR_OBSERVE, 40);
  assert(buddyAmbition.kind == OBJECT_COUNT && buddyAmbition.lastOutcome == AMBITION_FULFILLED &&
         buddyAmbition.lastKind == OBJECT_PLANT && buddyAmbition.lastEndedEpoch >= t+23UL*86400UL);
  assert(buddy.mood >= before && buddy.affection[OBJECT_PLANT] > 650 && buddySpecialPending);
  assert(buddyAmbition.lastEndedEpoch - t >= 21UL*86400UL); // Not a short project.
  assert(momentAvailable(MOMENT_CELEBRATE,buddyAmbition.lastEndedEpoch));
  assert(!strcmp(visibleMoodName(buddyAmbition.lastEndedEpoch),"PROUD"));
  char story[90];
  describeWorldEvent(buddy.events[(buddy.eventHead+23)%24],story,sizeof(story));
  assert(strstr(story,"FULFILLED A LONG DREAM"));
  assert(buddy.totalInteractions == 0);

  // A completed parent's wish becomes one influence on the child, while
  // the child's own traits remain capable of selecting another path.
  buddy.alive = false;
  buddy.deathEpoch = t+40UL*86400UL;
  buddy.prior.generation = buddy.generation;
  buddy.prior.traits = buddy.traits;
  buddy.prior.dominantBehavior = BEHAVIOR_OBSERVE;
  buddy.prior.deathEpoch = buddy.deathEpoch;
  saveBuddy();
  createBuddyLife(buddy.deathEpoch+86401UL,true);
  assert(buddy.generation == 2 && buddyAmbition.generation == 2 &&
         buddyAmbition.legacyKind == OBJECT_PLANT && buddyAmbition.legacyStrength == 2 &&
         buddyAmbition.kind == OBJECT_COUNT);
  buddy.currentStage = STAGE_CHILD;
  buddy.possessions = (1 << OBJECT_COUNT)-1;
  for (uint8_t i = 0; i < OBJECT_COUNT; i++) {
    buddy.visits[i] = 5;
    buddy.affection[i] = 60;
  }
  buddy.affection[OBJECT_STONE] = 900;
  buddy.traits = {95,5,5,5,5,95};
  assert(chooseBuddyAmbition(buddy.birthEpoch+16UL*86400UL) == OBJECT_STONE);

  // A stagnant ambition can be released when a different interest becomes
  // more compelling, without waiting for the user to change anything.
  start(); t = rawEpoch+16UL*86400UL;
  buddy.currentStage = STAGE_CHILD;
  buddy.possessions = (1 << OBJECT_STONE) | (1 << OBJECT_PLANT);
  buddy.visits[OBJECT_PLANT] = buddy.visits[OBJECT_STONE] = 5;
  buddy.affection[OBJECT_PLANT] = 900;
  buddy.affection[OBJECT_STONE] = 50;
  buddy.traits = {5,5,5,5,95,5};
  buddy.energy = buddy.health = 850;
  serviceBuddyAmbition(t);
  assert(buddyAmbition.kind == OBJECT_PLANT);
  buddy.affection[OBJECT_PLANT] = 5;
  buddy.affection[OBJECT_STONE] = 900;
  buddy.traits = {95,5,5,5,5,95};
  serviceBuddyAmbition(t+30UL*86400UL);
  assert(buddyAmbition.kind == OBJECT_COUNT && buddyAmbition.lastOutcome == AMBITION_ABANDONED);
  assert(buddyAmbition.lastKind == OBJECT_PLANT);
  serviceBuddyAmbition(t+31UL*86400UL);
  assert(buddyAmbition.kind == OBJECT_COUNT); // Deliberate recovery interval.
  serviceBuddyAmbition(t+45UL*86400UL);
  assert(buddyAmbition.kind == OBJECT_STONE);

  // The two new slots recover independently of the unchanged life save.
  start(); t = rawEpoch+16UL*86400UL;
  buddy.currentStage = STAGE_CHILD; buddy.possessions = 1 << OBJECT_PLANT;
  buddy.visits[OBJECT_PLANT] = 5; buddy.affection[OBJECT_PLANT] = 500;
  serviceBuddyAmbition(t);
  rememberWorldEvent(t, EVENT_WATERED, OBJECT_PLANT, BEHAVIOR_OBSERVE, 40);
  assert(buddyAmbition.progress == 4);
  nvs[std::string("buddy-life/")+(activeAmbitionSlot==0?"aim-a":"aim-b")][20] ^= 0x20;
  buddyAmbition = {}; setupBuddyAmbition(t);
  assert(!ambitionLocked && buddyAmbition.kind == OBJECT_PLANT && buddyAmbition.progress == 0);
  nvs["buddy-life/aim-a"] = {1}; nvs["buddy-life/aim-b"] = {2};
  buddyAmbition = {}; setupBuddyAmbition(t);
  assert(ambitionLocked && buddy.magic == BUDDY_MAGIC && buddy.alive);
  start(); t = rawEpoch+16UL*86400UL;
  buddy.currentStage = STAGE_CHILD; buddy.possessions = 1 << OBJECT_PLANT;
  buddy.visits[OBJECT_PLANT] = 5; buddy.affection[OBJECT_PLANT] = 500;
  serviceBuddyAmbition(t);
  failNvs = true;
  rememberWorldEvent(t, EVENT_WATERED, OBJECT_PLANT, BEHAVIOR_OBSERVE, 40);
  assert(ambitionSaveFailed);
  failNvs = false; saveBuddyAmbition(); assert(!ambitionSaveFailed);

  // Unattended life must form and advance a long goal through normal choices.
  start(); uint32_t born = buddy.birthEpoch;
  bool formed = false, worked = false, fulfilled = false;
  for (uint32_t hour = 1; hour <= 24UL*150UL; hour++) {
    updateBuddySimulation(born+hour*3600UL);
    formed |= buddyAmbition.kind < OBJECT_COUNT;
    worked |= buddyAmbition.progress > 0;
    fulfilled |= buddyAmbition.lastOutcome == AMBITION_FULFILLED;
  }
  assert(formed && worked && fulfilled && buddy.totalInteractions == 0);
  bool leftMark = false;
  for (uint8_t i = 0; i < OBJECT_COUNT; i++) leftMark |= buddyLegacy.condition[i] > 0;
  assert(leftMark); // A real unattended fulfillment changed the family site.
}

static void testItemArt() {
  // Each life gets a repeatable item design, including after a save/reload.
  assert(buddyItemStyle(0x5c95d946UL,1,OBJECT_TOY)==3);
  assert(buddyItemStyle(0x9b1500bcUL,1,OBJECT_TOY)==0);
  assert(buddyItemStyle(0x5c95d946UL,2,OBJECT_TOY)==5);
  assert(buddyItemStyle(0x9b1500bcUL,1,OBJECT_PLANT)==1);
  bool toyStyles[12]={}, plantStyles[4]={}, flowerStyles[4]={};
  for (uint32_t seed=1; seed<300; ++seed) {
    toyStyles[buddyItemStyle(seed,1,OBJECT_TOY)]=true;
    plantStyles[buddyItemStyle(seed,1,OBJECT_PLANT)]=true;
    flowerStyles[buddyItemStyle(seed,1,OBJECT_FLOWERS)]=true;
  }
  for (bool used:toyStyles) assert(used);
  for (bool used:plantStyles) assert(used);
  for (bool used:flowerStyles) assert(used);
  static const uint8_t otherKinds[] = {
    OBJECT_STONE, OBJECT_NOOK, OBJECT_GROVE, OBJECT_JOURNAL, OBJECT_INSTRUMENT
  };
  uint8_t frames[25][5000]={};
  for (uint8_t i=0;i<25;i++) {
    uint8_t kind=i<12 ? OBJECT_TOY : i<16 ? OBJECT_PLANT :
      i<20 ? OBJECT_FLOWERS : otherKinds[i-20];
    uint8_t style=i<12 ? i : i<20 ? (i-12)%4 : 0;
    display->EPD_Clear();
    drawItemArt(kind,40,30,3,style);
    memcpy(frames[i],display->buffer,5000);
    int pixels=0;
    for (int y=30;y<78;y++) for (int x=40;x<88;x++)
      pixels+=(frames[i][y*25+x/8]>>(7-x%8))&1;
    assert(pixels>80 && pixels<2000);
  }
  for (int i=0;i<25;i++) for (int j=i+1;j<25;j++) {
    if (memcmp(frames[i],frames[j],5000)==0)
      fprintf(stderr,"identical item art %d and %d\n",i,j);
    assert(memcmp(frames[i],frames[j],5000)!=0);
  }
  std::ofstream image(buddyTestTempPath("buddy-items.pbm"));
  image<<"P1\n1500 60\n";
  for (int y=0;y<60;y++) {
    for (int i=0;i<25;i++) for (int x=0;x<60;x++) {
      int sx=x+34, sy=y+24;
      image<<(sx<200 && sy<200 ? (frames[i][sy*25+sx/8]>>(7-sx%8))&1 : 0)<<' ';
    }
    image<<'\n';
  }
  start(); currentView=VIEW_BUDDY;
  const BuddyMoment displayMoments[] = {
    MOMENT_STONE, MOMENT_PLANT, MOMENT_NOOK, MOMENT_TOY,
    MOMENT_NATURE, MOMENT_SMELL_FLOWERS, MOMENT_JOURNAL, MOMENT_MUSIC,
    MOMENT_WATER, MOMENT_MEND
  };
  uint8_t scene[10][5000]={};
  for (int i=0;i<10;i++) {
    visibleMoment=displayMoments[i]; visibleBehavior=behaviorForMoment(visibleMoment);
    renderBuddy(true); memcpy(scene[i],display->buffer,5000);
    int iconPixels=0;
    for (int y=90;y<138;y++) for (int x=149;x<197;x++)
      iconPixels+=(scene[i][y*25+x/8]>>(7-x%8))&1;
    assert(iconPixels>80); // the scene actually uses the new object art
  }
  assert(memcmp(scene[1],scene[8],5000)!=0); // watering adds a cue
  assert(memcmp(scene[3],scene[9],5000)!=0); // mending adds a cue
  std::ofstream world("/tmp/buddy-world-review.pbm");
  world<<"P1\n400 200\n";
  for (int y=0;y<200;y++) {
    for (int view : {3,5}) for (int x=0;x<200;x++)
      world<<((scene[view][y*25+x/8]>>(7-x%8))&1)<<' ';
    world<<'\n';
  }
}

static void testLegacyWorks() {
  start();
  assert(!sdAvailable && !legacyLocked && !legacySaveFailed &&
         buddyLegacy.magic == BUDDY_LEGACY_MAGIC);
  uint32_t t = rawEpoch + 25UL*86400UL;
  buddy.currentStage = STAGE_CHILD;
  buddyHouse.phase = HOUSE_EMPTY;
  visibleMoment = MOMENT_WATCH;
  currentView = VIEW_BUDDY;
  renderBuddy(true);
  uint8_t plain[5000]; memcpy(plain,display->buffer,sizeof(plain));
  uint8_t markFrames[OBJECT_COUNT][5000] = {};
  for (uint8_t kind = 0; kind < OBJECT_COUNT; kind++) {
    buddyLegacyComplete(t,kind);
    assert(buddyLegacy.condition[kind] >= BUDDY_LEGACY_NEW_CONDITION &&
           buddyLegacy.makerGeneration[kind] == buddy.generation);
    visibleMoment = kind == OBJECT_STONE ? MOMENT_STONE : kind == OBJECT_PLANT ? MOMENT_PLANT :
      kind == OBJECT_NOOK ? MOMENT_NOOK : kind == OBJECT_TOY ? MOMENT_TOY :
      kind == OBJECT_GROVE ? MOMENT_NATURE : kind == OBJECT_FLOWERS ? MOMENT_SMELL_FLOWERS :
      kind == OBJECT_JOURNAL ? MOMENT_JOURNAL : MOMENT_MUSIC;
    renderBuddy(true);
    memcpy(markFrames[kind],display->buffer,5000);
    bool different = false;
    for (int y=64; y<88; y++) for (int x=5; x<34; x++) {
      uint8_t mask=0x80>>(x%8);
      different |= ((plain[y*25+x/8]^display->buffer[y*25+x/8]) & mask) != 0;
    }
    assert(different); // All four lasting pieces have on-screen silhouettes.
  }
  for (uint8_t i=0; i<OBJECT_COUNT; i++) for (uint8_t j=i+1; j<OBJECT_COUNT; j++) {
    bool distinct = false;
    for (int y=64; y<88; y++) for (int x=5; x<34; x++) {
      uint8_t mask=0x80>>(x%8);
      distinct |= ((markFrames[i][y*25+x/8]^markFrames[j][y*25+x/8]) & mask) != 0;
    }
    if (!distinct) fprintf(stderr,"indistinguishable legacy works %u and %u\n",i,j);
    assert(distinct);
  }
  buddyHouse.phase = HOUSE_RUIN;
  visibleMoment = MOMENT_PLANT;
  renderBuddy(true);
  for (int y=64; y<88; y++) for (int x=5; x<34; x++) {
    uint8_t mask=0x80>>(x%8);
    assert(!((markFrames[OBJECT_PLANT][y*25+x/8]^display->buffer[y*25+x/8]) & mask));
  }
  buddyHouse.phase = HOUSE_EMPTY;
  visibleMoment = MOMENT_WATCH;
  renderBuddy(true);
  std::ofstream legacyImage(buddyTestTempPath("buddy-legacy.pbm"));
  legacyImage << "P1\n1800 200\n";
  for (int y=0; y<200; y++) {
    for (int frame=0;frame<9;frame++) {
      const uint8_t *pixels=frame==0?plain:markFrames[frame-1];
      for (int x=0; x<200; x++) legacyImage << ((pixels[y*25+x/8]>>(7-x%8))&1) << ' ';
    }
    legacyImage << '\n';
  }
  legacyImage.close();
  assert(!buddyHouse.work && buddyHouse.phase == HOUSE_EMPTY); // House not required.
  BuddyLegacyState made = buddyLegacy;
  uint32_t seed = buddy.seed;
  saveBuddy(); buddyLegacy = {}; buddy = {};
  loadOrCreateBuddy(t); setupBuddyLegacy(t);
  assert(!memcmp(&buddyLegacy.condition,&made.condition,sizeof(made.condition)) &&
         buddy.seed == seed && !legacyLocked);

  buddy.possessions |= 1 << OBJECT_PLANT;
  buddy.affection[OBJECT_PLANT] = 250;
  buddy.visits[OBJECT_PLANT] = 9;
  buddy.comfort = 500;
  uint8_t plantCondition = buddyLegacy.condition[OBJECT_PLANT];
  rememberWorldEvent(t+86400UL, EVENT_WATERED, OBJECT_PLANT, BEHAVIOR_OBSERVE, 35);
  assert(buddyLegacy.condition[OBJECT_PLANT] == plantCondition+1 && buddy.comfort > 500);
  uint16_t comfort = buddy.comfort;
  rememberWorldEvent(t+86400UL+3600UL, EVENT_GARDENED, OBJECT_PLANT, BEHAVIOR_OBSERVE, 35);
  assert(buddyLegacy.condition[OBJECT_PLANT] == plantCondition+1 && buddy.comfort == comfort);
  advanceBuddyLegacyTo(t+7UL*86400UL);
  assert(buddyLegacy.condition[OBJECT_PLANT] == plantCondition); // Weather and care compete.
  buddyLegacyComplete(t+8UL*86400UL,OBJECT_PLANT);
  assert(buddyLegacy.condition[OBJECT_PLANT] == 100); // Renewed by a later work.
  advanceBuddyLegacyTo(t+300UL*86400UL);
  assert(buddyLegacy.condition[OBJECT_PLANT] > 0); // Survives into another life.

  buddy.alive = false;
  buddy.deathEpoch = t+365UL*86400UL;
  buddy.prior.generation = buddy.generation;
  buddy.prior.traits = buddy.traits;
  buddy.prior.dominantBehavior = BEHAVIOR_OBSERVE;
  buddy.prior.deathEpoch = buddy.deathEpoch;
  saveBuddy();
  createBuddyLife(buddy.deathEpoch+86401UL,true);
  uint32_t childBorn = buddy.birthEpoch;
  advanceBuddyLegacyTo(childBorn);
  assert(buddy.generation == 2 && buddyLegacy.makerGeneration[OBJECT_PLANT] == 1 &&
         legacyAvailable(OBJECT_PLANT));
  uint32_t invitationDay = childBorn + 25UL * 86400UL;
  buddy.currentStage = STAGE_CHILD;
  buddy.possessions = 1 << OBJECT_PLANT;
  buddy.inheritedObject = buddy.favoriteObject = OBJECT_PLANT;
  buddy.traits.sensitivity = 90;
  buddy.affection[OBJECT_PLANT] = 700;
  buddy.visits[OBJECT_PLANT] = 20;
  assert(buddyLegacyInvitation(buddy,buddyLegacy,invitationDay) == OBJECT_PLANT);
  assert(buddyLegacyInvitation(buddy,buddyLegacy,invitationDay) == OBJECT_PLANT);
  rawEpoch=invitationDay;clockTrusted=clockFault=false;
  visibleMoment=MOMENT_GARDEN;
  assert(!strcmp(buddyMomentText(),"VISITING THE OLD GARDEN"));
  int calledScore=momentScore(MOMENT_GARDEN,invitationDay,invitationDay/600UL);
  uint8_t oldCondition=buddyLegacy.condition[OBJECT_PLANT];
  buddyLegacy.condition[OBJECT_PLANT]=0;
  assert(buddyLegacyInvitation(buddy,buddyLegacy,invitationDay)==OBJECT_COUNT);
  assert(calledScore>momentScore(MOMENT_GARDEN,invitationDay,invitationDay/600UL)+150);
  buddyLegacy.condition[OBJECT_PLANT]=oldCondition;
  BuddyState interested=buddy;
  buddy.traits.sensitivity=5;buddy.affection[OBJECT_PLANT]=0;buddy.visits[OBJECT_PLANT]=0;
  buddy.inheritedObject=buddy.favoriteObject=OBJECT_COUNT;
  assert(buddyLegacyInvitation(buddy,buddyLegacy,invitationDay)==OBJECT_COUNT);
  buddy=interested;
  saveBuddy();uint32_t invitedSeed=buddy.seed;buddy={};loadOrCreateBuddy(invitationDay);
  assert(buddy.seed==invitedSeed && buddyLegacyInvitation(buddy,buddyLegacy,invitationDay)==OBJECT_PLANT);
  buddy.affection[OBJECT_PLANT] = 250;
  buddy.visits[OBJECT_PLANT] = 9;
  buddy.mood = 500;
  rememberWorldEvent(childBorn+86400UL, EVENT_WATERED, OBJECT_PLANT, BEHAVIOR_OBSERVE, 35);
  assert(buddy.mood >= 504 && buddyLegacy.lastCareDay[OBJECT_PLANT] == (childBorn+86400UL)/86400UL);
  visibleMoment = MOMENT_GARDEN;
  rawEpoch = childBorn+86400UL;
  clockTrusted = clockFault = false; // Test jumps the fake RTC ahead by a year.
  assert(!strcmp(buddyMomentText(),"TENDING THE OLD GARDEN"));
  advanceBuddyLegacyTo(childBorn+600UL*86400UL);
  assert(!legacyAvailable(OBJECT_PLANT));
  renderBuddy(true);
  for (uint8_t kind = 0; kind < OBJECT_COUNT; kind++) assert(!buddyLegacy.condition[kind]);

  // One damaged slot falls back to the other; two damaged site slots pause
  // only the marks, never create a new Buddy or alter the established life.
  start(); t = rawEpoch+25UL*86400UL;
  uint32_t safeSeed = buddy.seed;
  buddyLegacyComplete(t,OBJECT_STONE);
  nvs[std::string("buddy-life/")+(activeLegacySlot==0?"site-a":"site-b")][12] ^= 0x20;
  buddyLegacy = {}; setupBuddyLegacy(t);
  assert(!legacyLocked && !buddyLegacy.condition[OBJECT_STONE]);
  nvs["buddy-life/site-a"] = {1}; nvs["buddy-life/site-b"] = {2};
  buddyLegacy = {}; setupBuddyLegacy(t);
  assert(legacyLocked && buddy.alive && buddy.seed == safeSeed);
  start(); failNvs = true; buddyLegacyComplete(rawEpoch+25UL*86400UL,OBJECT_STONE);
  assert(legacySaveFailed && buddyLegacy.condition[OBJECT_STONE]);
  failNvs = false; saveBuddyLegacy(); assert(!legacySaveFailed);
  start();
  nvs.erase("buddy-life/site-a"); nvs.erase("buddy-life/site-b");
  buddyLegacy = {}; activeLegacySlot = -1; legacySequence = 0;
  clockFault = true; setupBuddyLegacy(rawEpoch);
  assert(buddyLegacy.magic != BUDDY_LEGACY_MAGIC);
  clockFault = false; serviceBuddyStorage(rawEpoch);
  assert(buddyLegacy.magic == BUDDY_LEGACY_MAGIC && !legacyLocked);
}

static void testLivingPatterns() {
  start();
  uint32_t now = rawEpoch + 20UL * 86400UL;
  buddy.currentStage = STAGE_YOUNG;
  buddy.possessions |= 1 << OBJECT_PLANT;
  buddy.favoriteObject = OBJECT_PLANT;
  buddy.affection[OBJECT_PLANT] = 230;
  buddy.visits[OBJECT_PLANT] = 10;
  uint8_t strength = 0;
  for (int day = 3; day >= 2; day--)
    rememberWorldEvent(now-day*86400UL, EVENT_WATERED, OBJECT_PLANT, BEHAVIOR_OBSERVE, 30);
  assert(buddyRoutineObject(now,&strength)==OBJECT_COUNT && strength==0);
  rememberWorldEvent(now-86400UL, EVENT_WATERED, OBJECT_PLANT, BEHAVIOR_OBSERVE, 30);
  assert(buddyRoutineObject(now,&strength)==OBJECT_PLANT && strength==3);
  assert(buddyRoutineObject(now+6UL*3600UL,&strength)==OBJECT_COUNT);
  int practiced = momentScore(MOMENT_PLANT,now,now/600UL);
  BuddyState remembered = buddy;
  buddy.eventCount = 0;
  int unpracticed = momentScore(MOMENT_PLANT,now,now/600UL);
  buddy = remembered;
  assert(practiced > unpracticed + 130);
  saveBuddy();buddy={};loadOrCreateBuddy(now);
  assert(buddyRoutineObject(now,&strength)==OBJECT_PLANT && strength==3);
  assert(buddyRoutineObject(now+15UL*86400UL,&strength)==OBJECT_COUNT); // fades unused

  // The house benefits actual rest, without changing ambient sensor readings.
  buddy.energy=600;buddy.comfort=400;buddy.security=400;
  BuddyState sameLife=buddy;
  buddyHouse.phase=HOUSE_OCCUPIED;buddyHouse.work=BUDDY_HOUSE_WORK_TARGET;
  buddyHouse.condition=1000;buddyHouse.builtEpoch=now-86400UL;
  performBehaviorHour(BEHAVIOR_REST,now);
  uint16_t homeComfort=buddy.comfort, homeSecurity=buddy.security;
  buddy=sameLife;buddyHouse.phase=HOUSE_EMPTY;
  performBehaviorHour(BEHAVIOR_REST,now);
  assert(homeComfort>=buddy.comfort+20 && homeSecurity>=buddy.security+12);
  buddy=sameLife;buddy.currentBehavior=BEHAVIOR_REST;
  buddyHouse.phase=HOUSE_OCCUPIED;latestTempC=12;latestClimateValid=true;
  applyClimateHour(now);uint16_t shelteredComfort=buddy.comfort;
  buddy=sameLife;buddy.currentBehavior=BEHAVIOR_REST;buddyHouse.phase=HOUSE_EMPTY;
  applyClimateHour(now);assert(shelteredComfort>buddy.comfort);
  latestTempC=22;
  buddy=sameLife;buddyHouse.phase=HOUSE_OCCUPIED;
  buddy.favoriteObject=OBJECT_PLANT;buddy.affection[OBJECT_PLANT]=300;
  buddy.visits[OBJECT_PLANT]=12;
  currentView=VIEW_BUDDY;renderBuddy(true);
  uint8_t furnished[5000];memcpy(furnished,display->buffer,5000);
  buddy.favoriteObject=OBJECT_COUNT;renderBuddy(true);
  bool sawFavorite=false;
  for (int y=110;y<138;y++) for (int x=51;x<68;x++) {
    uint8_t mask=0x80>>(x%8);
    if ((furnished[y*25+x/8]^display->buffer[y*25+x/8])&mask) sawFavorite=true;
  }
  assert(sawFavorite);
  // An attached resident feels a collapse more than one who rarely used it.
  BuddyHouseState houseBefore=buddyHouse;
  BuddyState resident=buddy;
  resident.energy=200;resident.security=600;resident.mood=600;
  buddy=resident;buddy.affection[OBJECT_NOOK]=900;
  buddyHouse=houseBefore;buddyHouse.phase=HOUSE_OCCUPIED;buddyHouse.condition=1;
  buddyHouse.lastDay=now/86400UL-1;
  advanceBuddyHouseTo(now);uint16_t attachedMood=buddy.mood;
  assert(buddyHouse.phase==HOUSE_RUIN);
  buddy=resident;buddy.affection[OBJECT_NOOK]=0;
  buddyHouse=houseBefore;buddyHouse.phase=HOUSE_OCCUPIED;buddyHouse.condition=1;
  buddyHouse.lastDay=now/86400UL-1;
  advanceBuddyHouseTo(now);
  assert(buddyHouse.phase==HOUSE_RUIN && attachedMood<buddy.mood);
  buddyHouse=houseBefore;

  // A child can embrace or reject a family custom without user input.
  buddy.generation=2;buddy.prior.generation=1;
  buddy.prior.dominantBehavior=BEHAVIOR_OBSERVE;
  buddy.inheritedObject=OBJECT_PLANT;buddy.favoriteObject=OBJECT_PLANT;
  buddy.currentStage=STAGE_CHILD;buddy.traits.sensitivity=85;
  buddy.affection[OBJECT_PLANT]=750;buddy.visits[OBJECT_PLANT]=18;
  buddy.energy=850;buddy.health=850;
  assert(buddyFamilyTraditionAffinity()>=150);
  uint32_t customHour=0;
  for (uint32_t hour=24;hour<24*10;hour++)
    if (buddyFamilyTraditionWindow(rawEpoch+hour*3600UL)) {
      customHour=rawEpoch+hour*3600UL;break;
    }
  assert(customHour);
  buddy.lastWorldEpoch=customHour-6UL*3600UL;
  uint16_t affectionBefore=buddy.affection[OBJECT_PLANT];
  liveWorldHour(customHour,BEHAVIOR_OBSERVE,true);
  assert(buddy.events[(buddy.eventHead+23)%24].event==EVENT_FAMILY_CUSTOM);
  assert(buddy.affection[OBJECT_PLANT]>affectionBefore && buddyFamilyTraditionRecent(customHour));
  assert(!buddy.totalInteractions);
  char story[80];describeWorldEvent(buddy.events[(buddy.eventHead+23)%24],story,sizeof(story));
  assert(strstr(story,"FAMILY CUSTOM"));
  saveBuddy();uint32_t childSeed=buddy.seed;buddy={};loadOrCreateBuddy(customHour);
  assert(buddy.seed==childSeed && buddyFamilyTraditionRecent(customHour));
  buddy.affection[OBJECT_PLANT]=20;buddy.visits[OBJECT_PLANT]=0;
  buddy.traits.sensitivity=5;
  assert(!buddyFamilyTraditionWindow(customHour));
  buddy.inheritedObject=OBJECT_STONE;
  assert(!buddyFamilyTraditionWindow(customHour)); // no stone to keep the custom with

  // The same pathways must emerge in a full unattended life, not only from
  // hand-prepared state in the focused assertions above.
  start();uint32_t born=buddy.birthEpoch;
  int routineHours=0;
  for(uint32_t hour=1;hour<=120UL*24UL;hour++) {
    uint32_t epoch=born+hour*3600UL;
    updateBuddySimulation(epoch);
    if(buddyRoutineObject(epoch,&strength)<OBJECT_COUNT) routineHours++;
  }
  assert(routineHours>0 && buddy.totalInteractions==0);
  updateBuddySimulation(born+(uint32_t)buddyLifespanDays(buddy.seed)*86400UL);
  assert(!buddy.alive);
  buddy.memorialShown=true;
  updateBuddySimulation(buddy.deathEpoch+86401UL);
  assert(buddy.generation==2 && buddy.alive && buddy.totalInteractions==0);
  uint32_t lastCustomEpoch=0;
  int customs=0;
  // Adoption is optional: eight-anchor routines can legitimately displace
  // the inherited favorite. Check emergence across individuals, not require
  // a particular descendant to follow a family custom against his preferences.
  for(uint8_t life=0;life<6 && !customs;++life) {
    uint32_t secondBorn=buddy.birthEpoch;
    for(uint32_t hour=1;hour<=180UL*24UL;hour++) {
      uint32_t epoch=secondBorn+hour*3600UL;
      updateBuddySimulation(epoch);
      for(uint8_t i=0;i<buddy.eventCount;i++) {
        const WorldMemory &event=buddy.events[i];
        if(event.event==EVENT_FAMILY_CUSTOM && event.epoch>lastCustomEpoch) {
          lastCustomEpoch=event.epoch;customs++;
        }
      }
    }
    assert(buddy.alive && buddy.lastUpdateEpoch>=secondBorn+180UL*86400UL);
    if(!customs && life<5) {
      updateBuddySimulation(secondBorn+(uint32_t)buddyLifespanDays(buddy.seed)*86400UL);
      buddy.memorialShown=true;updateBuddySimulation(buddy.deathEpoch+86401UL);
    }
  }
  assert(customs>0 && buddy.totalInteractions==0);
}

static void exportArtworkGallery() {
  const char *directory = std::getenv("BUDDY_ART_REVIEW_DIR");
  if (!directory || !*directory) return;
  for (int sex = 0; sex < 2; ++sex) {
    start();
    buddy.seed = sex ? 0x13579bdfUL : 0x13579bdeUL;
    buddy.currentStage = STAGE_ADULT;
    buddy.mood = buddy.comfort = 650;
    currentView = VIEW_BUDDY;
    const int columns = 5, rows = (MOMENT_COUNT + columns - 1) / columns;
    std::vector<uint8_t> full(MOMENT_COUNT * 5000), art(MOMENT_COUNT * 5000);
    for (int moment = 0; moment < MOMENT_COUNT; ++moment) {
      visibleMoment = (BuddyMoment)moment;
      visibleBehavior = behaviorForMoment(visibleMoment);
      renderBuddy(true);
      memcpy(full.data() + moment * 5000, display->buffer, 5000);
      display->EPD_Clear();
      drawBuddyFace(visibleMoment == MOMENT_NAP);
      memcpy(art.data() + moment * 5000, display->buffer, 5000);
    }
    for (int mode = 0; mode < 2; ++mode) {
      std::string path = std::string(directory) + (sex ? "/girl-" : "/boy-") +
        (mode ? "art-only.pbm" : "full-scenes.pbm");
      std::ofstream output(path);
      assert(output);
      output << "P1\n" << columns * 200 << ' ' << rows * 200 << "\n";
      const std::vector<uint8_t> &frames = mode ? art : full;
      for (int y = 0; y < rows * 200; ++y) {
        for (int x = 0; x < columns * 200; ++x) {
          int index = (y / 200) * columns + x / 200;
          int pixel = index < MOMENT_COUNT ?
            (frames[index * 5000 + (y % 200) * 25 + (x % 200) / 8] >>
             (7 - (x % 200) % 8)) & 1 : 0;
          output << pixel << ' ';
        }
        output << '\n';
      }
    }
  }
  // Normal connected scene and its caption-free counterpart come from the
  // same production render, with only the caption band cleared afterward.
  start();
  buddy.seed = 0x13579bdeUL;
  buddy.currentStage = STAGE_ADULT;
  currentView = VIEW_BUDDY;
  NearbyPacket peer = currentNearbyPacket();
  peer.sender = nearbyState.selfId + 1;
  buddy.seed = 0x13579bdfUL;
  peer.visual = visualGenome();
  peer.girl = true;
  buddy.seed = 0x13579bdeUL;
  cableScenePeer = peer;
  cableSceneMoment = MOMENT_STARGAZE;
  cableSceneActivity = cableActivityForMoment(MOMENT_STARGAZE);
  buddyCableStart(cableSession, ticks);
  assert(buddyCableObservePeer(cableSession, peer.sender, nearbyState.selfId, ticks));
  cableGreetingAwaitingPaint = false; cableGreetingUntilMs = 0;
  renderBuddy(true);
  auto cableInk = [&](int x, int y) {
    return (display->buffer[y * 25 + x / 8] >> (7 - x % 8)) & 1;
  };
  assert(cableInk(93, 18) && !cableInk(105, 15) &&
         cableInk(80, 20) && cableInk(124, 21));
  for (int mode = 0; mode < 2; ++mode) {
    if (mode) buddyFill(1, 143, 198, 56, DRIVER_COLOR_WHITE);
    std::string path = std::string(directory) +
      (mode ? "/cable-art-only.pbm" : "/cable-full-scene.pbm");
    std::ofstream output(path);
    assert(output);
    output << "P1\n200 200\n";
    for (int y = 0; y < 200; ++y) {
      for (int x = 0; x < 200; ++x)
        output << ((display->buffer[y * 25 + x / 8] >> (7 - x % 8)) & 1) << ' ';
      output << '\n';
    }
  }
  // Render every shared activity too: a broad PLAY/GARDEN symbol can conceal
  // mistakes that only occur in a specific tool or possession illustration.
  std::vector<uint8_t> pairs(MOMENT_COUNT*5000);
  for(int m=0;m<MOMENT_COUNT;m++) {
    cableSceneMoment=(BuddyMoment)m;
    cableSceneActivity=cableActivityForMoment((BuddyMoment)m);
    renderBuddy(true);
    memcpy(pairs.data()+m*5000,display->buffer,5000);
  }
  std::ofstream paired(std::string(directory)+"/cable-all-scenes.pbm");
  const int columns=5,rows=(MOMENT_COUNT+columns-1)/columns;
  paired<<"P1\n"<<columns*200<<' '<<rows*200<<"\n";
  for(int y=0;y<rows*200;y++) {
    for(int x=0;x<columns*200;x++) {
      int index=(y/200)*columns+x/200;
      paired<<(index<MOMENT_COUNT ?
        (pairs[index*5000+(y%200)*25+(x%200)/8]>>(7-x%8))&1 : 0)<<' ';
    }
    paired<<'\n';
  }
  buddyCableStop(cableSession);
  cableScenePeer = {};
}

static void testActivityArtIsolation() {
  start();
  visibleMoment=MOMENT_WATCH;
  for(int m=0;m<MOMENT_COUNT;m++) {
    display->EPD_Clear();
    drawPixelProp(false,(BuddyMoment)m);
    assert(visibleMoment==MOMENT_WATCH); // shared art must not change solo activity
    for(int y=0;y<200;y++) for(int x=0;x<200;x++) {
      bool ink=(display->buffer[y*25+x/8]>>(7-x%8))&1;
      if(ink) assert(x>=147 && x<199 && y>=38 && y<140);
    }
    display->EPD_Clear();
    drawPixelProp(true,(BuddyMoment)m);
    for(uint8_t value:display->buffer) assert(value==0);
  }
  // Even over dark body/tail pixels, only the item's own white details erase
  // ink. The rest of the prop lane remains transparent for every activity.
  for(int m=0;m<MOMENT_COUNT;m++) {
    display->EPD_Clear();
    buddyFill(150,82,48,58);
    drawPixelProp(false,(BuddyMoment)m);
    int paper=0;
    for(int y=82;y<140;y++) for(int x=150;x<198;x++)
      paper+=!((display->buffer[y*25+x/8]>>(7-x%8))&1);
    assert(paper<1200); // a rectangular white backdrop used to erase >1500
  }
  // Simple item scenes must composite exactly like their original artwork:
  // every variation retains its silhouette and interior, with no extra mask.
  const BuddyMoment moments[]={MOMENT_STONE,MOMENT_PLANT,MOMENT_NOOK,MOMENT_TOY};
  const uint8_t kinds[]={OBJECT_STONE,OBJECT_PLANT,OBJECT_NOOK,OBJECT_TOY};
  for(int i=0;i<4;i++) for(uint32_t seed=1;seed<80;seed++) {
    buddy.seed=seed;
    uint8_t expected[5000];
    auto backdrop=[&]() {
      display->EPD_Clear();
      for(int y=82;y<140;y+=4) buddyFill(150,y,48,2);
    };
    backdrop();
    drawItemArt(kinds[i],150,88,3,buddyItemStyle(seed,buddy.generation,kinds[i]));
    memcpy(expected,display->buffer,sizeof(expected));
    backdrop();
    drawPixelProp(false,moments[i]);
    assert(!memcmp(expected,display->buffer,sizeof(expected)));
  }
  // Hollow tools still have white interiors even when placed over dark ink.
  display->EPD_Clear();buddyFill(150,82,48,58);
  drawPixelProp(false,MOMENT_COOL);
  assert(!((display->buffer[106*25+188/8]>>(7-188%8))&1));
}

static void testStargazeArtwork() {
  start();
  buddy.currentStage = STAGE_ADULT;
  visibleMoment = MOMENT_STARGAZE;
  visibleBehavior = behaviorForMoment(visibleMoment);
  display->EPD_Clear();
  drawBuddyFace(false);
  auto ink = [&](int x, int y) {
    return (display->buffer[y * 25 + x / 8] >> (7 - x % 8)) & 1;
  };
  assert(ink(171, 51));                 // crescent remains visible
  assert(!ink(182, 45));                // white cutout opens the crescent
  assert(ink(153, 77) && ink(191, 88)); // two separate bright stars
}

static void testMemorialArtwork() {
  start();
  display->EPD_Clear();
  drawMemorialHeadstone();
  int artPixels=0;
  for(int y=0;y<200;y++) for(int x=0;x<200;x++) {
    bool ink=(display->buffer[y*25+x/8]>>(7-x%8))&1;
    if(ink) {
      artPixels++;
      assert(y<132); // dedicated art must never enter the summary/banner area
    }
  }
  assert(artPixels>1000);

  buddy.alive=false;
  buddy.currentStage=STAGE_GONE;
  buddy.prior.livedDays=365;
  buddy.memorialShown=true;
  archiveBlocked=false;
  currentView=VIEW_BUDDY;
  renderBuddy(true);
  std::ofstream memorial(buddyTestTempPath("buddy-memorial.pbm"));
  memorial<<"P1\n200 200\n";
  for(int y=0;y<200;y++) {
    for(int x=0;x<200;x++)
      memorial<<((display->buffer[y*25+x/8]>>(7-x%8))&1)<<' ';
    memorial<<'\n';
  }
  assert(memorial);
}

static void testTwentyConsecutiveAutonomousGenerations() {
  start();
  uint32_t previousSeed = 0;
  bool variedLifespan = false;
  uint16_t firstLifespan = buddyLifespanDays(buddy.seed);
  uint32_t lastDeath = 0;
  for (uint16_t generation = 1; generation <= 20; ++generation) {
    assert(buddy.alive && buddy.generation == generation);
    uint32_t seed = buddy.seed;
    assert(seed && seed != previousSeed);
    uint16_t lifespan = buddyLifespanDays(seed);
    assert(lifespan >= 305 && lifespan <= 425);
    variedLifespan |= lifespan != firstLifespan;
    uint32_t birth = buddy.birthEpoch;
    assert(birth > lastDeath);
    uint32_t priorHours = 0;
    for (uint32_t day = 1; day <= lifespan; ++day) {
      updateBuddySimulation(birth + day * 86400UL);
      if (day == 15 && lifespan > 15) assert(buddy.currentStage == STAGE_CHILD);
      if (day == 61 && lifespan > 61) assert(buddy.currentStage == STAGE_YOUNG);
      if (day == 151 && lifespan > 151) assert(buddy.currentStage == STAGE_ADULT);
      if (day < lifespan) {
        assert(buddy.alive && buddyAgeDays(birth + day * 86400UL) == day);
        uint32_t hours = 0;
        for (auto count : buddy.behaviorCounts) hours += count;
        assert(hours == day * 24UL && hours >= priorHours);
        priorHours = hours;
      }
    }
    assert(!buddy.alive && buddy.deathEpoch == birth + lifespan * 86400UL);
    assert(buddy.prior.livedDays == lifespan && buddy.eventCount > 0);
    uint32_t hours = 0;
    for (auto count : buddy.behaviorCounts) hours += count;
    assert(hours == lifespan * 24UL);
    // The actual memorial renderer marks the event as seen, rather than a
    // test assigning that state. Let the real archive exporter clear the
    // pending queue so twenty generations can succeed an optional SD card.
    currentView = VIEW_BUDDY;
    renderBuddy(true);
    assert(buddy.memorialShown);
    sdAvailable = true;
    retryBuddyArchives();
    assert(!archiveBlocked);
    sdAvailable = false;
    previousSeed = seed;
    lastDeath = buddy.deathEpoch;
    updateBuddySimulation(lastDeath + 86401UL);
    assert(buddy.alive && buddy.generation == generation + 1);
    assert(buddy.prior.livedDays == lifespan);
  }
  assert(variedLifespan);
}

#ifdef BUDDY_ACCELERATED_LIFE_TEST
static void testIntegratedSixHourFirmware() {
  reset();
  loadOrCreateBuddy(0);
  assert(!storageLocked && nvs["buddy-life/test-mode"].size() == 4);
  assert(buddyTestClockLoad());
  uint32_t physicalBirth = rawEpoch;
  uint32_t firstNow = buddyNow();
  assert(firstNow == physicalBirth && !clockFault);
  createBuddyLife(firstNow, false);
  setupBuddyHouse(firstNow);setupBuddyAmbition(firstNow);setupBuddyLegacy(firstNow);
  assert(buddy.alive && buddy.generation == 1 && testClockReady);
  uint32_t seed = buddy.seed;
  uint16_t lifespan = buddyLifespanDays(seed);
  rawEpoch += 30;
  ticks += 30000;
  uint32_t virtualNow = buddyNow();
  assert(virtualNow == physicalBirth + (uint32_t)lifespan * 120UL);
  updateBuddySimulation(virtualNow);
  uint32_t checkpoint = buddy.lastUpdateEpoch;
  assert(checkpoint > physicalBirth);

  // Restart at the same physical instant. No duplicate hourly work or new
  // generation may be created by loading the A/B life and A/B clock records.
  buddy = {};
  clockTrusted = clockFault = false;
  clockLastEpoch = clockLastMs = ticks = 0;
  loadOrCreateBuddy(0);
  assert(buddyTestClockLoad() && buddy.seed == seed);
  assert(buddyNow() == virtualNow && !clockFault);
  setupBuddyHouse(virtualNow);setupBuddyAmbition(virtualNow);setupBuddyLegacy(virtualNow);
  assert(recoverBuddyCompletion());
  updateBuddySimulation(buddyNow());
  assert(buddy.lastUpdateEpoch == checkpoint && buddy.generation == 1);

  uint32_t remaining = BUDDY_TEST_REAL_LIFE_SECONDS - 30;
  rawEpoch += remaining;
  ticks += remaining * 1000UL;
  assert(buddyNow() == physicalBirth + (uint32_t)lifespan * 86400UL);
  updateBuddySimulation(buddyNow());
  assert(!buddy.alive && buddy.prior.livedDays == lifespan);
  assert(!buddyTestMemorialReady(clockLastEpoch, buddy.deathEpoch));
  rawEpoch += 121;
  ticks += 121000;
  updateBuddySimulation(buddyNow());
  assert(buddy.alive && buddy.generation == 2 && testLifeTime.generation == 2);
  assert(buddy.birthEpoch == testLifeTime.simulatedBirth);
  assert(testLifeTime.realBirth == rawEpoch);
  assert(rtcSetCalls == 0); // the physical RTC was never accelerated

  rtcGood = false;
  uint32_t saved = buddy.lastUpdateEpoch;
  buddyNow();
  assert(clockFault);
  updateBuddySimulation(saved + 86400UL);
  assert(buddy.lastUpdateEpoch == saved);
}

int main() {
  testIntegratedSixHourFirmware();
  testMemorialArtwork();
  puts("PASS: six-hour clock, real RTC, restart, death, memorial, successor, RTC fault");
  return 0;
}
#else
#include "stability_regressions.inc"
#include "checkpoint_regressions.inc"
int main() {
  testOrdinaryCheckpointRecovery();
  testAuditStabilization();
  testRecoveryButton();
  testClockPolicy();
  testRestoredLifeClock();
  testRawRestoreCatchesUpByRealDays();
#ifdef BUDDY_RESTORE_OFFSET_SECONDS
  testRestoreBootstrap();
#endif
  testAcceleratedTimeArithmeticAndLives();
  testNearbySchedule();
  testEightAnchorMigration();
  testEightAnchorLives();
  testSdUpdatePackage();
  testSdSleepHandoff();
  testVoiceCommandPolicy();
  testCableSessionLifecycle();
  testTwoCableBuddiesReconnect();
  testSoundModes();
  testCableRecoveryMarker();
  testI2cBusClear();
  testSoundDecoder();
  testTunePlan();
  testSleepWakeSound();
  testRuntimePowerAndMusicPolicy();
  testListeningWindowPolicy();
  testDeadlineScheduler();
  testMomentDurations();
  testFirstStartAndSex();
  testNearby();
  testInnerLife();
  testInnerLifeUnattended();
  testAmbitions();
  testLivingPatterns();
  // Mic level rejects DC; room noise, a bump, and brief sounds cannot count
  // as speech. Sustained sound needs an ending gap and respects cooldown.
  int16_t stereo[256];
  for(int i=0;i<128;i++){stereo[2*i]=1000;stereo[2*i+1]=0;}
  assert(buddyVoiceLevel(stereo,128)==0);
  for(int i=0;i<128;i++)stereo[2*i]=i&1?2000:-2000;
  assert(buddyVoiceLevel(stereo,128)==2000);
  BuddyVoiceDetector voice;
  uint32_t voiceMs=1000;
  for(int i=0;i<100;i++,voiceMs+=16)assert(!voice.feed(25,16,voiceMs));
  assert(voice.threshold()==460);
  for(int i=0;i<40;i++,voiceMs+=16)assert(!voice.feed(350,16,voiceMs));
  for(int i=0;i<16;i++,voiceMs+=16)assert(!voice.feed(25,16,voiceMs));
  for(int i=0;i<12;i++,voiceMs+=16)assert(!voice.feed(1800,16,voiceMs));
  for(int i=0;i<16;i++,voiceMs+=16)assert(!voice.feed(25,16,voiceMs));
  for(int i=0;i<40;i++,voiceMs+=16)assert(!voice.feed(1800,16,voiceMs));
  for(int i=0;i<14;i++,voiceMs+=16)assert(!voice.feed(25,16,voiceMs));
  assert(voice.feed(25,16,voiceMs));voiceMs+=16;
  for(int i=0;i<40;i++,voiceMs+=16)assert(!voice.feed(1800,16,voiceMs));
  for(int i=0;i<40;i++,voiceMs+=16)assert(!voice.feed(25,16,voiceMs));
  voiceMs+=20UL*1000UL;
  for(int i=0;i<40;i++,voiceMs+=16)assert(!voice.feed(1800,16,voiceMs));
  for(int i=0;i<14;i++,voiceMs+=16)assert(!voice.feed(25,16,voiceMs));
  assert(voice.feed(25,16,voiceMs));
  for(uint32_t n=0;n<100;n++)assert(buddyRandomSoundIndex(n)==n%BUDDY_SOUND_CLIPS);
  for (BuddySound event : {SOUND_NORMAL,SOUND_SLEEP,SOUND_WAKE,SOUND_SPECIAL}) {
    bool heard[BUDDY_SOUND_CLIPS]={};
    for (uint32_t n=0;n<100;n++) {
      uint8_t index=buddySoundClipIndex(event,n);
      assert(index==n%BUDDY_SOUND_CLIPS);
      heard[index]=true;
    }
    for (bool used:heard) assert(used);
    assert(buddySoundClipIndex(event,999,7)==7);
    assert(buddySoundClipIndex(event,999,8)==buddyRandomSoundIndex(999));
  }
  start();
  buddy.traits.sensitivity=50;
  latestTempC=4.9f;assert(!strcmp(temperatureFeeling(latestTempC),"VERY COLD"));
  latestTempC=14.9f;assert(!strcmp(temperatureFeeling(latestTempC),"COLD"));
  latestTempC=18.9f;assert(!strcmp(temperatureFeeling(latestTempC),"COOL"));
  latestTempC=22.0f;assert(!strcmp(temperatureFeeling(latestTempC),"COZY"));
  latestTempC=30.0f;assert(!strcmp(temperatureFeeling(latestTempC),"WARM"));
  latestTempC=35.0f;assert(!strcmp(temperatureFeeling(latestTempC),"HOT"));
  latestTempC=40.1f;assert(!strcmp(temperatureFeeling(latestTempC),"VERY HOT"));
  latestTempC=22.0f;
  // Regression: sleep (index zero) must win when exhausted at night.
  buddy.energy=0;assert(chooseBehavior(23,rawEpoch)==BEHAVIOR_SLEEP);
  // Distinct first lives start with different dispositions and needs, while
  // adding the separate home record never changes an existing life.
  start();BuddyState firstLife=buddy;
  start();assert(buddy.seed!=firstLife.seed);
  assert(buddy.energy!=firstLife.energy || buddy.social!=firstLife.social ||
         buddy.play!=firstLife.play || buddy.curiosityNeed!=firstLife.curiosityNeed);
  uint32_t rngCheckpoint=fakeRandomState;
  start();uint32_t oneBoardSeed=buddy.seed;
  fakeRandomState=rngCheckpoint;fakeDeviceSalt^=0x01234567;
  start();assert(buddy.seed!=oneBoardSeed);fakeDeviceSalt=0xB00D1234;
  BuddyState unchanged=buddy;
  nvs.erase("buddy-life/home-a");nvs.erase("buddy-life/home-b");
  buddyHouse={};activeHouseSlot=-1;houseSequence=0;
  setupBuddyHouse(rawEpoch);
  assert(!memcmp(&buddy,&unchanged,sizeof(buddy)));
  assert(preferences.getBytesLength("home-a")==sizeof(BuddyHouseState));
  assert(preferences.getBytesLength("home-b")==sizeof(BuddyHouseState));
  // An independent, multi-month build changes real needs, survives reboot,
  // and its ownership survives into the next generation.
  buddy.energy=900;buddy.health=900;
  buddy.possessions|=(1<<OBJECT_STONE)|(1<<OBJECT_NOOK);
  uint32_t firstHomeSeed=buddyHouse.siteSeed;
  uint32_t builtDay=0;
  for(uint32_t day=buddyHouse.lastDay+1;day<buddy.birthEpoch/86400UL+365;day++) {
    buddy.energy=900; // the hourly life simulation normally replenishes energy
    advanceBuddyHouseTo(day*86400UL);
    if(buddyHouse.phase==HOUSE_OCCUPIED){builtDay=day;break;}
  }
  assert(builtDay && builtDay > buddy.birthEpoch/86400UL+45);
  assert(buddyHouse.condition==1000 && buddyHouse.builderGeneration==1 && buddyHouse.cycles==1);
  BuddyHouseState finishedHome=buddyHouse;
  buddyHouse={};setupBuddyHouse(builtDay*86400UL);
  assert(buddyHouse.phase==HOUSE_OCCUPIED && buddyHouse.work==BUDDY_HOUSE_WORK_TARGET &&
         buddyHouse.siteSeed==firstHomeSeed && buddyHouse.builtEpoch==finishedHome.builtEpoch);
  buddy.alive=false;buddy.prior.generation=1;buddy.prior.dominantBehavior=BEHAVIOR_REST;
  createBuddyLife((builtDay+1)*86400UL,true);
  assert(buddy.generation==2 && buddyHouse.builderGeneration==1 && buddyHouse.phase==HOUSE_OCCUPIED);
  assert(buddy.events[(buddy.eventHead+23)%24].event==EVENT_HOUSE_INHERITED);
  uint32_t collapsedDay=0;
  for(uint32_t day=builtDay+1;day<builtDay+1100;day++) {
    advanceBuddyHouseTo(day*86400UL);
    if(buddyHouse.phase==HOUSE_RUIN){collapsedDay=day;break;}
  }
  assert(collapsedDay && buddyHouse.condition==0);
  advanceBuddyHouseTo((collapsedDay+30)*86400UL);
  assert(buddyHouse.phase==HOUSE_EMPTY && buddyHouse.siteSeed==firstHomeSeed);
  buddy.birthEpoch=(collapsedDay+15)*86400UL;buddy.energy=900;buddy.health=900;
  buddy.possessions|=(1<<OBJECT_STONE)|(1<<OBJECT_NOOK);
  uint32_t secondBuildDay=0;
  for(uint32_t day=collapsedDay+31;day<collapsedDay+350;day++) {
    buddy.energy=900;
    advanceBuddyHouseTo(day*86400UL);
    if(buddyHouse.phase==HOUSE_OCCUPIED){secondBuildDay=day;break;}
  }
  assert(secondBuildDay && buddyHouse.cycles==2 && buddyHouse.builderGeneration==2);
  // Interrupted writes recover the last complete home record. Corruption of
  // both slots pauses only the home, never recreating or erasing Buddy.
  uint32_t stableSequence=houseSequence;
  failNvs=true;buddyHouse.condition=500;saveBuddyHouse();assert(houseSaveFailed);
  failNvs=false;buddyHouse={};setupBuddyHouse(secondBuildDay*86400UL);
  assert(!houseLocked && buddyHouse.condition==1000 && houseSequence==stableSequence);
  nvs[std::string("buddy-life/")+(activeHouseSlot==0?"home-a":"home-b")][19]^=0x40;
  buddyHouse={};setupBuddyHouse(secondBuildDay*86400UL);
  assert(!houseLocked && buddyHouse.phase==HOUSE_BUILDING && buddyHouse.siteSeed==firstHomeSeed);
  auto livingSeed=buddy.seed;
  nvs["buddy-life/home-a"]={1};nvs["buddy-life/home-b"]={2};
  buddyHouse={};setupBuddyHouse(secondBuildDay*86400UL);
  assert(houseLocked && buddy.seed==livingSeed && !storageLocked);
  // Drive the real hourly simulator too: the home should emerge without
  // direct house calls or user interactions once resources are discovered.
  start();buddy.possessions|=(1<<OBJECT_STONE)|(1<<OBJECT_NOOK);
  updateBuddySimulation(buddy.birthEpoch+250UL*86400UL);
  assert(buddyHouse.phase==HOUSE_OCCUPIED && buddy.totalInteractions==0);
  // Migration preserves the exact old identity, history and physical family features.
  start();
  BuddyStateV1 legacy=buddy;legacy.version=1;legacy.generation=7;legacy.totalInteractions=41;
  nvs.clear();preferences.putBytes("state",&legacy,sizeof(legacy));buddy={};activeSaveSlot=-1;
  loadOrCreateBuddy(rawEpoch);
  assert(buddy.version==3&&buddy.seed==legacy.seed&&buddy.birthEpoch==legacy.birthEpoch&&buddy.totalInteractions==41);
  assert(preferences.getBytesLength("state")==sizeof(legacy));assert(!storageLocked);
  auto saved=buddy; saveBuddy();
  // Corrupt the newest slot: load the intact recovery slot, never create generation one.
  nvs[std::string("buddy-life/")+(activeSaveSlot==0?"life-a":"life-b")][20]^=0x40;
  buddy={};loadOrCreateBuddy(rawEpoch);assert(buddy.seed==saved.seed&&buddy.generation==7&&lifeRollbackDetected);
  nvs["buddy-life/life-a"]={1};nvs["buddy-life/life-b"]={2};buddy={};
  loadOrCreateBuddy(rawEpoch);assert(storageLocked);
  start();failNvs=true;saveBuddy();assert(saveFailed);failNvs=false;saveBuddy();assert(!saveFailed);
  // Clock loss and backward correction do not rewind or replay life.
  start();assert(buddyNow()==rawEpoch);auto before=buddy.lastUpdateEpoch;
  rtcGood=false;assert(buddyNow()==before&&clockFault);updateBuddySimulation(before+86400);assert(buddy.lastUpdateEpoch==before);
  rtcGood=true;rawEpoch=before-100;
  assert(buddyNow()==before&&!clockFault&&rawEpoch==before);
  assert(lastClockRecoveredReason==CLOCK_REASON_BACKWARD&&rtcSetCalls==1);
  serviceBuddyRecovery();
  assert(lastRecovery.clockReason==CLOCK_REASON_COMM || lastRecovery.clockReason==CLOCK_REASON_BACKWARD);
  assert(lastRecovery.flags&2);
  uint32_t recoveredSequence=lastRecovery.sequence;
  uint32_t recoveredSeed=buddy.seed;
  assert(recordBuddyRecovery(8,CLOCK_REASON_NONE));
  nvs[std::string("buddy-recovery/")+(recoverySlot==0?"rec-a":"rec-b")][12]^=0x40;
  lastRecovery={};recoverySlot=-1;loadBuddyRecovery();
  assert(lastRecovery.sequence==recoveredSequence && buddy.seed==recoveredSeed);
  rawEpoch=before+3600;ticks=3600000;assert(buddyNow()==rawEpoch&&!clockFault);
  updateBuddySimulation(rawEpoch);assert(buddy.lastUpdateEpoch==rawEpoch);
  rawEpoch+=86400;
  uint32_t trusted=clockLastEpoch;
  assert(buddyNow()==trusted&&!clockFault&&rawEpoch==trusted); // reject false aging
  assert(lastClockRecoveredReason==CLOCK_REASON_FORWARD);
  // A failed RTC write is visible but never rewinds a saved life. Retry works.
  rawEpoch=trusted-100;rtcSetGood=false;ticks+=31000;
  assert(buddyNow()>=trusted&&clockFault);
  rtcSetGood=true;ticks+=31000;
  assert(buddyNow()>=trusted&&!clockFault);
  // Failed daily write must not consume the day; retry commits exactly once.
  start();sdAvailable=true;failSd=true;appendDailyJournal(rawEpoch);assert(buddy.lastLoggedDay==UINT32_MAX);
  failSd=false;sdAvailable=true;appendDailyJournal(rawEpoch);assert(buddy.lastLoggedDay==0);
  char path[96];lifePath(path,sizeof(path),"daily.csv");auto csv=files[path];
  buddy.lastLoggedDay=UINT32_MAX;appendDailyJournal(rawEpoch);assert(files[path]==csv);
  // A power interruption after the original rename restores its .bak on retry.
  files[std::string(path)+".bak"]=files[path];files.erase(path);
  buddy.lastLoggedDay=UINT32_MAX;appendDailyJournal(rawEpoch);assert(files[path]==csv);
  // Real memory biases behavior and experience changes personality.
  start();int bias=memoryBehaviorBias(BEHAVIOR_PLAY,rawEpoch);
  rememberWorldEvent(rawEpoch,EVENT_PLAYED,OBJECT_TOY,BEHAVIOR_PLAY,70);
  assert(memoryBehaviorBias(BEHAVIOR_PLAY,rawEpoch)>bias);
  auto birth=buddy.traits;
  for(int h=1;h<=24*10;h++) liveWorldHour(rawEpoch+h*3600,BEHAVIOR_PLAY,true);
  assert(buddy.traits.playfulness>birth.playfulness&&buddy.possessions&(1<<OBJECT_TOY));
  // A pursuit emerges from personality and affection, steers actual hourly
  // world activity, remains reproducible after save/reload, and is visible.
  start();buddy.possessions=(1<<OBJECT_COUNT)-1;
  buddy.traits={95,5,5,5,5,95};buddy.inheritedObject=OBJECT_COUNT;
  for(auto &affection:buddy.affection) affection=0;
  buddy.affection[OBJECT_STONE]=1000;
  uint32_t pursuitTime=rawEpoch+6*3600;
  assert(buddyDailyPursuit(pursuitTime)==OBJECT_STONE);
  assert(pursuitBehavior(OBJECT_STONE)==BEHAVIOR_EXPLORE);
  buddy.lastWorldEpoch=rawEpoch;
  liveWorldHour(pursuitTime,BEHAVIOR_EXPLORE,true);
  const WorldMemory &pursuitEvent=buddy.events[(buddy.eventHead+23)%24];
  assert(pursuitEvent.object==OBJECT_STONE&&buddy.visits[OBJECT_STONE]==1);
  int recentScore=momentScore(MOMENT_POLISH,pursuitTime,pursuitTime/600);
  buddy.events[(buddy.eventHead+23)%24].epoch=pursuitTime-4*3600;
  assert(recentScore-momentScore(MOMENT_POLISH,pursuitTime,pursuitTime/600)==260);
  buddy.events[(buddy.eventHead+23)%24].epoch=pursuitTime;
  auto pursuitSeed=buddy.seed;saveBuddy();buddy={};loadOrCreateBuddy(rawEpoch);
  assert(buddy.seed==pursuitSeed&&buddyDailyPursuit(pursuitTime)==OBJECT_STONE);
  // Multi-day work is carried by the existing event ring and save layout.
  // Temperament, ownership, and affection select a project; compatible hours
  // advance it, a reboot preserves progress, and completion changes real needs.
  start();buddy.currentStage=STAGE_YOUNG;buddy.energy=900;
  buddy.possessions=(1<<OBJECT_COUNT)-1;
  for(auto &visits:buddy.visits) visits=2;
  for(auto &affection:buddy.affection) affection=0;
  buddy.affection[OBJECT_PLANT]=900;
  buddy.traits={5,5,5,5,95,5};
  assert(chooseBuddyProject(rawEpoch+6*3600)==OBJECT_PLANT);
  buddy.lastWorldEpoch=rawEpoch;
  liveWorldHour(rawEpoch+6*3600,BEHAVIOR_OBSERVE,true);
  assert(buddyActiveProject(rawEpoch+6*3600).object==OBJECT_PLANT);
  assert(buddyDailyPursuit(rawEpoch+6*3600)==OBJECT_PLANT);
  liveWorldHour(rawEpoch+12*3600,BEHAVIOR_OBSERVE,true);
  liveWorldHour(rawEpoch+18*3600,BEHAVIOR_OBSERVE,true);
  assert(buddyActiveProject(rawEpoch+18*3600).steps==2);
  auto projectSeed=buddy.seed;saveBuddy();buddy={};loadOrCreateBuddy(rawEpoch);
  assert(buddy.seed==projectSeed&&buddyActiveProject(rawEpoch+18*3600).steps==2);
  uint8_t plantBefore=buddy.plantGrowth;
  uint16_t affectionBefore=buddy.affection[OBJECT_PLANT];
  liveWorldHour(rawEpoch+24*3600,BEHAVIOR_OBSERVE,true);
  assert(buddyActiveProject(rawEpoch+24*3600).object==OBJECT_COUNT);
  assert(buddyRecentProjectCompletion(rawEpoch+24*3600)==rawEpoch+24*3600);
  assert(buddy.plantGrowth==plantBefore+9&&buddy.affection[OBJECT_PLANT]>affectionBefore);
  assert(buddySpecialPending&&buddySpecialEpoch==rawEpoch+24*3600);
  assert(momentAvailable(MOMENT_CELEBRATE,rawEpoch+24*3600));
  buddy.lastWorldEpoch=rawEpoch+24*3600;
  liveWorldHour(rawEpoch+30*3600,BEHAVIOR_OBSERVE,true);
  assert(buddyActiveProject(rawEpoch+30*3600).object==OBJECT_COUNT); // recovery interval
  // With no user interaction or preloaded possessions, a normal life still
  // discovers objects and carries projects through to completion by itself.
  start();
  uint32_t autonomousBirth=buddy.birthEpoch;
  int projectStarts=0,projectFinishes=0;
  uint8_t previousHead=buddy.eventHead;
  for(int hour=1;hour<=24*30;hour++) {
    updateBuddySimulation(autonomousBirth+hour*3600UL);
    if(buddy.eventHead!=previousHead) {
      const WorldMemory &event=buddy.events[(buddy.eventHead+23)%24];
      projectStarts += event.event==EVENT_PROJECT_STARTED;
      projectFinishes += event.event==EVENT_PROJECT_FINISHED;
      previousHead=buddy.eventHead;
    }
  }
  assert(projectStarts>0&&projectFinishes>0&&buddy.totalInteractions==0);
  assert(buddySpecialPending&&buddySpecialEpoch>=autonomousBirth+15UL*86400UL);
  buddy.traits={5,5,5,95,95,5};
  for(auto &affection:buddy.affection) affection=0;
  buddy.affection[OBJECT_PLANT]=1000;
  buddy.possessions|=1<<OBJECT_PLANT;
  buddy.eventCount=0;
  assert(buddyDailyPursuit(pursuitTime)==OBJECT_PLANT);
  // Possessions unlock meaningful occupations with corresponding world effects.
  start();buddy.possessions=(1<<OBJECT_COUNT)-1;buddy.currentStage=STAGE_ADULT;
  buddy.energy=800;buddy.health=800;buddy.mood=800;buddy.plantGrowth=0;buddy.lastWorldEpoch=rawEpoch;buddy.eventCount=3;
  for(int moment=MOMENT_WATER;moment<MOMENT_COUNT;moment++) if(moment!=MOMENT_STARGAZE)
    assert(momentAvailable((BuddyMoment)moment,rawEpoch));
  uint32_t midnight=rawEpoch-((rawEpoch/3600UL)%24)*3600UL;
  assert(momentAvailable(MOMENT_STARGAZE,midnight));
  buddy.possessions=1<<OBJECT_PLANT;
  liveWorldHour(rawEpoch+6*3600,BEHAVIOR_OBSERVE,true);
  assert(buddy.plantGrowth==2&&buddy.events[(buddy.eventHead+23)%24].event==EVENT_WATERED);
  // Awake presentation changes every ten minutes without mutating life needs.
  buddy.possessions=(1<<OBJECT_COUNT)-1;buddy.energy=800;buddy.lastAttentionEpoch=0;
  buddy.seed=0x12345678UL;
  buddy.traits={50,50,50,50,50,50};
  for(auto &affection:buddy.affection) affection=0;
  buddy.favoriteObject=OBJECT_COUNT;
  buddy.currentBehavior=BEHAVIOR_OBSERVE;
  visibleMoment=MOMENT_WATCH;
  std::set<int> moments;
  uint8_t momentFrames[6][5000];
  for(int i=0;i<12;i++){
    updateBuddyMoment(buddy.birthEpoch+i*600,true,false);moments.insert(visibleMoment);
    if(i<6){currentView=VIEW_BUDDY;renderBuddy(true);memcpy(momentFrames[i],display->buffer,5000);}
  }
  assert(moments.size()>=4);
  std::ofstream momentsImage(buddyTestTempPath("buddy-moments.pbm"));momentsImage<<"P1\n1200 200\n";
  for(int y=0;y<200;y++){for(int i=0;i<6;i++)for(int x=0;x<200;x++)momentsImage<<((momentFrames[i][y*25+x/8]>>(7-x%8))&1)<<' ';momentsImage<<'\n';}
  momentsImage.close();
  std::ofstream tasksImage(buddyTestTempPath("buddy-tasks.pbm"));tasksImage<<"P1\n1600 600\n";
  static uint8_t taskFrames[24][5000];
  for(int i=0;i<24;i++){
    visibleMoment=(BuddyMoment)(MOMENT_WATER+i);visibleBehavior=behaviorForMoment(visibleMoment);
    currentView=VIEW_BUDDY;renderBuddy(true);memcpy(taskFrames[i],display->buffer,5000);
  }
  for(int row=0;row<3;row++)for(int y=0;y<200;y++){
    for(int i=0;i<8;i++)for(int x=0;x<200;x++)
      tasksImage<<((taskFrames[row*8+i][y*25+x/8]>>(7-x%8))&1)<<' ';
    tasksImage<<'\n';
  }
  tasksImage.close();
  // Multiple year-long lives: bounded values, spontaneous sleep, persistent world,
  // autonomous survival, complete offline catch-up and inherited family features.
  bool sawBoy=false,sawGirl=false;
  for(int trial=0;trial<24;trial++) {
    start();uint32_t born=buddy.birthEpoch;
    sawBoy|=buddySexFromSeed(buddy.seed)==BUDDY_BOY;
    sawGirl|=buddySexFromSeed(buddy.seed)==BUDDY_GIRL;
    uint32_t lifespan = buddyLifespanDays(buddy.seed);
    updateBuddySimulation(born+lifespan*86400UL);
    assert(!buddy.alive&&buddy.lastUpdateEpoch==buddy.deathEpoch);
    assert(buddy.behaviorCounts[BEHAVIOR_SLEEP]>0&&buddy.energy<=1000&&buddy.health<=1000);
    uint32_t hours=0;for(auto count:buddy.behaviorCounts)hours+=count;assert(hours==lifespan*24);
    assert(buddy.eventCount>0&&buddy.possessions!=0);
    auto mark=buddy.familyMarking,favorite=buddy.favoriteObject;
    buddy.memorialShown=true;updateBuddySimulation(buddy.deathEpoch+86401);
    assert(buddy.alive&&buddy.generation==2&&buddy.familyMarking==mark&&buddy.inheritedObject==favorite);
    sawBoy|=buddySexFromSeed(buddy.seed)==BUDDY_BOY;
    sawGirl|=buddySexFromSeed(buddy.seed)==BUDDY_GIRL;
    assert(nvs["buddy-archive/life0"].size()==sizeof(BuddySave));
    sdAvailable=true;retryBuddyArchives();assert(nvs["buddy-archive/life0"].empty());
  }
  assert(sawBoy&&sawGirl);
  // A full pending archive queue holds the old life, preserving its identity.
  start();updateBuddySimulation(buddy.birthEpoch+(uint32_t)buddyLifespanDays(buddy.seed)*86400UL);
  auto dead=buddy;Preferences archives;archives.begin("buddy-archive",false);
  for(int i=0;i<8;i++) {buddy.seed=100+i;BuddySave entry=packedLife();char key[8];snprintf(key,sizeof(key),"life%d",i);archives.putBytes(key,&entry,sizeof(entry));}
  buddy=dead;buddy.memorialShown=true;updateBuddySimulation(buddy.deathEpoch+86401);
  assert(!buddy.alive&&buddy.seed==dead.seed&&archiveBlocked);
  // Charge evidence needs an uninterrupted rise, cannot renew itself while
  // already latched, and expires; full voltage never masquerades as USB.
  usb=false;batteryChargeTrend=false;previousBatteryV=0;ticks=0;
  updateBuddyPowerEstimate(4.2f);assert(!buddyExternalPowerPresent());
  ticks=600000;updateBuddyPowerEstimate(4.21f);
  ticks+=600000;updateBuddyPowerEstimate(4.211f); // flat/noisy breaks the streak
  ticks+=600000;updateBuddyPowerEstimate(4.22f);
  ticks+=600000;updateBuddyPowerEstimate(4.23f);assert(!batteryChargeTrend);
  ticks+=600000;updateBuddyPowerEstimate(4.24f);assert(batteryChargeTrend);
  uint32_t evidenceAt=lastChargeEvidenceMs;
  ticks+=600000;updateBuddyPowerEstimate(4.25f);assert(lastChargeEvidenceMs==evidenceAt);
  ticks+=46UL*60000;updateBuddyPowerEstimate(4.22f);assert(!batteryChargeTrend);
  usb=true;ticks+=1000;assert(buddyExternalPowerPresent());usb=false;ticks+=1000;assert(!buddyExternalPowerPresent());
  // Exercise all views, stages and representative visual genomes under ASan.
  start();for(int h=1;h<=240;h++)liveWorldHour(rawEpoch+h*3600,BEHAVIOR_PLAY,true);
  for(int stage=0;stage<6;stage++)for(int seed=1;seed<30;seed++) {
    buddy.seed=seed*93737;buddy.currentStage=stage;
    for(int view=0;view<VIEW_COUNT;view++){currentView=(BuddyView)view;renderBuddy(true);}
  }
  // Page changes use a white-cleaning partial pass plus the new partial frame.
  currentView=VIEW_BUDDY;partialReady=false;renderBuddy(true);
  uint8_t awakePixels[5000];memcpy(awakePixels,display->buffer,5000);
  renderingDoze=true;renderBuddy(true);renderingDoze=false;
  assert(memcmp(awakePixels,display->buffer,5000)!=0); // doze is visibly distinct on the same home page
  sleepSdCardPresent=false;renderSleepFace();
  uint8_t noCardSleep[5000];memcpy(noCardSleep,display->buffer,5000);
  sleepSdCardPresent=true;renderSleepFace();
  assert(memcmp(noCardSleep,display->buffer,5000)!=0);
  sleepSdCardPresent=false;
  std::ofstream dozeImage(buddyTestTempPath("buddy-doze.pbm"));dozeImage<<"P1\n200 200\n";
  for(int y=0;y<200;y++)for(int x=0;x<200;x++)
    dozeImage<<((display->buffer[y*25+x/8]>>(7-x%8))&1)<<' ';
  dozeImage.close();
  // The house is legible at each major stage without adding a new screen.
  BuddyHouseState savedHouse=buddyHouse;
  uint8_t houseFrames[5][5000];
  BuddyHousePhase phases[]={HOUSE_EMPTY,HOUSE_BUILDING,HOUSE_OCCUPIED,HOUSE_OCCUPIED,HOUSE_RUIN};
  for(int i=0;i<5;i++) {
    buddyHouse.phase=phases[i];buddyHouse.work=i==1?350:BUDDY_HOUSE_WORK_TARGET;
    buddyHouse.condition=i==3?200:1000;
    currentView=VIEW_BUDDY;renderBuddy(true);memcpy(houseFrames[i],display->buffer,5000);
    if(i)assert(memcmp(houseFrames[i],houseFrames[i-1],5000)!=0);
  }
  buddyHouse=savedHouse;
  std::ofstream houseImage(buddyTestTempPath("buddy-houses.pbm"));houseImage<<"P1\n1000 200\n";
  for(int y=0;y<200;y++) {
    for(int i=0;i<5;i++) for(int x=0;x<200;x++)
      houseImage<<((houseFrames[i][y*25+x/8]>>(7-x%8))&1)<<' ';
    houseImage<<'\n';
  }
  houseImage.close();
  display->fullUpdates=display->partialUpdates=0;
  currentView=VIEW_ABOUT;cleanPartialRequested=true;renderBuddy(false);
  assert(display->fullUpdates==0&&display->partialUpdates==2);
  display->fullUpdates=display->partialUpdates=0;
  currentView=VIEW_SETTINGS;settingsSelection=0;renderBuddy(false);
  static_assert(SETTINGS_COUNT == 3, "retired auto-doze setting must stay hidden");
  settingsSelection=SETTINGS_ORIENTATION;renderBuddy(true);
  assert(display->buffer[94*25] & 0x08); // third-row highlight at x=4
  assert(!(display->buffer[124*25] & 0x08)); // no obsolete fourth row
  display->fullUpdates=display->partialUpdates=0;
  settingsSelection=1;cleanPartialRequested=false;renderBuddy(false);
  assert(display->fullUpdates==0&&display->partialUpdates==1);
  // Rotation affects the display, but portrait exports remain canonically upright.
  memset(display->buffer,0,5000);displayRotation180=true;buddyPixel(10,20);
  assert(display->buffer[(179)*25+189/8]&(0x80>>(189%8)));
  displayRotation180=false;
  // Contact sheet: the complete three-screen UI at native resolution.
  buddy.currentStage=STAGE_CHILD;
  std::ofstream image("/tmp/buddy-review.pbm");image<<"P1\n600 200\n";
  uint8_t frames[3][5000];int views[]={VIEW_BUDDY,VIEW_ABOUT,VIEW_SETTINGS};
  for(int i=0;i<3;i++){currentView=(BuddyView)views[i];renderBuddy(true);memcpy(frames[i],display->buffer,5000);}
  for(int y=0;y<200;y++){for(int i=0;i<3;i++)for(int x=0;x<200;x++)image<<((frames[i][y*25+x/8]>>(7-x%8))&1)<<' ';image<<'\n';}
  image.close();
  testLegacyWorks();
  testItemArt();
  testCableMomentRepertoire();
  testStargazeArtwork();
  testMemorialArtwork();
  testActivityArtIsolation();
  exportArtworkGallery();
  testTwentyConsecutiveAutonomousGenerations();
  printf("PASS: migration, saves, clock, SD, projects, inner life, long ambitions and lasting legacies, house lifecycle/recovery/display, mandatory first start/sex/inheritance, nearby protocol/handshake/recovery/scene, sleep/wake sound policy, voice gate, doze display, 24 lifetimes, 20 consecutive autonomous generations, power\n");
}
#endif
