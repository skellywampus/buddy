#pragma once
#include <stdint.h>

// One activation can meet at most one peer. All times are unsigned monotonic
// millis() values; subtraction remains valid across a 32-bit wrap.
static constexpr uint32_t BUDDY_CABLE_WAIT_MS = 2UL * 60UL * 1000UL;
static constexpr uint32_t BUDDY_CABLE_SILENCE_MS = 5UL * 1000UL;

enum BuddyCablePhase : uint8_t {
  CABLE_OFF, CABLE_WAITING, CABLE_CONNECTED
};
enum BuddyCableEnd : uint8_t {
  CABLE_CONTINUE, CABLE_WAIT_EXPIRED, CABLE_PEER_LOST
};

struct BuddyCableSession {
  BuddyCablePhase phase = CABLE_OFF;
  uint32_t startedMs = 0;
  uint32_t lastPeerMs = 0;
  uint32_t peerId = 0;
};

inline bool buddyCableActive(const BuddyCableSession &session) {
  return session.phase != CABLE_OFF;
}

inline void buddyCableStart(BuddyCableSession &session, uint32_t nowMs) {
  session = {CABLE_WAITING, nowMs, 0, 0};
}

inline BuddyCableEnd buddyCableEndDue(const BuddyCableSession &session, uint32_t nowMs) {
  if (session.phase == CABLE_WAITING &&
      (uint32_t)(nowMs - session.startedMs) >= BUDDY_CABLE_WAIT_MS) return CABLE_WAIT_EXPIRED;
  if (session.phase == CABLE_CONNECTED &&
      (uint32_t)(nowMs - session.lastPeerMs) >= BUDDY_CABLE_SILENCE_MS) return CABLE_PEER_LOST;
  return CABLE_CONTINUE;
}

// Called only after the complete wire frame and Nearby packet validate. A
// second sender cannot extend this activation or replace its original peer.
// Late packets cannot revive an expired wait or disconnected session.
inline bool buddyCableObservePeer(BuddyCableSession &session, uint32_t sender,
                                  uint32_t selfId, uint32_t nowMs) {
  if (!sender || sender == selfId || session.phase == CABLE_OFF ||
      buddyCableEndDue(session, nowMs) != CABLE_CONTINUE ||
      (session.phase == CABLE_CONNECTED && sender != session.peerId)) return false;
  if (session.phase == CABLE_WAITING) {
    session.phase = CABLE_CONNECTED;
    session.peerId = sender;
  }
  session.lastPeerMs = nowMs;
  return true;
}

inline void buddyCableStop(BuddyCableSession &session) { session = {}; }

// The greeting's six seconds begin after the e-paper waveform finishes, not
// when the first packet arrives. Pending means it has not yet been shown.
inline bool buddyCableGreetingVisible(bool pending, uint32_t untilMs,
                                      uint32_t nowMs) {
  return pending || (untilMs && (int32_t)(nowMs - untilMs) < 0);
}

inline const char *buddyCableGreetingText(bool known) {
  return known ? "I KNOW YOU!" : "A STRANGER!";
}
