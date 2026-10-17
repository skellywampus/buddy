#pragma once
#include <stdint.h>

// This ordering is shared by the on-board teardown and the host's mocked
// hardware tests. In particular, no slow display work may precede QUIESCE.
enum BuddyCableShutdownStep {
  CABLE_SHUTDOWN_DISARM,
  CABLE_SHUTDOWN_STOP_WORKER,
  CABLE_SHUTDOWN_STOP_UART,
  CABLE_SHUTDOWN_RELEASE_PADS
};
template<typename Action>
inline void buddyCableShutdownSequence(bool uart, bool worker, bool pads, Action action) {
  action(CABLE_SHUTDOWN_DISARM);
  // The worker owns all UART reads/writes; join it before deleting the driver.
  if (worker) action(CABLE_SHUTDOWN_STOP_WORKER);
  if (uart) action(CABLE_SHUTDOWN_STOP_UART);
  if (pads) action(CABLE_SHUTDOWN_RELEASE_PADS);
}
