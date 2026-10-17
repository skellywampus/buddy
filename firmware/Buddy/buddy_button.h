#pragma once
#include <stdint.h>

static constexpr uint32_t BUDDY_MENU_IDLE_MS = 45000;
static inline bool buddyMenuReturnDue(uint32_t now, uint32_t lastActionMs,
                                      bool buttonHeld) {
  return !buttonHeld && (uint32_t)(now - lastActionMs) >= BUDDY_MENU_IDLE_MS;
}

enum BuddyClickAction : uint8_t {
  BUDDY_CLICK_NONE, BUDDY_CLICK_INTERACT, BUDDY_CLICK_OPEN_MENU,
  BUDDY_CLICK_ADVANCE_MENU, BUDDY_CLICK_RETURN_HOME
};

static inline BuddyClickAction buddyClickAction(BuddyView view, ButtonEvent event) {
  if (event == BUTTON_SHORT)
    return view == VIEW_BUDDY ? BUDDY_CLICK_INTERACT : BUDDY_CLICK_ADVANCE_MENU;
  if (event == BUTTON_DOUBLE)
    return view == VIEW_BUDDY ? BUDDY_CLICK_OPEN_MENU : BUDDY_CLICK_RETURN_HOME;
  return BUDDY_CLICK_NONE;
}

// Defer ordinary click actions until the sequence is complete. In particular,
// five clicks must not perform a single-click or interaction on the way there.
struct BuddyButtonRecognizer {
  static constexpr uint32_t CLICK_GAP_MS = 250;
  static constexpr uint32_t HOLD_MS = 1000;
  bool stableHigh = true;
  bool candidateHigh = true;
  bool longSent = false;
  uint8_t clicks = 0;
  uint32_t candidateSince = 0;
  uint32_t pressedSince = 0;
  uint32_t releasedSince = 0;

  ButtonEvent poll(bool high, uint32_t now) {
    if (high != candidateHigh) {
      candidateHigh = high;
      candidateSince = now;
    }
    if (candidateHigh != stableHigh && (uint32_t)(now - candidateSince) >= 35) {
      stableHigh = candidateHigh;
      if (!stableHigh) {
        pressedSince = now;
        longSent = false;
      } else if (!longSent) {
        releasedSince = now;
        if (++clicks >= 5) { clicks = 0; return BUTTON_RECOVER; }
      }
    }
    if (!stableHigh && !longSent && (uint32_t)(now - pressedSince) >= HOLD_MS) {
      longSent = true;
      clicks = 0;
      return BUTTON_LONG;
    }
    if (stableHigh && clicks && (uint32_t)(now - releasedSince) > CLICK_GAP_MS) {
      uint8_t completed = clicks;
      clicks = 0;
      return completed == 1 ? BUTTON_SHORT : completed == 2 ? BUTTON_DOUBLE : BUTTON_NONE;
    }
    return BUTTON_NONE;
  }
};
