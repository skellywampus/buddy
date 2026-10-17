// Execute production acquisition, sampling and standby against fault mocks.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include "../buddy_types.h"
#include "../buddy_runtime_policy.h"
#include "../buddy_deadlines.h"
uint32_t ticks=0;uint32_t millis(){return ticks;}void delay(uint32_t ms){ticks+=ms;}
const int VBAT_PWR_PIN=7,BAT_ADC_PIN=8,HIGH=1,LOW=0,I2C_SHTC3_ADDR=0x70,OUTPUT=1;
const int EPD_PWR_PIN=1,AUDIO_PWR_PIN=2,AUDIO_PA_PIN=3,EPD_TP_RST_PIN=4,STATUS_LED_PIN=5;
bool batteryLatch=false,usbPower=false,pwrHeld=true;unsigned enabled=0,disabled=0;
bool systemPowered(){return usbPower||pwrHeld||batteryLatch;}
void pinMode(int,int){}
void setBuddyLed(bool){}
void digitalWrite(int pin,int value){
  if(pin!=VBAT_PWR_PIN)return;
  batteryLatch=value==HIGH;value?++enabled:++disabled;
}
#include "../buddy_power_setup.inc"
std::vector<uint32_t> adc;size_t adcIndex=0;
uint32_t analogReadMilliVolts(int pin){assert(pin==BAT_ADC_PIN&&systemPowered());return adc.at(adcIndex++);}
#include "../buddy_battery_acquisition.inc"
bool wakeOk=true,measureOk=true,crcOk=true,sleepOk=true;
std::vector<uint16_t> commands;
bool i2cWriteBytes(int,const uint8_t *data,size_t n){
  assert(n==2);uint16_t command=(data[0]<<8)|data[1];commands.push_back(command);
  return command==0x3517?wakeOk:command==0x7866?measureOk:sleepOk;
}
struct MockWire {
  size_t request=6,received=6;
  size_t requestFrom(int,size_t){return request;}
  size_t readBytes(uint8_t *bytes,size_t n){memset(bytes,0,n);return received;}
} Wire;
uint8_t crc8Shtc3(const uint8_t *,size_t){return crcOk?0:1;}
#include "../buddy_shtc3_acquisition.inc"
float latestBatteryV=0;
uint32_t lastPowerSampleMs=0;
BuddyBatteryNotice batteryNotice;
bool needsRender=false;
void updateBuddyPowerEstimate(float){}
#include "../buddy_battery_sample.inc"
int main(){
  // Battery-only cold boot, then release the power button. ADC failure must
  // not drop the rail either. These use the production setup/acquisition.
  setupBuddyPower();pwrHeld=false;assert(systemPowered()&&batteryLatch);
  adc={0};assert(readBatteryVoltage()==0&&batteryLatch&&enabled==1&&disabled==0);
  adc={1700,1690,1710,2200,1400};adcIndex=0;sampleBuddyPower();
  assert(latestBatteryV==3.4f&&batteryLatch&&lastPowerSampleMs==ticks&&needsRender);
  needsRender=false;adc={1701,1700,1700,1699,1700};adcIndex=0;sampleBuddyPower();
  assert(!needsRender&&batteryLatch); // tiny changes do not trigger e-paper updates
  adc={0,0,2300,100,1700};adcIndex=0;sampleBuddyPower();
  assert(latestBatteryV==0&&needsRender&&batteryLatch); // one usable reading is insufficient
  usbPower=true;setupBuddyPower();usbPower=false;
  assert(systemPowered()&&batteryLatch&&disabled==0); // unplug after USB boot
  ticks+=60000;adc={1700,1700,1700,1700,1700};adcIndex=0;sampleBuddyPower();
  assert(systemPowered()&&batteryLatch&&disabled==0); // subsequent minute check
  char text[8];buddyBatteryText(text,sizeof(text),0);assert(!strcmp(text,"?%"));
  assert(!buddyBatteryVoltageValid(NAN)&&!buddyBatteryVoltageValid(INFINITY));
  assert(!buddyAudioPowerAllowed(0,false)&&!buddyAudioPowerAllowed(3.49f,false));
  assert(buddyAudioPowerAllowed(3.6f,false)&&buddyAudioPowerAllowed(0,true));
  assert(buddyWaitForSleepRelease(false,true,5000)); // manual semantics unchanged
  assert(buddyWaitForSleepRelease(true,true,1999)&&!buddyWaitForSleepRelease(true,true,2000));
  assert(buddySleepWakePins(false,false,false,1,4)==5);
  assert(buddySleepWakePins(true,false,true,1,4)==4);
  assert(buddySleepWakePins(true,false,false,1,4)==0);
  BuddyLightSleepRetry retry;assert(retry.allowed(0));retry.finish(false,0xffffff00u);
  assert(!retry.allowed(0xffffff01u)&&!retry.allowed(1743)&&retry.allowed(1744));
  retry.finish(true,1744);assert(retry.allowed(1744)&&retry.failures==1);
  retry.failures=UINT32_MAX;retry.finish(false,0);assert(retry.failures==UINT32_MAX);
  assert(buddyBatteryVoltageValid(2.8f));
  BuddyCriticalBattery critical;
  assert(!buddyCriticalBatteryReading(critical,2.8f,false,0));
  assert(!buddyCriticalBatteryReading(critical,NAN,false,15000)&&critical.lowReadings==1);
  assert(!buddyCriticalBatteryReading(critical,0,false,20000)&&critical.lowReadings==1);
  assert(!buddyCriticalBatteryReading(critical,3.4f,false,30000));
  assert(buddyCriticalBatteryReading(critical,3.4f,false,45000));
  assert(!buddyCriticalBatteryReading(critical,3.6f,false,60000)&&!critical.lowReadings);
  for(int fault=0;fault<6;++fault){
    wakeOk=measureOk=crcOk=sleepOk=true;Wire.request=Wire.received=6;commands.clear();
    if(fault==0)wakeOk=false;
    if(fault==1)measureOk=false;
    if(fault==2)Wire.request=0;
    if(fault==3)Wire.received=0;
    if(fault==4)crcOk=false;
    if(fault==5)sleepOk=false;
    assert(!readShtc3().ok&&commands.back()==0xb098);
  }
  wakeOk=measureOk=crcOk=sleepOk=true;Wire.request=Wire.received=6;commands.clear();
  assert(readShtc3().ok&&commands.back()==0xb098);
  assert(buddyClimateInterval(false,1)==2000&&buddyClimateInterval(false,5)==30000);
  assert(buddyClimateInterval(false,10)==120000&&buddyClimateInterval(true,0)==120000);
  BuddyWorkSchedule work={};work.simulationPeriodMs=600000;work.climatePeriodMs=120000;
  work.powerPeriodMs=60000;assert(buddyNextWorkDelay(0,work)==60000);
  work.micRetryPending=true;assert(buddyNextWorkDelay(0,work)==30000);
  work.powerLastMs=0xfffffff0u;work.storageLastMs=work.powerLastMs;
  work.simulationLastMs=work.climateLastMs=work.powerLastMs;work.micRetryPending=false;
  assert(buddyNextWorkDelay(0x20,work)==60000-48);
  puts("PASS: battery-only boot/PWR release/USB unplug retain battery latch across successful and failed ADC samples; median/outlier/unknown battery, low voltage, sensor exits, deadlines and display standby");
}
