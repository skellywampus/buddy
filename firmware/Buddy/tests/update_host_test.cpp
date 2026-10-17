// Executes the production OTA-completion path without flash or an SD card.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
#include "../buddy_update_format.h"
#include "../buddy_battery.h"
uint32_t ticks=0,lastFeed=0,readCost=40;
unsigned watchdogFeeds=0;
bool shortRead=false,hashBad=false,writeBad=false;
struct WatchdogExpired {};
void advanceTime(uint32_t amount){ticks+=amount;if(ticks-lastFeed>30000)throw WatchdogExpired{};}
std::vector<uint8_t> payload;
struct File {
  size_t pos=0;
  operator bool()const{return !payload.empty();}
  size_t size(){return payload.size();}
  size_t read(uint8_t *out,size_t n){
    advanceTime(readCost);
    if(shortRead)return 0;
    n=std::min(n,payload.size()-pos);memcpy(out,payload.data()+pos,n);pos+=n;return n;
  }
  void close(){}
};
struct MockSD { bool exists(const char *){return !payload.empty();} File open(const char *,const char *){return {};} } SD_MMC;
const char *FILE_READ="r";
struct MockDisplay {
  bool ok=true;bool healthy(){return ok;}
  void EPD_Clear(){}void EPD_Init(){}void EPD_Display(){}
} screen,*display=&screen;
std::string hint;
void buddyRect(int,int,int,int){}void buddyLine(int,int,int,int){}
void buddyCentered(int y,const char *text,int){if(y==158)hint=text;}
bool partialReady=false,needsRender=false,hourlyDozeWake=false,sdAvailable=true;
bool storageLocked=false,saveFailed=false,clockFault=false,introLocked=false,introSaveFailed=false;
bool houseLocked=false,houseSaveFailed=false,ambitionLocked=false,ambitionSaveFailed=false;
bool legacyLocked=false,legacySaveFailed=false,nearbyStorageLocked=false,nearbySaveFailed=false;
bool buddyCompletionPending=false,watchdogReady=true,buddyButtonEvents=true;
bool buddyLightSleepWakeReady=true;
int partialCount=0;float latestBatteryV=4.1f;
void delay(int ms){advanceTime(ms);}void renderBuddy(bool){}
void esp_task_wdt_reset(){++watchdogFeeds;lastFeed=ticks;}
struct MockSerial { template<class... T>void printf(const char *,T...){} } Serial;
bool usb_serial_jtag_is_connected(){return false;}
using esp_err_t=int;using esp_ota_img_states_t=int;
const int ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NOT_FOUND=-2;
const int ESP_OTA_IMG_UNDEFINED=-1,ESP_OTA_IMG_PENDING_VERIFY=1,ESP_OTA_IMG_VALID=2;
struct esp_partition_t {uint32_t address,size;} partition;
esp_partition_t nextPartition={0x260000,0x250000};
const esp_partition_t *esp_ota_get_next_update_partition(void *){return &nextPartition;}
const esp_partition_t *esp_ota_get_running_partition(){return &partition;}
int queryResult=ESP_OK,confirmResult=ESP_OK,state=ESP_OTA_IMG_PENDING_VERIFY;
unsigned confirmations=0,releases=0;
int esp_ota_get_state_partition(const esp_partition_t *,int *out){*out=state;return queryResult;}
int esp_ota_mark_app_valid_cancel_rollback(){++confirmations;if(confirmResult==ESP_OK)state=ESP_OTA_IMG_VALID;return confirmResult;}
void releaseBuddyMigrationProtection(){++releases;}
struct MockUpdate {
  size_t bytes=0;bool aborted=false;
  bool begin(uint32_t,int){bytes=0;return true;}
  size_t write(uint8_t *,size_t n){advanceTime(40);if(writeBad)return 0;bytes+=n;return n;}
  void abort(){aborted=true;}bool end(){return true;}
} Update;
int U_FLASH=0;
struct mbedtls_sha256_context {};
void mbedtls_sha256_init(mbedtls_sha256_context *){}
void mbedtls_sha256_free(mbedtls_sha256_context *){}
int mbedtls_sha256_starts(mbedtls_sha256_context *,int){return 0;}
int mbedtls_sha256_update(mbedtls_sha256_context *,const uint8_t *,size_t){return hashBad?-1:0;}
int mbedtls_sha256_finish(mbedtls_sha256_context *,uint8_t *out){memset(out,0,32);return 0;}
uint32_t pending=BUDDY_FIRMWARE_VERSION;bool removeOk=true,preferencesOk=true;
struct Preferences {
  bool begin(const char *,bool){return preferencesOk;}void end(){}
  size_t putUInt(const char *,uint32_t){return 4;}
  uint32_t getUInt(const char *,uint32_t){return pending;}
  bool remove(const char *){if(!removeOk)return false;pending=0;return true;}
};
struct Restarted {};
struct MockESP {void restart(){throw Restarted{};}} ESP;
#include "../buddy_update.inc"
int main(){
  bool *faults[]={&storageLocked,&saveFailed,&clockFault,&introLocked,&introSaveFailed,
    &houseLocked,&houseSaveFailed,&ambitionLocked,&ambitionSaveFailed,&legacyLocked,
    &legacySaveFailed,&nearbyStorageLocked,&nearbySaveFailed,&buddyCompletionPending};
  for(bool *fault:faults){
    *fault=true;assert(!finishBuddySdUpdate());*fault=false;
    assert(confirmations==0 && releases==0 && pending==BUDDY_FIRMWARE_VERSION);
  }
  for(bool *ready: {&watchdogReady,&buddyButtonEvents,&screen.ok,&buddyLightSleepWakeReady}){
    *ready=false;assert(!finishBuddySdUpdate());*ready=true;
    assert(confirmations==0 && releases==0);
  }
  queryResult=ESP_FAIL;assert(!finishBuddySdUpdate());assert(confirmations==0&&releases==0);
  queryResult=ESP_OK;confirmResult=ESP_FAIL;
  assert(!finishBuddySdUpdate());assert(confirmations==1&&releases==0&&pending);
  confirmResult=ESP_OK;removeOk=false;
  assert(!finishBuddySdUpdate());assert(confirmations==2&&pending);
  removeOk=true;assert(finishBuddySdUpdate());
  assert(confirmations==2&&pending==0&&hint=="SLEEP TO REMOVE CARD");
  assert(hint.find("YOU MAY REMOVE")==std::string::npos);
  assert(!finishBuddySdUpdate()&&confirmations==2);
  queryResult=ESP_ERR_NOT_FOUND;state=ESP_OTA_IMG_UNDEFINED;
  assert(!finishBuddySdUpdate()&&confirmations==2);
  reportBuddyUpdateValidation(); // same readiness probe used by private updater
  ticks=lastFeed=0;watchdogFeeds=0;
  std::vector<uint8_t> image(2UL*1024*1024,0);
  image[0]=0xe9;image[12]=9;
  std::string tag="BUDDY:WS154V2-BW:"+std::to_string(BUDDY_FIRMWARE_VERSION+1)+"UL";
  memcpy(image.data()+64,tag.c_str(),tag.size()+1);
  BuddyUpdateHeader header={};
  memcpy(header.magic,"BUDDYUP1",8);header.format=1;
  memcpy(header.target,BUDDY_UPDATE_TARGET,sizeof(header.target));
  header.release=BUDDY_FIRMWARE_VERSION+1;header.imageSize=image.size();
  payload=image;File slow;
  assert(hashUpdateImage(slow,header));
  assert(ticks>30000 && watchdogFeeds==1024);
  // Whole production install executes two reads/hash passes and a slow write.
  payload.resize(sizeof(header)+image.size());
  memcpy(payload.data(),&header,sizeof(header));
  memcpy(payload.data()+sizeof(header),image.data(),image.size());
  ticks=lastFeed=0;watchdogFeeds=0;
  try{checkBuddySdUpdate();assert(false);}catch(const Restarted &){}
  assert(Update.bytes==image.size() && watchdogFeeds==2048 && ticks>120000);
  // Failed progress never pets the watchdog, and a hung single read expires.
  payload=image;ticks=lastFeed=0;watchdogFeeds=0;shortRead=true;File bad;
  assert(!hashUpdateImage(bad,header)&&watchdogFeeds==0);shortRead=false;
  hashBad=true;File badHash;assert(!hashUpdateImage(badHash,header)&&watchdogFeeds==0);hashBad=false;
  ticks=lastFeed=0;readCost=30001;File hung;
  try{hashUpdateImage(hung,header);assert(false);}catch(const WatchdogExpired &){}
  assert(watchdogFeeds==0);
  puts("PASS: production OTA acceptance faults, failed confirmation/removal retries, ready peripherals, and safe card-removal instructions");
}
