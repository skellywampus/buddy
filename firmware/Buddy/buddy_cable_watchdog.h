#pragma once
#include <stdint.h>
#include <stddef.h>

// Kept outside the life/save ABI. RTC no-init memory survives watchdog resets.
enum BuddyCableStage : uint32_t {
  CABLE_STAGE_OFF, CABLE_STAGE_STARTING, CABLE_STAGE_WAITING,
  CABLE_STAGE_RECEIVING, CABLE_STAGE_SENDING, CABLE_STAGE_STOPPING,
  CABLE_STAGE_SCENE, CABLE_STAGE_AUDIO, CABLE_STAGE_GPIO,
  CABLE_STAGE_WORKER, CABLE_STAGE_ISR, CABLE_STAGE_DISPLAY, CABLE_STAGE_UART
};
inline const char *buddyCableStageText(BuddyCableStage stage) {
  switch (stage) {
    case CABLE_STAGE_STARTING: return "START";
    case CABLE_STAGE_WAITING: return "WAIT";
    case CABLE_STAGE_RECEIVING: return "RX";
    case CABLE_STAGE_SENDING: return "TX";
    case CABLE_STAGE_STOPPING: return "EXIT";
    case CABLE_STAGE_SCENE: return "SCENE";
    case CABLE_STAGE_AUDIO: return "AUDIO";
    case CABLE_STAGE_GPIO: return "GPIO";
    case CABLE_STAGE_WORKER: return "WORKER";
    case CABLE_STAGE_ISR: return "IRQ";
    case CABLE_STAGE_DISPLAY: return "DISPLAY";
    case CABLE_STAGE_UART: return "UART";
    default: return "UNKNOWN";
  }
}
struct BuddyCableWatchdogMark {
  uint32_t magic;
  uint32_t inverse;
  uint32_t stage;
};
static constexpr uint32_t BUDDY_CABLE_WATCHDOG_MAGIC = 0x43574431UL;
static constexpr uint32_t BUDDY_CABLE_WORKER_READY_MS = 150UL;

inline bool buddyCableWorkerReadyExpired(bool ready, uint32_t startedMs,
                                        uint32_t nowMs) {
  return !ready && (uint32_t)(nowMs - startedMs) >= BUDDY_CABLE_WORKER_READY_MS;
}

inline void buddyCableMark(BuddyCableWatchdogMark &mark, BuddyCableStage stage) {
  // The loop task and cable worker run on different cores. Keep every
  // breadcrumb write atomic; ordinary concurrent struct assignments are a
  // C++ data race and can leave a misleading stale START mark after a crash.
  __atomic_store_n(&mark.stage, (uint32_t)stage, __ATOMIC_RELEASE);
  __atomic_store_n(&mark.inverse, ~BUDDY_CABLE_WATCHDOG_MAGIC, __ATOMIC_RELAXED);
  __atomic_store_n(&mark.magic, BUDDY_CABLE_WATCHDOG_MAGIC, __ATOMIC_RELEASE);
}

inline BuddyCableStage buddyCableTakeWatchdogMark(BuddyCableWatchdogMark &mark,
                                                   bool watchdogReset) {
  BuddyCableStage stage = CABLE_STAGE_OFF;
  uint32_t magic = __atomic_load_n(&mark.magic, __ATOMIC_ACQUIRE);
  uint32_t inverse = __atomic_load_n(&mark.inverse, __ATOMIC_RELAXED);
  uint32_t recorded = __atomic_load_n(&mark.stage, __ATOMIC_ACQUIRE);
  if (watchdogReset && magic == BUDDY_CABLE_WATCHDOG_MAGIC &&
      inverse == ~BUDDY_CABLE_WATCHDOG_MAGIC && recorded <= CABLE_STAGE_UART)
    stage = (BuddyCableStage)recorded;
  mark = {};
  return stage;
}

template<typename Store> inline BuddyCableStage buddyCableLoadWatchdogStage(Store &store) {
  uint8_t value = store.getUChar("last-wdt", CABLE_STAGE_OFF);
  return value <= CABLE_STAGE_UART ? (BuddyCableStage)value : CABLE_STAGE_OFF;
}

template<typename Store> inline bool buddyCablePersistWatchdogStage(Store &store,
                                                                    BuddyCableStage stage) {
  return stage > CABLE_STAGE_OFF && stage <= CABLE_STAGE_UART &&
    store.putUChar("last-wdt", stage) == 1 &&
    buddyCableLoadWatchdogStage(store) == stage;
}

// No uninterrupted send/receive span exceeds two 20-ms bytes. Leave the
// final byte untouched so the frame's trailing idle period stays standard.
inline bool buddyCableYieldAfterByte(size_t index, size_t total) {
  return index + 1 < total && ((index + 1) % 2 == 0);
}
