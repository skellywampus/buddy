void sampleBuddyClimate() {
  Shtc3Reading reading = readShtc3();
  if (reading.ok) {
    climateConsecutiveFailures = 0;
    climateLastGoodMs = millis();
    climateTempSamples[climateSampleIndex] = reading.tempC;
    climateHumiditySamples[climateSampleIndex] = reading.humidity;
    climateSampleIndex = (climateSampleIndex + 1) % 5;
    if (climateSampleCount < 5) climateSampleCount++;
    float tempTotal=0.0f, humidityTotal=0.0f;
    for (uint8_t i=0;i<climateSampleCount;i++) {
      tempTotal += climateTempSamples[i]; humidityTotal += climateHumiditySamples[i];
    }
    latestTempC = tempTotal / climateSampleCount;
    latestHumidity = humidityTotal / climateSampleCount;
    latestClimateValid = true;
  } else {
    if (climateConsecutiveFailures < UINT8_MAX) ++climateConsecutiveFailures;
    if (climateReadFailures < UINT32_MAX) climateReadFailures++;
    // A cached value is not a new measurement. Stop using it as live input
    // and retry in two seconds; recovery starts a new smoothing window.
    latestClimateValid = false;
    climateSampleCount = climateSampleIndex = 0;
  }
}
