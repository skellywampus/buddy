// Manual, battery-to-battery link over an ordinary USB-C cable. The board is
// not a USB host: only D- is used, as a 500-bit/s open-drain shared line.
// Never enter this mode while attached to a charger or computer. No GPIO is
// ever driven high, and D+ is disabled. Normal USB resumes after a restart.
static constexpr gpio_num_t CABLE_DATA_PIN = GPIO_NUM_19;
static constexpr gpio_num_t CABLE_UNUSED_PIN = GPIO_NUM_20;
struct CableObservation { NearbyPacket packet; uint32_t receivedMs; };
static QueueHandle_t cableRxQueue = nullptr;
static TaskHandle_t cableTask = nullptr;
static inline TaskHandle_t cableTaskHandle() {
  return __atomic_load_n(&cableTask, __ATOMIC_ACQUIRE);
}
static inline void cableSetTaskHandle(TaskHandle_t task) {
  __atomic_store_n(&cableTask, task, __ATOMIC_RELEASE);
}
static portMUX_TYPE cableTxLock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t cableTxFrame[BUDDY_CABLE_FRAME_BYTES] = {};
static volatile bool cableTaskRunning = false;
static volatile bool cableWorkerReady = false;
static volatile bool cableLinkArmed = false;
static volatile bool cableStartupComplete = false;
static inline bool cableLoadFlag(const volatile bool *flag) {
  return __atomic_load_n(flag, __ATOMIC_ACQUIRE);
}
static inline void cableStoreFlag(volatile bool *flag, bool value) {
  __atomic_store_n(flag, value, __ATOMIC_RELEASE);
}
static bool cablePadsClaimed = false;
static bool cableUartReady = false;
static volatile bool cableUartFault = false;
static uint32_t cableFramesReceived = 0;
static uint32_t cableFramesSent = 0;
static uint32_t cableBadFrames = 0;
static uint32_t cableQueueOverflows = 0;
static uint32_t cableGreetingUntilMs = 0;
static bool cableGreetingKnown = false;
static bool cableGreetingAwaitingPaint = false;
static void cableTraceCheckpoint(BuddyCableOperation operation) {
  buddyCableTraceStep(cableTrace, operation);
  uint32_t resources = (cableRxQueue ? 1u : 0u) |
    (cablePadsClaimed ? 2u : 0u) | (cableUartReady ? 4u : 0u) |
    (cableTaskHandle() ? 8u : 0u) | (cableLoadFlag(&cableLinkArmed) ? 16u : 0u);
  buddyCableTraceSnapshot(cableTrace, resources, cableSession.phase,
                          cableClockRecoveryPending);
}

static inline bool cableLineHigh() { return gpio_get_level(CABLE_DATA_PIN) != 0; }
static void cableQueuePacket(const NearbyPacket &packet) {
  CableObservation observation = {packet, millis()};
  if (cableRxQueue && xQueueSend(cableRxQueue, &observation, 0) != pdTRUE) {
    // A slow display or five-clip tune can delay the main loop. Retain the
    // newest validated heartbeat instead of timing out on a stale queue.
    CableObservation oldest;
    xQueueReceive(cableRxQueue, &oldest, 0);
    xQueueSend(cableRxQueue, &observation, 0);
    cableQueueOverflows++;
  }
}

static void cableWorker(void *) {
  cableStoreFlag(&cableWorkerReady, true);
  while (cableLoadFlag(&cableTaskRunning) && !cableLoadFlag(&cableLinkArmed))
    vTaskDelay(1);
  BuddyCableStream stream;
  uint32_t nextSend = millis() + 300 + esp_random() % 900;
  while (cableLoadFlag(&cableTaskRunning)) {
    if (cableLoadFlag(&cableStartupComplete))
      buddyCableMark(cableWatchdogMark, CABLE_STAGE_WAITING);
    uint8_t byte = 0;
    if (uart_read_bytes(UART_NUM_1, &byte, 1, pdMS_TO_TICKS(20)) == 1) {
      buddyCableMark(cableWatchdogMark, CABLE_STAGE_RECEIVING);
      NearbyPacket packet = {};
      bool validFrame = buddyCableStreamPush(stream, byte, millis(), packet);
      if (cableLoadFlag(&cableStartupComplete))
        buddyCableMark(cableWatchdogMark, CABLE_STAGE_WAITING);
      if (validFrame) {
        cableQueuePacket(packet);
        // Reply after the peer's full frame, before our independent heartbeat.
        nextSend = millis() + 425 + esp_random() % 125;
      }
    }
    if (!cableLoadFlag(&cableTaskRunning)) break;
    if (stream.count && (uint32_t)(millis() - stream.lastByteMs) <= BUDDY_CABLE_FRAME_GAP_MS)
      continue;
    if (stream.count) { buddyCableStreamReset(stream); cableBadFrames++; }
    if ((int32_t)(millis() - nextSend) < 0) continue;
    // Listen-before-talk plus unequal random retry periods makes simultaneous
    // starts transient. A collision cannot short two push-pull outputs.
    if (!cableLineHigh()) { nextSend = millis() + 150 + esp_random() % 450; continue; }
    uint8_t frame[BUDDY_CABLE_FRAME_BYTES];
    portENTER_CRITICAL(&cableTxLock);
    memcpy(frame, cableTxFrame, sizeof(frame));
    portEXIT_CRITICAL(&cableTxLock);
    buddyCableMark(cableWatchdogMark, CABLE_STAGE_SENDING);
    // Preserve the old two-byte bursts so a pre-update Buddy can receive us.
    // Hardware UART receives the local echo, which is discarded after TX.
    bool sent = true;
    for (size_t i = 0; i < sizeof(frame) && cableLoadFlag(&cableTaskRunning); i += 2) {
      size_t chunk = sizeof(frame) - i < 2 ? sizeof(frame) - i : 2;
      if (uart_write_bytes(UART_NUM_1, frame + i, chunk) != (int)chunk ||
          uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(120)) != ESP_OK) {
        sent = false; break;
      }
      if (i + chunk < sizeof(frame)) vTaskDelay(1);
    }
    uart_flush_input(UART_NUM_1);
    buddyCableStreamReset(stream);
    if (!sent && cableLoadFlag(&cableTaskRunning)) {
      cableStoreFlag(&cableUartFault, true);
      break;
    }
    if (cableLoadFlag(&cableStartupComplete))
      buddyCableMark(cableWatchdogMark, CABLE_STAGE_WAITING);
    if (sent) cableFramesSent++;
    nextSend = millis() + 1200 + esp_random() % 700;
  }
  cableStoreFlag(&cableWorkerReady, false);
  // Only the main-loop owner deletes/clears this handle. Park after releasing
  // UART access: check/reload deletion of a self-cleared handle could delete
  // the main task instead. No further peripheral access occurs while parked.
  vTaskSuspend(nullptr);
}

void refreshCablePacket() {
  if (!cableFriendsActive() || nearbyState.magic != NEAR_MAGIC) return;
  NearbyPacket packet = currentNearbyPacket();
  packet.activity = cableActivityForMoment((BuddyMoment)cableSceneMoment);
  packet.moment = cableSceneMoment;
  packet.expression = buddyCableExpression(buddy.mood, buddy.comfort,
                                            buddy.traits.curiosity);
  packet.girl = buddySexFromSeed(buddy.seed) == BUDDY_GIRL;
  uint8_t frame[BUDDY_CABLE_FRAME_BYTES];
  encodeBuddyCableFrame(packet, frame);
  portENTER_CRITICAL(&cableTxLock);
  memcpy(cableTxFrame, frame, sizeof(frame));
  portEXIT_CRITICAL(&cableTxLock);
}

bool startCableFriends() {
  if (cableFriendsActive()) return true;
  cableModeError = CABLE_ERROR_NONE;
  cableBootFailure = CABLE_ERROR_NONE;
  if (usb_serial_jtag_is_connected()) { cableModeError = CABLE_ERROR_USB_HOST; return false; }
  if (storageLocked) { cableModeError = CABLE_ERROR_SAVE_LOCK; return false; }
  if (introRequired || !buddy.alive || hourlyDozeWake) {
    cableModeError = CABLE_ERROR_LIFE; return false;
  }
  buddyNow(); // one preflight RTC read; no RTC transaction after cable startup begins
  if (!recoverBuddyClockForCable()) { cableModeError = CABLE_ERROR_CLOCK; return false; }
  if (nearbyStorageLocked) { cableModeError = CABLE_ERROR_CONTACT_LOCK; return false; }
  if (nearbyState.magic != NEAR_MAGIC || nearbyState.lifeSeed != buddy.seed) newNearbyLife();
  // A social-history write failure must not strand Cable Friends OFF or abort
  // a live connection. The valid in-memory identity can still meet peers;
  // history writes retry independently when NVS becomes available.
  if (nearbyStorageLocked || nearbyState.magic != NEAR_MAGIC) {
    cableModeError = CABLE_ERROR_CONTACT_LOCK; return false;
  }
  if (!cableRxQueue) cableRxQueue = xQueueCreate(32, sizeof(CableObservation));
  if (!cableRxQueue) { cableModeError = CABLE_ERROR_QUEUE; return false; }
  xQueueReset(cableRxQueue);
  buddyCableStart(cableSession, millis());
  cableTrace = {};
  cableTraceCheckpoint(CABLE_OP_WAIT);
  buddyCableMark(cableWatchdogMark, CABLE_STAGE_STARTING);
  cableStoreFlag(&cableWorkerReady, false);
  cableStoreFlag(&cableLinkArmed, false);
  cableStoreFlag(&cableStartupComplete, false);
  cableStoreFlag(&cableUartFault, false);
  cableFramesSent = cableFramesReceived = cableBadFrames = cableQueueOverflows = 0;
  cableGreetingUntilMs = 0;
  cableGreetingKnown = false;
  cableGreetingAwaitingPaint = false;
  nearbyEncounter = {};
  cableScenePeer = {};
  buddyCableMark(cableWatchdogMark, CABLE_STAGE_SCENE);
  cableSceneMoment = chooseCableMoment(clockLastEpoch, esp_random(), MOMENT_COUNT);
  cableSceneActivity = cableActivityForMoment((BuddyMoment)cableSceneMoment);
  cableNextMomentMs = millis() + buddyMomentDurationMs((BuddyMoment)cableSceneMoment,
    (BuddyStage)buddy.currentStage, buddy.energy, buddy.comfort, buddy.traits,
    0, 0, esp_random());
  memset(nearbySeen, 0, sizeof(nearbySeen));
  buddyCableMark(cableWatchdogMark, CABLE_STAGE_AUDIO);
  buddyListeningClose(buddyListeningWindow);
  stopBuddyMic(); // release I2S/audio before claiming USB data pads
  buddyCableMark(cableWatchdogMark, CABLE_STAGE_GPIO);
  gpio_config_t unused = {};
  unused.pin_bit_mask = 1ULL << CABLE_UNUSED_PIN;
  unused.mode = GPIO_MODE_INPUT;
  unused.pull_up_en = GPIO_PULLUP_DISABLE;
  unused.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cableTraceCheckpoint(CABLE_OP_GPIO_UNUSED);
  esp_err_t result = gpio_config(&unused);
  if (result != ESP_OK) {
    buddyCableTraceFail(cableTrace, CABLE_OP_GPIO_UNUSED, result);
    cableModeError = CABLE_ERROR_GPIO; cableBootFailure = cableModeError;
    exitCableFriends(); return false;
  }
  cablePadsClaimed = true;
  // Selects GPIO mux and removes the USB D+ pull-up. A failed setup after
  // this point restarts so normal USB is recovered without a cable session.
  gpio_set_level(CABLE_DATA_PIN, 1);
  gpio_config_t data = {};
  data.pin_bit_mask = 1ULL << CABLE_DATA_PIN;
  data.mode = GPIO_MODE_INPUT_OUTPUT_OD;
  data.pull_up_en = GPIO_PULLUP_ENABLE;
  data.pull_down_en = GPIO_PULLDOWN_DISABLE;
  data.intr_type = GPIO_INTR_DISABLE;
  cableTraceCheckpoint(CABLE_OP_GPIO_DATA);
  result = gpio_config(&data);
  if (result != ESP_OK) {
    buddyCableTraceFail(cableTrace, CABLE_OP_GPIO_DATA, result);
    cableModeError = CABLE_ERROR_GPIO; cableBootFailure = cableModeError;
    exitCableFriends(); return false;
  }
  // A one-wire UART matches the existing 500-baud, 8N1 open-drain protocol.
  // Keep TX/RX on D- only; never drive high or use D+ for data.
  buddyCableMark(cableWatchdogMark, CABLE_STAGE_UART);
  cableTraceCheckpoint(CABLE_OP_UART_INSTALL);
  uart_config_t config = {};
  config.baud_rate = 500;
  config.data_bits = UART_DATA_8_BITS;
  config.parity = UART_PARITY_DISABLE;
  config.stop_bits = UART_STOP_BITS_1;
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  result = uart_param_config(UART_NUM_1, &config);
  if (result == ESP_OK) result = uart_driver_install(UART_NUM_1, 256, 0, 0, nullptr, 0);
  if (result != ESP_OK) {
    buddyCableTraceFail(cableTrace, CABLE_OP_UART_INSTALL, result);
    cableModeError = CABLE_ERROR_UART; cableBootFailure = cableModeError;
    exitCableFriends(); return false;
  }
  cableUartReady = true;
  cableTraceCheckpoint(CABLE_OP_UART_PIN);
  result = uart_set_pin(UART_NUM_1, CABLE_DATA_PIN, CABLE_DATA_PIN,
                        UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (result == ESP_OK) result = gpio_set_level(CABLE_DATA_PIN, 1);
  if (result == ESP_OK) result = gpio_set_direction(CABLE_DATA_PIN, GPIO_MODE_INPUT_OUTPUT_OD);
  if (result == ESP_OK) result = gpio_pullup_en(CABLE_DATA_PIN);
  if (result == ESP_OK) result = uart_set_rx_timeout(UART_NUM_1, 2);
  if (result != ESP_OK) {
    buddyCableTraceFail(cableTrace, CABLE_OP_UART_PIN, result);
    cableModeError = CABLE_ERROR_UART; cableBootFailure = cableModeError;
    exitCableFriends(); return false;
  }
  // Same-pin uart_set_pin() has shipped with a TX routing regression on some
  // ESP-IDF builds. Route UART1 TX back to the pad after configuring both
  // signals; the pad remains open-drain, so no peer can be driven high.
  esp_rom_gpio_connect_out_signal(CABLE_DATA_PIN,
    UART_PERIPH_SIGNAL(UART_NUM_1, SOC_UART_TX_PIN_IDX), false, false);
  // Before showing WAITING, verify the physical pad can send and receive one
  // byte through its own RX path. A silent TX must fail closed, not count
  // successful heartbeat writes while the other Buddy hears nothing.
  cableTraceCheckpoint(CABLE_OP_UART_ECHO);
  bool echoReady = false;
  for (uint8_t attempt = 0; attempt < 3 && !echoReady; ++attempt) {
    uart_flush_input(UART_NUM_1);
    const uint8_t probe = 0x55; // not a frame sync; harmless to an older peer
    uint8_t echo = 0;
    if (cableLineHigh() && uart_write_bytes(UART_NUM_1, &probe, 1) == 1 &&
        uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(120)) == ESP_OK &&
        uart_read_bytes(UART_NUM_1, &echo, 1, pdMS_TO_TICKS(180)) == 1 &&
        echo == probe) echoReady = true;
    else delay(10);
  }
  uart_flush_input(UART_NUM_1);
  if (!echoReady) {
    buddyCableTraceFail(cableTrace, CABLE_OP_UART_ECHO, -1);
    cableModeError = CABLE_ERROR_UART; cableBootFailure = cableModeError;
    exitCableFriends(); return false;
  }
  cableStoreFlag(&cableTaskRunning, true);
  refreshCablePacket();
  buddyCableMark(cableWatchdogMark, CABLE_STAGE_WORKER);
  cableTraceCheckpoint(CABLE_OP_WORKER_CREATE);
  TaskHandle_t createdTask = nullptr;
  if (xTaskCreatePinnedToCore(cableWorker, "buddy-cable", 4096, nullptr, 8,
                              &createdTask, 0) != pdPASS) {
    cableModeError = CABLE_ERROR_WORKER; cableBootFailure = cableModeError;
    exitCableFriends(); return false;
  }
  cableSetTaskHandle(createdTask);
  cableTraceCheckpoint(CABLE_OP_WORKER_READY);
  uint32_t workerStartMs = millis();
  while (!cableLoadFlag(&cableWorkerReady) &&
         !buddyCableWorkerReadyExpired(cableLoadFlag(&cableWorkerReady),
                                       workerStartMs, millis()))
    delay(1);
  if (!cableLoadFlag(&cableWorkerReady)) {
    buddyCableTraceFail(cableTrace, CABLE_OP_WORKER_READY, -1);
    cableModeError = CABLE_ERROR_WORKER; cableBootFailure = cableModeError;
    exitCableFriends(); return false;
  }
  cableStoreFlag(&cableLinkArmed, true);
  // A prior meeting's delayed farewell repaint must never mask a new wait.
  cableReturnRepaintPending = false;
  cableReturnActivityOverride = false;
  cableReturnFaultShown = false;
  lastUserMs = millis();
  currentView = VIEW_BUDDY;
  updateDisplayOrientation();
  cleanPartialRequested = true;
  needsRender = true;
  buddyCableMark(cableWatchdogMark, CABLE_STAGE_DISPLAY);
  cableTraceCheckpoint(CABLE_OP_DISPLAY);
  renderBuddy(true);
  cableTraceCheckpoint(CABLE_OP_WAIT);
  cableStoreFlag(&cableStartupComplete, true);
  return true;
}

void quiesceCableLink() {
  if (!cablePadsClaimed && !cableUartReady && !cableTaskHandle()) return;
  buddyCableMark(cableWatchdogMark, CABLE_STAGE_STOPPING);
  buddyCableShutdownSequence(cableUartReady, cableTaskHandle() != nullptr,
                              cablePadsClaimed, [&](BuddyCableShutdownStep step) {
    switch (step) {
      case CABLE_SHUTDOWN_DISARM:
        cableStoreFlag(&cableLinkArmed, false);
        cableStoreFlag(&cableStartupComplete, false);
        cableStoreFlag(&cableTaskRunning, false);
        break;
      case CABLE_SHUTDOWN_STOP_WORKER: {
        buddyCableTraceStep(cableTrace, CABLE_OP_WORKER_STOP);
        TaskHandle_t worker = cableTaskHandle();
        if (worker) {
          bool forced = buddyJoinOwnedWorker(worker,
            [] { return cableLoadFlag(&cableWorkerReady); },
            [] { delay(10); }, [](TaskHandle_t owned) { vTaskDelete(owned); });
          cableSetTaskHandle(nullptr);
          if (forced) {
            buddyCableTraceFail(cableTrace, CABLE_OP_WORKER_STOP, -1);
            cableBootFailure = CABLE_ERROR_WORKER;
          }
        }
        break;
      }
      case CABLE_SHUTDOWN_STOP_UART: {
        buddyCableTraceStep(cableTrace, CABLE_OP_UART_DELETE);
        esp_err_t result = uart_driver_delete(UART_NUM_1);
        if (result != ESP_OK) {
          buddyCableTraceFail(cableTrace, CABLE_OP_UART_DELETE, result);
          cableBootFailure = CABLE_ERROR_UART;
        }
        cableUartReady = false;
        break;
      }
      case CABLE_SHUTDOWN_RELEASE_PADS:
        buddyCableTraceStep(cableTrace, CABLE_OP_PADS_RELEASE);
        {
          esp_err_t result = gpio_set_level(CABLE_DATA_PIN, 1);
          if (result == ESP_OK) result = gpio_set_direction(CABLE_DATA_PIN, GPIO_MODE_INPUT);
          if (result == ESP_OK) result = gpio_pullup_dis(CABLE_DATA_PIN);
          if (result == ESP_OK) result = gpio_set_direction(CABLE_UNUSED_PIN, GPIO_MODE_INPUT);
          if (result != ESP_OK) {
            buddyCableTraceFail(cableTrace, CABLE_OP_PADS_RELEASE, result);
            cableBootFailure = CABLE_ERROR_GPIO;
          }
        }
        cablePadsClaimed = false;
        break;
    }
  });
}

void stopCableFriends() {
  if (!cableFriendsActive() && !cablePadsClaimed && !cableUartReady &&
      !cableRxQueue && !cableTaskHandle()) return;
  quiesceCableLink();
  buddyCableStop(cableSession);
  if (cableRxQueue) { vQueueDelete(cableRxQueue); cableRxQueue = nullptr; }
  cableStoreFlag(&cableWorkerReady, false);
  cableStoreFlag(&cableUartFault, false);
  portENTER_CRITICAL(&cableTxLock);
  memset(cableTxFrame, 0, sizeof(cableTxFrame));
  portEXIT_CRITICAL(&cableTxLock);
  nearbyEncounter = {};
  cableScenePeer = {};
  cableSceneActivity = NEAR_WATCH;
  cableSceneMoment = MOMENT_WATCH;
  cableNextMomentMs = 0;
  memset(nearbySeen, 0, sizeof(nearbySeen));
  cableGreetingUntilMs = 0;
  cableGreetingKnown = false;
  cableGreetingAwaitingPaint = false;
  stopBuddyMic();
  updateDisplayOrientation(); // leave the cable-only 180-degree orientation
  // GPIO19/20 are USB pins, not the GPIO47/48 I2C bus. Leave a healthy I2C
  // controller alone; failed RTC reads invoke bounded bus recovery.
  cleanPartialRequested = true;
  needsRender = true;
  buddyCableMark(cableWatchdogMark, CABLE_STAGE_OFF);
}

void serviceCableFriends() {
  if (!cableFriendsActive()) return;
  if (cableLoadFlag(&cableUartFault)) {
    cableBootFailure = CABLE_ERROR_UART;
    exitCableFriends();
    return;
  }
  if (!cableRxQueue || !cableLoadFlag(&cableTaskRunning) || !cableTaskHandle() ||
      !cablePadsClaimed ||
      storageLocked || nearbyStorageLocked || !buddy.alive) {
    cableBootFailure = CABLE_ERROR_RUNTIME;
    exitCableFriends();
    return;
  }
  CableObservation observation;
  while (xQueueReceive(cableRxQueue, &observation, 0) == pdTRUE) {
    if (buddyCableEndDue(cableSession, observation.receivedMs) != CABLE_CONTINUE) {
      exitCableFriends();
      return;
    }
    bool firstPeer = cableSession.phase == CABLE_WAITING;
    if (!buddyCableObservePeer(cableSession, observation.packet.sender,
                               nearbyState.selfId, observation.receivedMs)) continue;
    cableFramesReceived++;
    // The lower public ID leads the joint activity. Only the leader chooses;
    // the other Buddy mirrors that moment while preserving his own expression.
    if (observation.packet.sender < nearbyState.selfId &&
        cableSceneMoment != observation.packet.moment) {
      cableSceneMoment = observation.packet.moment;
      cableSceneActivity = cableActivityForMoment((BuddyMoment)cableSceneMoment);
      needsRender = true;
    }
    if (firstPeer) {
      const NearbyContact *contact = nearbyContact(observation.packet.sender);
      cableGreetingKnown = contact && contact->meetings;
      cableGreetingUntilMs = 0;
      cableGreetingAwaitingPaint = true;
      if (nearbyState.selfId < observation.packet.sender)
        cableNextMomentMs = millis() + buddyMomentDurationMs(
          (BuddyMoment)cableSceneMoment, (BuddyStage)buddy.currentStage,
          buddy.energy, buddy.comfort, buddy.traits, 0, 0, esp_random());
      // A valid physical link confirms this meeting directly. The old radio
      // invitation handshake is not needed and could re-open its scene after
      // every timeout while the cable remained connected.
      uint32_t now = buddyNow();
      if (!clockFault && now && readyForNearbyMeeting(observation.packet.sender, now))
        rememberNearbyMeeting(observation.packet.sender,
          cableActivityForMoment((BuddyMoment)cableSceneMoment), now);
      rememberNearbySeen(observation.packet, -40);
      needsRender = true;
    }
    if (buddyCablePortraitChanged(cableScenePeer, observation.packet)) {
      cableScenePeer = observation.packet;
      needsRender = true;
    }
    refreshCablePacket();
  }
  if (cableSession.phase == CABLE_CONNECTED &&
      nearbyState.selfId < cableSession.peerId && cableNextMomentMs &&
      (int32_t)(millis() - cableNextMomentMs) >= 0) {
    BuddyMoment previous = (BuddyMoment)cableSceneMoment;
    BuddyMoment chosen = chooseCableMoment(buddyNow(), esp_random(), previous);
    cableSceneMoment = chosen;
    cableSceneActivity = cableActivityForMoment(chosen);
    cableNextMomentMs = millis() + buddyMomentDurationMs(chosen,
      (BuddyStage)buddy.currentStage, buddy.energy, buddy.comfort, buddy.traits,
      0, 0, esp_random());
    refreshCablePacket();
    if (chosen != previous) needsRender = true;
  }
  BuddyCableEnd end = buddyCableEndDue(cableSession, millis());
  if (end != CABLE_CONTINUE) {
    exitCableFriends(); // never return to waiting after losing the first peer
    return;
  }
  if (cableGreetingUntilMs && (int32_t)(millis() - cableGreetingUntilMs) >= 0) {
    cableGreetingUntilMs = 0;
    cleanPartialRequested = true;
    needsRender = true;
  }
}
