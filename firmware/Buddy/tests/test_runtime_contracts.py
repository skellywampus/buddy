"""Source contracts supplement executable policies for ESP-only workers."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

class RuntimeContracts(unittest.TestCase):
    def test_battery_latch_is_asserted_before_boot_work_and_never_sample_switched(self):
        app=(ROOT/"buddy_app.inc").read_text()
        boot=app[app.index("void setup() {"):app.index("void loop() {")]
        self.assertLess(boot.index("setupBuddyPower();"),boot.index("Serial.begin"))
        self.assertLess(boot.index("releaseBuddyPowerHolds();"),boot.index("setupBuddyPower();"))
        power=(ROOT/"buddy_power_setup.inc").read_text()
        self.assertIn("digitalWrite(VBAT_PWR_PIN, HIGH)",power)
        self.assertNotIn("digitalWrite(VBAT_PWR_PIN, LOW)",power)
        acquisition=(ROOT/"buddy_battery_acquisition.inc").read_text()
        self.assertNotIn("digitalWrite",acquisition)
        # Intentional manual/critical power-off is still the sole LOW write.
        self.assertEqual(app.count("digitalWrite(VBAT_PWR_PIN, LOW)"),1)
        sleep=app[app.index("void enterBuddySleep("):app.index("static bool persistCableClockRecoveryMarker")]
        self.assertIn("digitalWrite(VBAT_PWR_PIN, LOW)",sleep)

    def test_power_services_and_failure_paths_are_integrated(self):
        app=(ROOT/"buddy_app.inc").read_text()
        simulation=app[app.index("if (millis() - lastSimulationMs"):app.index("// Temperature has")]
        self.assertNotIn("sampleBuddyPower()",simulation)
        self.assertIn("work.powerPeriodMs = BUDDY_POWER_INTERVAL_MS",app)
        self.assertIn("work.micRetryPending",app)
        self.assertIn("buddyLightSleepWakeReady &&",app)
        self.assertIn("buddyLightSleepRetry.finish(slept,millis())",app)
        self.assertIn("if (watchdogReady) enableLoopWDT()",app)
        audio=(ROOT/"buddy_audio.inc").read_text()
        mic=(ROOT/"buddy_mic.inc").read_text()
        self.assertEqual(audio.count("buddyAudioPowerAllowed"),2)
        self.assertEqual(mic.count("buddyAudioPowerAllowed"),3)
        driver=(ROOT.parents[1]/"third_party/waveshare_epaper_display/epaper_driver_bsp.cpp").read_text()
        partial=driver[driver.index("void epaper_driver_display::EPD_Init_Partial()"):driver.index("void epaper_driver_display::EPD_DisplayPart()")]
        self.assertIn("EPD_PowerOff()",partial)
        self.assertIn("activate(0xcf)",driver)
        self.assertIn("epaperWaitIdle(",driver)
        self.assertIn("fault_ = true",driver)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT",driver)
        frame=(ROOT.parents[1]/"third_party/waveshare_epaper_display/epaper_frame.h").read_text()
        self.assertIn("epaperFrameLayout",frame)
        self.assertIn("epaperWritePixel",driver)

    def test_speech_allocates_before_workers_and_never_aborts_on_memory(self):
        sr = (ROOT / "buddy_esp32_sr.c").read_text()
        start = sr[sr.index("esp_err_t sr_start("):sr.index("esp_err_t sr_stop(void)")]
        self.assertLess(start.index("afe_in_buffer = heap_caps_malloc"), start.index("xTaskCreatePinnedToCore"))
        self.assertIn("ESP_ERR_NO_MEM, err", start)
        self.assertNotIn("esp_system_abort", sr)

    def test_pause_acknowledges_workers_before_i2s_release(self):
        sr = (ROOT / "buddy_esp32_sr.c").read_text()
        pause = sr[sr.index("esp_err_t sr_pause(void)"):sr.index("esp_err_t sr_resume(void)")]
        self.assertIn("xEventGroupWaitBits", pause)
        self.assertIn("FEED_PAUSED | DETECT_PAUSED", pause)
        self.assertIn("ESP_ERR_TIMEOUT", pause)
        resume = sr[sr.index("esp_err_t sr_resume(void)"):]
        self.assertIn("FEED_PAUSED | DETECT_PAUSED", resume)
        self.assertIn("ESP_ERR_TIMEOUT", resume)
        self.assertIn("fetch_with_delay", sr)
        self.assertNotIn("vTaskDelay(100)", sr)  # milliseconds are converted to ticks
        speech = (ROOT / "buddy_speech.inc").read_text()
        self.assertIn("millis() - started < 100UL", speech)
        self.assertNotIn("delay(140)", speech)
        mic = (ROOT / "buddy_mic.inc").read_text()
        stop = mic[mic.index("void stopBuddyMic()"):mic.index('#include "buddy_sound_settings.inc"')]
        self.assertLess(stop.index("pauseBuddySpeech()"), stop.index("i2s_del_channel"))
        self.assertIn("stopBuddySpeech(); // bounded stop", speech)

    def test_cable_worker_cannot_clear_or_delete_owner_handle(self):
        cable = (ROOT / "buddy_cable.inc").read_text()
        worker = cable[cable.index("static void cableWorker"):cable.index("void refreshCablePacket")]
        self.assertNotIn("cableSetTaskHandle", worker)
        self.assertNotIn("vTaskDelete", worker)
        self.assertIn("vTaskSuspend(nullptr)", worker)
        self.assertNotIn("vTaskDelete(cableTaskHandle())", cable)

    def test_audio_has_an_independent_random_stream(self):
        self.assertNotIn("buddyRandom()", (ROOT / "buddy_audio.inc").read_text())

    def test_sensor_retry_deadline_matches_runtime(self):
        app = (ROOT / "buddy_app.inc").read_text()
        self.assertIn("work.climatePeriodMs = buddyClimateInterval(latestClimateValid, climateConsecutiveFailures)", app)
        self.assertIn("climateInterval = buddyClimateInterval(latestClimateValid, climateConsecutiveFailures)", app)

if __name__ == "__main__":
    unittest.main()
