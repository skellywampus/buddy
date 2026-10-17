#if BUDDY_WIRELESS_LINK_ENABLED
// A private service-data beacon: no name, chip MAC, or life seed is sent.
// Legacy advertising carries exactly one compact appearance + handshake packet.
static NimBLEUUID nearbyUuid("a92b8540-e61e-4f1c-95e3-5e3a83a45d41");
struct NearbyObservation { NearbyPacket packet; int8_t rssi; };
static QueueHandle_t nearbyQueue = nullptr;
static uint32_t lastNearbyScanMs = 0;
static uint32_t lastNearbyStartAttemptMs = 0;
static uint32_t lastNearbyPublishRetryMs = 0;
static uint32_t lastNearbyBurstMs = 0;
static uint32_t nearbyBurstStartedMs = 0;
static bool nearbyStackInitialized = false;
static uint32_t nearbyScanStarts = 0;
static uint32_t nearbyPacketsReceived = 0;

class BuddyNearbyScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *device) override {
    std::string data = device->getServiceData(nearbyUuid);
    NearbyObservation observation = {};
    if (decodeNearbyPacket((const uint8_t *)data.data(), data.size(), observation.packet) &&
        nearbyQueue) {
      observation.rssi = device->getRSSI();
      xQueueSend(nearbyQueue, &observation, 0);
    }
  }
} nearbyScanCallbacks;

static bool publishNearbyAdvertisement() {
  if (!nearbyRadioActive) return false;
  uint8_t bytes[NEAR_PACKET_BYTES];
  encodeNearbyPacket(currentNearbyPacket(), bytes);
  NimBLEAdvertisementData data;
  if (!data.setServiceData(nearbyUuid, bytes, sizeof(bytes))) return false;
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->stop();
  bool ok = advertising->setAdvertisementData(data) && advertising->start();
  Serial.printf("NEARBY publish=%u payload=%u sender=%08lx\n", ok,
    (unsigned)data.getPayload().size(), (unsigned long)currentNearbyPacket().sender);
  return ok;
}

void refreshNearbyAdvertisement() {
  if (nearbyRadioActive && !publishNearbyAdvertisement()) nearbyRadioError = 5;
}

bool startNearbyRadio() {
  if (nearbyRadioActive) return true;
  if (nearbyCrashGuard) { nearbyRadioError = 7; return false; }
  if (!nearbyDiscoveryActive() || storageLocked || nearbyStorageLocked || nearbySaveFailed ||
      clockFault || !buddy.alive || nearbyState.magic != NEAR_MAGIC || hourlyDozeWake) return false;
  lastNearbyStartAttemptMs = millis() ? millis() : 1;
  if (buddyMicActive) {
    stopBuddyMic();
    buddyMicRetryMs = millis() - 30000UL;
    delay(50); // sr_stop releases its worker and internal RAM before BLE init.
  }
  bool heapOk = heap_caps_check_integrity_all(true);
  Serial.printf("NEARBY start free_heap=%lu heap_ok=%u\n",
                (unsigned long)ESP.getFreeHeap(), heapOk);
  if (!heapOk) { nearbyCrashGuard = true; nearbyRadioError = 8; return false; }
  if (!nearbyQueue) nearbyQueue = xQueueCreate(8, sizeof(NearbyObservation));
  if (!nearbyQueue) { nearbyRadioError = 1; return false; }
  if (!nearbyStackInitialized) {
    nearbyInitMarker = NEARBY_INIT_MARKER;
    bool initialized = NimBLEDevice::init("");
    nearbyInitMarker = 0;
    Serial.printf("NEARBY init_result=%u free=%lu\n", initialized, (unsigned long)ESP.getFreeHeap());
    if (!initialized) { nearbyRadioError = 2; return false; }

    // Static-random address for this awake session, never the factory MAC.
    uint8_t address[6];
    uint32_t randomA = esp_random(), randomB = esp_random();
    for (uint8_t i = 0; i < 4; i++) address[i] = randomA >> (i * 8);
    address[4] = randomB;
    address[5] = (randomB >> 8) | 0xC0;
    if (!NimBLEDevice::setOwnAddr(address) ||
        !NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)) {
      nearbyRadioError = 3;
      NimBLEDevice::deinit(true);
      return false;
    }
    NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
    advertising->setConnectableMode(BLE_GAP_CONN_MODE_NON);
    advertising->setAdvertisingInterval(320); // 200 ms while discovery is enabled
    NimBLEScan *scanner = NimBLEDevice::getScan();
    scanner->setScanCallbacks(&nearbyScanCallbacks, false);
    scanner->setActiveScan(false);
    scanner->setInterval(100);
    scanner->setWindow(80);
    scanner->setMaxResults(0);
    nearbyStackInitialized = true;
  }
  nearbyRadioActive = true;
  if (!publishNearbyAdvertisement()) {
    nearbyRadioError = 4;
    stopNearbyRadio();
    return false;
  }
  Serial.printf("NEARBY advertising free=%lu\n", (unsigned long)ESP.getFreeHeap());
  // A normal burst lasts 30 seconds. The previous 30-second offset made
  // the first scan due after the radio had already shut down, so Buddies could
  // advertise but never discover one another.
  lastNearbyScanMs = buddyNearbyInitialLastScanMs(millis());
  nearbyBurstStartedMs = millis();
  nearbyRadioError = 0;
  return true;
}

bool startVoiceNearbySearch() {
  if (storageLocked || nearbyStorageLocked || clockFault || !buddy.alive) return false;
  if (!nearbyEnabled) nearbyVoiceSearchUntilMs = millis() + 5UL * 60UL * 1000UL;
  if (nearbyState.magic != NEAR_MAGIC || nearbyState.lifeSeed != buddy.seed)
    newNearbyLife();
  if (nearbySaveFailed || !startNearbyRadio()) {
    if (!nearbyEnabled) nearbyVoiceSearchUntilMs = 0;
    return false;
  }
  // Voice request scans immediately; normal background Nearby keeps its
  // existing five-minute schedule. No setting is persisted by this request.
  lastNearbyScanMs = buddyNearbyInitialLastScanMs(millis());
  return true;
}

void stopNearbyRadio() {
  if (!nearbyRadioActive) return;
  Serial.printf("NEARBY stop begin free=%lu scans=%lu\n", (unsigned long)ESP.getFreeHeap(),
    (unsigned long)nearbyScanStarts);
  NimBLEDevice::getScan()->stop();
  NimBLEDevice::getAdvertising()->stop();
  // Repeated full NimBLE teardown crashed its host task on the second burst
  // in the complete Buddy firmware. Keep the initialized stack for this awake
  // session; only the radio activities are stopped between bursts.
  Serial.printf("NEARBY stop done free=%lu\n", (unsigned long)ESP.getFreeHeap());
  nearbyRadioActive = false;
  lastNearbyBurstMs = millis() ? millis() : 1;
  buddyMicRetryMs = millis() - 30000UL;
  if (nearbyQueue) xQueueReset(nearbyQueue);
}

void shutdownNearbyStackForSleep() {
  stopNearbyRadio();
  if (!nearbyStackInitialized) return;
  // Deep sleep powers the radio down. Deinitialize once per awake session,
  // after display/audio work has given scan callbacks time to settle.
  bool ok = NimBLEDevice::deinit(true);
  Serial.printf("NEARBY sleep deinit=%u\n", ok);
  nearbyStackInitialized = false;
}

void serviceNearbyRadio() {
  if (nearbyCrashGuard) return;
  if (nearbyVoiceSearchUntilMs && !nearbyDiscoveryActive()) {
    nearbyVoiceSearchUntilMs = 0;
    nearbyEncounter = {};
    needsRender = true;
  }
  if (!nearbyDiscoveryActive() || nearbyStorageLocked || nearbySaveFailed || clockFault ||
      !buddy.alive || hourlyDozeWake) {
    stopNearbyRadio();
    return;
  }
  if (!nearbyRadioActive) {
    uint32_t interval = buddyNearbyBurstInterval(nearbyEncounter.phase != NEAR_IDLE,
                                                  nearbyVoiceSearchUntilMs != 0);
    if ((!lastNearbyBurstMs || millis() - lastNearbyBurstMs >= interval) &&
        (!lastNearbyStartAttemptMs || millis() - lastNearbyStartAttemptMs >= 30000UL))
      startNearbyRadio();
    return;
  }
  if (nearbyRadioError == 5 && millis() - lastNearbyPublishRetryMs >= 30000UL) {
    lastNearbyPublishRetryMs = millis();
    if (publishNearbyAdvertisement()) nearbyRadioError = 0;
  }
  NearbyObservation observation;
  while (nearbyQueue && xQueueReceive(nearbyQueue, &observation, 0) == pdTRUE) {
    nearbyPacketsReceived++;
    observeNearbyPacket(observation.packet, observation.rssi);
  }
  serviceNearbyEncounter();
  // ESP-SR and the BLE controller both need substantial internal RAM. They
  // alternate rather than coexist; discovery is a bounded awake-time burst.
  uint32_t burstLimit = buddyNearbyBurstLimit(nearbyEncounter.phase != NEAR_IDLE);
  if (millis() - nearbyBurstStartedMs >= burstLimit) {
    stopNearbyRadio();
    return;
  }
  uint32_t interval = nearbyEncounter.phase == NEAR_INVITING ||
    nearbyEncounter.phase == NEAR_ACCEPTING ? 30000UL : 5UL * 60UL * 1000UL;
  if (!NimBLEDevice::getScan()->isScanning() && buddyNearbyScanDue(millis(), lastNearbyScanMs, interval)) {
    lastNearbyScanMs = millis();
    if (!NimBLEDevice::getScan()->start(5000, false, true)) nearbyRadioError = 6;
    else {
      nearbyScanStarts++;
      Serial.printf("NEARBY scan started count=%lu\n", (unsigned long)nearbyScanStarts);
      if (nearbyRadioError == 6) nearbyRadioError = 0;
    }
  }
}
#else
// This V2 board is a USB-C sink-only design. Wireless discovery is retired;
// retain encounter data in NVS while a safe wired transport is decided.
static bool nearbyStackInitialized = false;
static uint32_t nearbyScanStarts = 0;
static uint32_t nearbyPacketsReceived = 0;
bool startNearbyRadio() { return false; }
bool startVoiceNearbySearch() { return false; }
void stopNearbyRadio() { nearbyRadioActive = false; }
void shutdownNearbyStackForSleep() {}
void refreshNearbyAdvertisement() { refreshCablePacket(); }
void serviceNearbyRadio() { serviceCableFriends(); }
#endif
