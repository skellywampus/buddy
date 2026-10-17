// Execute the production sensor sampler, not a duplicated policy.
#include <cassert>
#include <cstdint>
#include <cstdio>
struct Shtc3Reading { bool ok; float tempC, humidity; } reading;
Shtc3Reading readShtc3(){return reading;}
uint32_t ticks=0,climateLastGoodMs=0,climateReadFailures=0;
uint32_t millis(){return ticks;}
float climateTempSamples[5]={},climateHumiditySamples[5]={};
uint8_t climateSampleIndex=0,climateSampleCount=0;
uint8_t climateConsecutiveFailures=0;
float latestTempC=0,latestHumidity=0;
bool latestClimateValid=false;
#include "../buddy_climate.inc"
int main(){
  reading={true,23,40};ticks=1000;sampleBuddyClimate();
  assert(latestClimateValid && climateLastGoodMs==1000 && climateSampleCount==1);
  reading.ok=false;ticks+=120000;sampleBuddyClimate();
  assert(!latestClimateValid && climateSampleCount==0 && climateReadFailures==1);
  for(int i=0;i<30;++i){ticks+=2000;sampleBuddyClimate();assert(!latestClimateValid);}
  assert(climateLastGoodMs==1000);
  reading={true,31,55};sampleBuddyClimate();
  assert(latestClimateValid && latestTempC==31 && latestHumidity==55);
  assert(climateConsecutiveFailures==0);
  ticks=0xfffffff0u;sampleBuddyClimate();
  ticks+=120000;reading.ok=false;sampleBuddyClimate();assert(!latestClimateValid);
  climateReadFailures=UINT32_MAX;sampleBuddyClimate();assert(climateReadFailures==UINT32_MAX);
  puts("PASS: production climate freshness, failure retry readiness, clean recovery smoothing and rollover");
}
