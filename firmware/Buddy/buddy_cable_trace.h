#pragma once
#include <stdint.h>

// RTC-only diagnostics; never part of Buddy's persisted life or wire protocol.
enum BuddyCableOperation : uint32_t {
  CABLE_OP_NONE, CABLE_OP_WAIT, CABLE_OP_GPIO_UNUSED, CABLE_OP_GPIO_DATA,
  CABLE_OP_WORKER_CREATE, CABLE_OP_WORKER_READY, CABLE_OP_ISR_INSTALL,
  CABLE_OP_ISR_ADD, CABLE_OP_ISR_TYPE, CABLE_OP_ISR_ENABLE,
  CABLE_OP_DISPLAY, CABLE_OP_ISR_DISABLE, CABLE_OP_ISR_REMOVE,
  CABLE_OP_WORKER_STOP, CABLE_OP_PADS_RELEASE, CABLE_OP_FAREWELL,
  CABLE_OP_UART_INSTALL, CABLE_OP_UART_PIN, CABLE_OP_UART_DELETE,
  CABLE_OP_UART_ECHO
};
inline const char *buddyCableOperationText(BuddyCableOperation op) {
  switch (op) {
    case CABLE_OP_WAIT: return "WAIT";
    case CABLE_OP_GPIO_UNUSED: return "GPIO_UNUSED";
    case CABLE_OP_GPIO_DATA: return "GPIO_DATA";
    case CABLE_OP_WORKER_CREATE: return "WORKER_CREATE";
    case CABLE_OP_WORKER_READY: return "WORKER_READY";
    case CABLE_OP_ISR_INSTALL: return "ISR_INSTALL";
    case CABLE_OP_ISR_ADD: return "ISR_ADD";
    case CABLE_OP_ISR_TYPE: return "ISR_TYPE";
    case CABLE_OP_ISR_ENABLE: return "ISR_ENABLE";
    case CABLE_OP_DISPLAY: return "DISPLAY";
    case CABLE_OP_ISR_DISABLE: return "ISR_DISABLE";
    case CABLE_OP_ISR_REMOVE: return "ISR_REMOVE";
    case CABLE_OP_WORKER_STOP: return "WORKER_STOP";
    case CABLE_OP_PADS_RELEASE: return "PADS_RELEASE";
    case CABLE_OP_FAREWELL: return "FAREWELL";
    case CABLE_OP_UART_INSTALL: return "UART_INSTALL";
    case CABLE_OP_UART_PIN: return "UART_PIN";
    case CABLE_OP_UART_DELETE: return "UART_DELETE";
    case CABLE_OP_UART_ECHO: return "UART_ECHO";
    default: return "NONE";
  }
}
struct BuddyCableTrace {
  uint32_t magic;
  uint32_t inverse;
  uint32_t operation;
  int32_t error;
  uint32_t resources;
  uint32_t phase;
  uint32_t rtcRecovery;
};
static constexpr uint32_t BUDDY_CABLE_TRACE_MAGIC = 0x43545232UL;
inline void buddyCableTraceSet(BuddyCableTrace &trace, BuddyCableOperation op, int32_t error = 0) {
  __atomic_store_n(&trace.operation, (uint32_t)op, __ATOMIC_RELEASE);
  __atomic_store_n(&trace.error, error, __ATOMIC_RELEASE);
  __atomic_store_n(&trace.inverse, ~BUDDY_CABLE_TRACE_MAGIC, __ATOMIC_RELAXED);
  __atomic_store_n(&trace.magic, BUDDY_CABLE_TRACE_MAGIC, __ATOMIC_RELEASE);
}
inline void buddyCableTraceStep(BuddyCableTrace &trace, BuddyCableOperation op) {
  // Once an API reports a failure, cleanup must not overwrite its cause.
  if (__atomic_load_n(&trace.error, __ATOMIC_ACQUIRE) == 0)
    buddyCableTraceSet(trace, op);
}
inline void buddyCableTraceFail(BuddyCableTrace &trace, BuddyCableOperation op, int32_t error) {
  if (__atomic_load_n(&trace.error, __ATOMIC_ACQUIRE) == 0)
    buddyCableTraceSet(trace, op, error);
}
inline void buddyCableTraceSnapshot(BuddyCableTrace &trace, uint32_t resources,
                                   uint32_t phase, bool rtcRecovery) {
  __atomic_store_n(&trace.resources, resources, __ATOMIC_RELEASE);
  __atomic_store_n(&trace.phase, phase, __ATOMIC_RELEASE);
  __atomic_store_n(&trace.rtcRecovery, rtcRecovery ? 1u : 0u, __ATOMIC_RELEASE);
}
inline BuddyCableTrace buddyCableTraceTake(BuddyCableTrace &trace) {
  BuddyCableTrace result = {};
  if (__atomic_load_n(&trace.magic, __ATOMIC_ACQUIRE) == BUDDY_CABLE_TRACE_MAGIC &&
      __atomic_load_n(&trace.inverse, __ATOMIC_RELAXED) == ~BUDDY_CABLE_TRACE_MAGIC &&
      __atomic_load_n(&trace.operation, __ATOMIC_ACQUIRE) <= CABLE_OP_UART_ECHO) {
    result = trace;
  }
  trace = {};
  return result;
}
