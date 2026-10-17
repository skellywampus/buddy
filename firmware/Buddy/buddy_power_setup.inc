void setupBuddyPower() {
  // BAT_Control holds the entire system on after PWR is released. Assert it
  // before any slow boot work; it remains HIGH through ordinary light sleep.
  pinMode(VBAT_PWR_PIN, OUTPUT);
  digitalWrite(VBAT_PWR_PIN, HIGH);
  pinMode(EPD_PWR_PIN, OUTPUT); pinMode(AUDIO_PWR_PIN, OUTPUT); pinMode(AUDIO_PA_PIN, OUTPUT);
  pinMode(EPD_TP_RST_PIN, OUTPUT); pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, LOW); digitalWrite(AUDIO_PWR_PIN, HIGH); digitalWrite(AUDIO_PA_PIN, LOW);
  digitalWrite(EPD_TP_RST_PIN, LOW); setBuddyLed(false);
}
