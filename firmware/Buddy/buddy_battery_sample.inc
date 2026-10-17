void sampleBuddyPower() {
  // A median rejects isolated load/ADC spikes without smoothing away a
  // sustained low cell voltage. At least three usable samples are required.
  float samples[5] = {};
  uint8_t validBatterySamples = 0;
  for (uint8_t i = 0; i < 5; i++) {
    float voltage = readBatteryVoltage();
    if (buddyBatteryVoltageValid(voltage)) {
      samples[validBatterySamples++] = voltage;
    }
    delay(3);
  }
  latestBatteryV = buddyBatteryMedian(samples, validBatterySamples);
  lastPowerSampleMs = millis();
  updateBuddyPowerEstimate(latestBatteryV);
  if (buddyBatteryNoticeDue(batteryNotice, latestBatteryV)) needsRender = true;
}
