#pragma once
#include <stdint.h>

enum BuddyVoiceCommand : uint8_t {
  VOICE_NONE = 0,
  VOICE_SOUND_OFF = 1,
  VOICE_GO_TO_SLEEP = 3,
};

struct BuddyVoiceAction {
  bool changeSound;
  bool soundOn;
  bool manualSleep;
};

inline BuddyVoiceAction buddyVoiceAction(int commandId) {
  switch (commandId) {
    case VOICE_SOUND_OFF: return {true, false, false};
    case VOICE_GO_TO_SLEEP: return {false, false, true};
    default: return {false, false, false};
  }
}

inline const char *buddyVoiceCommandText(int commandId) {
  switch (commandId) {
    case VOICE_SOUND_OFF: return "SOUND OFF";
    case VOICE_GO_TO_SLEEP: return "GOODNIGHT";
    default: return "";
  }
}
