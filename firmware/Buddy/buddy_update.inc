// SD updates are only checked on an ordinary boot, not during hourly doze
// housekeeping. No life-save fields or NVS partitions are changed.
static bool buddyStartupHealthy() {
  return !storageLocked && !saveFailed && !clockFault && !introLocked && !introSaveFailed &&
    !houseLocked && !houseSaveFailed && !ambitionLocked && !ambitionSaveFailed &&
    !legacyLocked && !legacySaveFailed && !nearbyStorageLocked && !nearbySaveFailed &&
    !buddyCompletionPending && display && display->healthy() && buddyButtonEvents && watchdogReady &&
    buddyLightSleepWakeReady;
}
static void drawUpdateScreen(const char *title, const char *detail, const char *hint) {
  if (!display) return;
  display->EPD_Clear();
  buddyRect(0, 0, 200, 200);
  buddyCentered(27, "BUDDY UPDATE", 1);
  buddyLine(8, 46, 191, 46);
  buddyCentered(70, title, 1);
  buddyCentered(97, detail, 1);
  buddyCentered(158, hint, 1);
  display->EPD_Init();
  display->EPD_Display();
  partialReady = false;
  partialCount = 0;
}

static void updateMessage(const char *title, const char *detail) {
  drawUpdateScreen(title, detail, "BUDDY IS SAFE");
  Serial.printf("UPDATE %s: %s\n", title, detail);
  delay(3500);
  needsRender = true;
  renderBuddy(true);
}

static bool hashUpdateImage(File &file, const BuddyUpdateHeader &header) {
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  int error = mbedtls_sha256_starts(&hash, 0);
  uint8_t buffer[2048];
  uint32_t remaining = header.imageSize;
  BuddyUpdateImageCheck image(header.release);
  while (!error && remaining) {
    size_t amount = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (file.read(buffer, amount) != amount) { error = -1; break; }
    error = mbedtls_sha256_update(&hash, buffer, amount);
    image.feed(buffer, amount);
    remaining -= amount;
    // Yielding alone does not reset the loop task watchdog. Feed only after
    // a complete, successfully hashed block; a hung read still times out.
    if (!error && watchdogReady) esp_task_wdt_reset();
    delay(0);
  }
  uint8_t digest[32] = {};
  if (!error) error = mbedtls_sha256_finish(&hash, digest);
  mbedtls_sha256_free(&hash);
  return !error && image.valid() && !memcmp(digest, header.imageSha256, sizeof(digest));
}

static void checkBuddySdUpdate() {
  if (hourlyDozeWake || !sdAvailable || !SD_MMC.exists(BUDDY_UPDATE_PATH)) return;
  File file = SD_MMC.open(BUDDY_UPDATE_PATH, FILE_READ);
  if (!file) { updateMessage("CANNOT READ CARD", "TRY ANOTHER CARD"); return; }
  BuddyUpdateHeader header = {};
  size_t size = file.size();
  const esp_partition_t *next = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (!next || !running || next->address == running->address) {
    file.close();
    updateMessage("UPDATE UNAVAILABLE", "USB SETUP NEEDED");
    return;
  }
  if (file.read((uint8_t *)&header, sizeof(header)) != sizeof(header)) {
    file.close(); updateMessage("BAD UPDATE FILE", "COPY FILE AGAIN"); return;
  }
  BuddyUpdateCheck check = buddyCheckUpdate(header, size, next->size);
  if (check == UPDATE_ALREADY_CURRENT) {
    file.close();
    Serial.printf("UPDATE skipped: release %lu is already installed or older\n",
                  (unsigned long)header.release);
    return;
  }
  if (check != UPDATE_READY) {
    file.close();
    if (check == UPDATE_BAD_TARGET) updateMessage("WRONG BOARD FILE", "BUDDY V2 ONLY");
    else updateMessage("BAD UPDATE FILE", "COPY FILE AGAIN");
    return;
  }
  // A failed write leaves the current slot intact, but avoid deliberately
  // starting a long flash operation on a nearly flat battery.
  if (!usb_serial_jtag_is_connected() &&
      (!buddyBatteryVoltageValid(latestBatteryV) || latestBatteryV < 3.65f)) {
    file.close(); updateMessage("CHARGE BUDDY", "THEN RESTART"); return;
  }
  drawUpdateScreen("CHECKING FILE", "PLEASE WAIT", "DO NOT REMOVE CARD");
  if (!hashUpdateImage(file, header)) {
    file.close(); updateMessage("FILE DAMAGED", "COPY FILE AGAIN"); return;
  }
  // Reopen rather than trust a seek on a flaky/removable card. The image is
  // hashed a second time as it is written, so a mid-install card change fails.
  file.close();
  file = SD_MMC.open(BUDDY_UPDATE_PATH, FILE_READ);
  BuddyUpdateHeader repeated = {};
  if (!file || file.size() != size ||
      file.read((uint8_t *)&repeated, sizeof(repeated)) != sizeof(repeated) ||
      memcmp(&header, &repeated, sizeof(header))) {
    file.close(); updateMessage("CARD READ ERROR", "TRY AGAIN"); return;
  }
  drawUpdateScreen("INSTALLING", "PLEASE WAIT", "DO NOT REMOVE CARD");
  if (!Update.begin(header.imageSize, U_FLASH)) {
    file.close(); updateMessage("NO UPDATE SPACE", "USB SETUP NEEDED"); return;
  }
  mbedtls_sha256_context hash;
  mbedtls_sha256_init(&hash);
  int hashError = mbedtls_sha256_starts(&hash, 0);
  uint8_t buffer[2048];
  uint32_t remaining = header.imageSize;
  bool written = !hashError;
  while (written && remaining) {
    size_t amount = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    if (file.read(buffer, amount) != amount) { written = false; break; }
    if (mbedtls_sha256_update(&hash, buffer, amount)) { written = false; break; }
    if (Update.write(buffer, amount) != amount) { written = false; break; }
    remaining -= amount;
    if (watchdogReady) esp_task_wdt_reset(); // verified read/hash/write progress
    delay(0);
  }
  uint8_t digest[32] = {};
  if (written && mbedtls_sha256_finish(&hash, digest)) written = false;
  mbedtls_sha256_free(&hash);
  file.close();
  if (!written || remaining || memcmp(digest, header.imageSha256, sizeof(digest))) {
    Update.abort(); updateMessage("INSTALL FAILED", "OLD BUDDY IS SAFE"); return;
  }
  if (!Update.end()) {
    updateMessage("INSTALL FAILED", "OLD BUDDY IS SAFE"); return;
  }
  Preferences updater;
  if (updater.begin("buddy-update", false)) {
    updater.putUInt("pending", header.release);
    updater.end();
  }
  drawUpdateScreen("UPDATE WRITTEN", "RESTARTING", "DO NOT REMOVE CARD");
  delay(500);
  ESP.restart();
}

static bool finishBuddySdUpdate() {
  // Do not abandon rollback or release old-format checkpoints until normal
  // operation, storage, clock, display, input and watchdog are actually ready.
  if (!buddyStartupHealthy()) return false;
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_err_t query = running ? esp_ota_get_state_partition(running, &state) : ESP_FAIL;
  if (query != ESP_OK && query != ESP_ERR_NOT_FOUND) return false;
  if (state == ESP_OTA_IMG_PENDING_VERIFY &&
      esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) return false;
  releaseBuddyMigrationProtection();
  Preferences updater;
  if (!updater.begin("buddy-update", false)) return false;
  uint32_t pending = updater.getUInt("pending", 0);
  bool completed = false;
  if (pending && pending == BUDDY_FIRMWARE_VERSION) {
    if (!updater.remove("pending")) { updater.end(); return false; }
    drawUpdateScreen("UPDATE COMPLETE", "BUDDY IS READY", "SLEEP TO REMOVE CARD");
    delay(2500);
    needsRender = true;
    renderBuddy(true);
    completed = true;
  }
  updater.end();
  return completed;
}

static void reportBuddyUpdateValidation() {
  finishBuddySdUpdate();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_partition_t *running = esp_ota_get_running_partition();
  bool ready = buddyStartupHealthy() && running &&
    esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_VALID;
  Serial.printf("USB_UPDATE_READY release=%lu ready=%u\n",
    (unsigned long)BUDDY_FIRMWARE_VERSION, ready);
}
