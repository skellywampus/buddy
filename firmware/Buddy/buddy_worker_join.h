#pragma once
// The owner holds the task handle throughout teardown. Workers acknowledge
// quiescence and park; they never clear or delete the owner's handle.
template<class Handle, class Ready, class Pause, class Delete>
bool buddyJoinOwnedWorker(Handle worker, Ready ready, Pause pause, Delete erase) {
  if (!worker) return false;
  for (unsigned i = 0; ready() && i < 60; ++i) pause();
  bool forced = ready();
  erase(worker);
  return forced;
}
