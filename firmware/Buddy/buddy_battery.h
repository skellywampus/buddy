#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
static constexpr uint32_t BUDDY_POWER_INTERVAL_MS = 60000UL;
inline bool buddyBatteryVoltageValid(float voltage) {
  // Comparisons also reject NaN/Inf. A deeply discharged cell must still
  // reach the low-voltage safeguard rather than be mistaken for ADC failure.
  return voltage >= 2.5f && voltage <= 4.5f;
}
inline float buddyBatteryMedian(float *samples, uint8_t count) {
  if (count < 3) return 0;
  for (uint8_t i=1;i<count;++i) {
    float value=samples[i]; uint8_t j=i;
    while(j && samples[j-1]>value){samples[j]=samples[j-1];--j;}
    samples[j]=value;
  }
  return count%2 ? samples[count/2] : (samples[count/2-1]+samples[count/2])/2;
}
inline void buddyBatteryText(char *text, size_t size, float voltage) {
  if (!buddyBatteryVoltageValid(voltage)) {snprintf(text,size,"?%%");return;}
  // Voltage-only estimate, deliberately not presented as a fuel gauge.
  unsigned percent=voltage<=3.3f ? 0 : voltage>=4.2f ? 100 :
    (unsigned)((voltage-3.3f)*100/0.9f+0.5f);
  snprintf(text,size,"%u%%",percent);
}
struct BuddyBatteryNotice { int16_t percent = -1; bool valid = false; };
inline bool buddyBatteryNoticeDue(BuddyBatteryNotice &notice, float voltage) {
  bool valid = buddyBatteryVoltageValid(voltage);
  int percent = valid ? (voltage<=3.3f ? 0 : voltage>=4.2f ? 100 :
    (int)((voltage-3.3f)*100/0.9f+0.5f)) : -1;
  int delta = percent-notice.percent;
  bool due = valid != notice.valid || (valid && (delta>=5 || delta<=-5));
  if (due) { notice.percent=percent; notice.valid=valid; }
  return due;
}
