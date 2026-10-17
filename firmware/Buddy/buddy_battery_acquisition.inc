float readBatteryVoltage() {
  // GPIO17 is BAT_Control: the board's battery-to-system power latch, not
  // an ADC-divider enable. Sampling must never interrupt battery power.
  uint32_t mv = analogReadMilliVolts(BAT_ADC_PIN);
  return mv > 0 ? (mv * 2.0f / 1000.0f) : 0.0f;
}
