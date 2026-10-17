/**
 * Update the external-power estimate.
 *
 * The board does not route its charger's status output to the ESP32. A USB
 * host can be detected directly from USB SOF packets. For charge-only USB
 * supplies, use a deliberately conservative voltage-trend latch: three
 * consecutive rising samples indicate charging, while two falling samples
 * clear that evidence after the cable is removed. A flat/noisy sample breaks
 * either streak so ordinary battery movement cannot accumulate indefinitely.
 */
static uint32_t buddyLastTrendSampleMs = 0;
void updateBuddyPowerEstimate(float voltage) {
  usbHostPowerPresent = usb_serial_jtag_is_connected();
  lastUsbPowerCheckMs = millis();

  if (!buddyBatteryVoltageValid(voltage)) {
    risingBatterySamples = fallingBatterySamples = 0;
    previousBatteryV = 0;
  }
  // The voltage-rise heuristic was tuned to ten-minute samples. Faster
  // safety readings must not silently change charging/rotation semantics.
  bool trendDue = !buddyBatteryVoltageValid(previousBatteryV) ||
    millis() - buddyLastTrendSampleMs >= 10UL * 60UL * 1000UL;
  if (trendDue && buddyBatteryVoltageValid(voltage) && buddyBatteryVoltageValid(previousBatteryV)) {
    float delta = voltage - previousBatteryV;
    if (delta >= 0.006f) {
      risingBatterySamples = min<uint8_t>(3, risingBatterySamples + 1);
      fallingBatterySamples = 0;
      if (risingBatterySamples >= 3 && !batteryChargeTrend) {
        batteryChargeTrend = true;
        lastChargeEvidenceMs = millis();
      }
    } else if (delta <= -0.006f) {
      fallingBatterySamples = min<uint8_t>(2, fallingBatterySamples + 1);
      risingBatterySamples = 0;
      if (fallingBatterySamples >= 2) {
        batteryChargeTrend = false;
        fallingBatterySamples = 0;
      }
    } else {
      // Two isolated upward jitters must not become a charging claim.
      risingBatterySamples = 0;
      fallingBatterySamples = 0;
    }
  }
  // Flat full-charge voltage alone cannot prove that a charger is connected.
  // Expire old evidence; only a new uninterrupted rise can assert it again.
  if (batteryChargeTrend && millis() - lastChargeEvidenceMs > 45UL * 60UL * 1000UL) {
    batteryChargeTrend = false;
    risingBatterySamples = 0;
    fallingBatterySamples = 0;
  }
  if (trendDue && buddyBatteryVoltageValid(voltage)) {
    previousBatteryV = voltage;
    buddyLastTrendSampleMs = millis();
  }
}

bool buddyExternalPowerPresent() {
  // Refresh this quickly enough to notice an attached/detached computer,
  // without asking the USB peripheral for its state on every loop pass.
  if (millis() - lastUsbPowerCheckMs >= 1000) {
    usbHostPowerPresent = usb_serial_jtag_is_connected();
    lastUsbPowerCheckMs = millis();
  }
  return usbHostPowerPresent || batteryChargeTrend;
}
