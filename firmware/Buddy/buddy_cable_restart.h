#pragma once
#include <stdint.h>

// RTC no-init memory survives ESP.restart(), unlike initialized RTC_DATA.
// A signature and its inverse reject uninitialized or stale power-on memory.
struct BuddyCableRestartNotice {
  uint32_t magic;
  uint32_t inverse;
  uint32_t reason;
};
static constexpr uint32_t BUDDY_CABLE_NOTICE_MAGIC = 0x43424e31UL;

inline void buddyCableNoticeSet(BuddyCableRestartNotice &notice, uint8_t reason) {
  notice.reason = reason;
  notice.inverse = ~BUDDY_CABLE_NOTICE_MAGIC;
  notice.magic = BUDDY_CABLE_NOTICE_MAGIC;
}

inline uint8_t buddyCableNoticeTake(BuddyCableRestartNotice &notice, bool softwareReset,
                                   uint8_t maximumReason) {
  uint8_t reason = softwareReset && notice.magic == BUDDY_CABLE_NOTICE_MAGIC &&
    notice.inverse == ~BUDDY_CABLE_NOTICE_MAGIC && notice.reason <= maximumReason ?
    (uint8_t)notice.reason : 0;
  notice = {};
  return reason;
}
