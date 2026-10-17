// The home belongs to the world across lives. It has its own alternating NVS
// records so a bad house write cannot corrupt a living Buddy's saved identity.
static uint32_t houseSequence = 0;
static int8_t activeHouseSlot = -1;

static uint32_t houseHash(const BuddyHouseState &state) {
  BuddyHouseState copy = state;
  copy.checksum = 0;
  uint32_t hash = 2166136261UL;
  const uint8_t *bytes = (const uint8_t *)&copy;
  for (size_t i = 0; i < sizeof(copy); i++) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}

static bool validHouse(const BuddyHouseState &state) {
  if (state.magic != BUDDY_HOUSE_MAGIC || state.version != BUDDY_HOUSE_VERSION ||
      !state.siteSeed || state.phase > HOUSE_RUIN || state.style >= 4 ||
      state.lastDay > UINT32_MAX / 86400UL || state.work > BUDDY_HOUSE_WORK_TARGET ||
      state.condition > 1000 || state.checksum != houseHash(state)) return false;
  if (state.phase == HOUSE_BUILDING && state.work == BUDDY_HOUSE_WORK_TARGET) return false;
  if (state.phase == HOUSE_OCCUPIED && (state.work != BUDDY_HOUSE_WORK_TARGET ||
      !state.builtEpoch || !state.builderGeneration || !state.condition)) return false;
  return true;
}

static bool readHouse(const char *key, BuddyHouseState &state) {
  return preferences.getBytesLength(key) == sizeof(state) &&
    preferences.getBytes(key, &state, sizeof(state)) == sizeof(state) && validHouse(state);
}

void saveBuddyHouse() {
  if (buddyCompletionPreparing) return;
  if (storageLocked || houseLocked || buddyHouse.magic != BUDDY_HOUSE_MAGIC) return;
  BuddyHouseState next = buddyHouse, check = {};
  next.sequence = houseSequence + 1;
  next.checksum = houseHash(next);
  int8_t slot = activeHouseSlot == 0 ? 1 : 0;
  const char *key = slot == 0 ? "home-a" : "home-b";
  houseSaveFailed = preferences.putBytes(key, &next, sizeof(next)) != sizeof(next) ||
    !readHouse(key, check) || check.sequence != next.sequence;
  if (!houseSaveFailed) {
    buddyHouse = next;
    houseSequence = next.sequence;
    activeHouseSlot = slot;
  }
}

void setupBuddyHouse(uint32_t now) {
  if (storageLocked || buddy.magic != BUDDY_MAGIC || !now || clockFault) return;
  BuddyHouseState a = {}, b = {};
  bool hasA = readHouse("home-a", a), hasB = readHouse("home-b", b);
  if (hasA || hasB) {
    bool useB = hasB && (!hasA || (int32_t)(b.sequence - a.sequence) > 0);
    buddyHouse = useB ? b : a;
    activeHouseSlot = useB ? 1 : 0;
    houseSequence = buddyHouse.sequence;
    houseLocked = false;
    houseSaveFailed = false;
    return;
  }
  if (preferences.getBytesLength("home-a") || preferences.getBytesLength("home-b")) {
    houseLocked = true;
    return;
  }
  buddyHouse = {};
  buddyHouse.magic = BUDDY_HOUSE_MAGIC;
  buddyHouse.version = BUDDY_HOUSE_VERSION;
  buddyHouse.siteSeed = esp_random() ^ buddy.seed ^ 0xA851E72DUL;
  if (!buddyHouse.siteSeed) buddyHouse.siteSeed = 1;
  buddyHouse.style = buddyHouse.siteSeed % 4;
  buddyHouse.lastDay = now / 86400UL;
  houseLocked = false;
  saveBuddyHouse();
  if (!houseSaveFailed) saveBuddyHouse(); // establish both recovery slots
}

static uint32_t houseDayHash(uint32_t day, uint32_t salt) {
  uint32_t x = buddyHouse.siteSeed ^ buddy.seed ^ (day * 0x9E3779B9UL) ^ salt;
  x ^= x >> 16; x *= 0x7FEB352DUL;
  x ^= x >> 15; x *= 0x846CA68BUL;
  return x ^ (x >> 16);
}

static bool canBuildHouse(uint32_t epoch) {
  return buddy.alive && buddyAgeDays(epoch) >= 15 && buddy.health >= 350 &&
    buddy.energy >= 300 && (buddy.possessions & (1 << OBJECT_NOOK)) &&
    (buddy.possessions & (1 << OBJECT_STONE));
}

void advanceBuddyHouseTo(uint32_t epoch) {
  if (houseLocked || buddyHouse.magic != BUDDY_HOUSE_MAGIC || clockFault || !epoch) return;
  uint32_t targetDay = epoch / 86400UL;
  if (targetDay <= buddyHouse.lastDay) return;
  // The RTC's maximum representable gap is bounded; each day is processed once.
  for (uint32_t day = buddyHouse.lastDay + 1; day <= targetDay; day++) {
    uint32_t dayEpoch = day * 86400UL;
    uint32_t roll = houseDayHash(day, 0x1A05UL) % 100;
    if (buddyHouse.phase == HOUSE_EMPTY && canBuildHouse(dayEpoch) && roll < 75) {
      buddyHouse.phase = HOUSE_BUILDING;
      buddyHouse.work = 0;
      rememberWorldEvent(dayEpoch, EVENT_HOUSE_STARTED, OBJECT_NOOK, BEHAVIOR_REST, 35);
    }
    if (buddyHouse.phase == HOUSE_BUILDING && canBuildHouse(dayEpoch)) {
      // Building takes many in-world months; a tired or unwell Buddy pauses.
      uint32_t willingness = 35 + buddy.traits.independence / 3 + buddy.traits.resilience / 5;
      if (roll < willingness) {
        uint16_t previous = buddyHouse.work;
        uint16_t effort = 7 + buddy.traits.independence / 13 +
          buddy.traits.resilience / 20 + houseDayHash(day, 0xB114UL) % 5;
        buddyHouse.work = min<uint16_t>(BUDDY_HOUSE_WORK_TARGET, previous + effort);
        buddy.energy = clamp1000((int)buddy.energy - 14);
        buddy.security = clamp1000((int)buddy.security + 3);
        if (previous / 100 != buddyHouse.work / 100)
          rememberWorldEvent(dayEpoch, EVENT_HOUSE_PROGRESS, OBJECT_NOOK, BEHAVIOR_REST, 28);
        if (buddyHouse.work == BUDDY_HOUSE_WORK_TARGET) {
          buddyHouse.phase = HOUSE_OCCUPIED;
          buddyHouse.condition = 1000;
          buddyHouse.builderGeneration = buddy.generation;
          buddyHouse.builtEpoch = dayEpoch;
          buddyHouse.cycles++;
          buddy.security = clamp1000(buddy.security + 100);
          buddy.comfort = clamp1000(buddy.comfort + 80);
          rememberWorldEvent(dayEpoch, EVENT_HOUSE_FINISHED, OBJECT_NOOK, BEHAVIOR_REST, 80);
          buddySpecialPending = true;
          buddySpecialEpoch = dayEpoch;
        }
      }
    } else if (buddyHouse.phase == HOUSE_OCCUPIED) {
      // The home helps every generation, but maintenance cannot defeat age.
      uint16_t wear = 2 + (houseDayHash(day, 0x0EACUL) % 4 == 0);
      buddyHouse.condition = buddyHouse.condition > wear ? buddyHouse.condition - wear : 0;
      if (buddy.alive) {
        buddy.security = clamp1000(buddy.security + (buddyHouse.condition >= 250 ? 8 : 2));
        buddy.comfort = clamp1000(buddy.comfort + (buddyHouse.condition >= 250 ? 6 : 1));
        if (buddyHouse.condition < 350) {
          // Living in a worn home is less reassuring, even before collapse.
          buddy.security = clamp1000((int)buddy.security - 7);
        }
        if (buddyHouse.condition > 0 && buddyHouse.condition < 800 &&
            buddy.energy > 400 && buddy.health > 500 &&
            roll < 8 + buddy.traits.resilience / 12 +
              min<uint16_t>(buddy.visits[OBJECT_NOOK] / 25, 8) +
              (buddyHouse.condition < 350 ? 4 : 0)) {
          buddyHouse.condition = min<uint16_t>(999, buddyHouse.condition + 3);
          if (buddyHouse.condition % 100 < 3)
            rememberWorldEvent(dayEpoch, EVENT_HOUSE_REPAIRED, OBJECT_NOOK, BEHAVIOR_REST, 20);
        }
      }
      if (!buddyHouse.condition) {
        buddyHouse.phase = HOUSE_RUIN;
        buddyHouse.collapsedEpoch = dayEpoch;
        buddyHouse.work = 0;
        if (buddy.alive) {
          // Losing a place he made his own hurts more than losing a house he
          // barely used; later generations may feel that loss differently.
          buddy.security = clamp1000((int)buddy.security - 120 - buddy.affection[OBJECT_NOOK] / 12);
          buddy.mood = clamp1000((int)buddy.mood - 80 - buddy.affection[OBJECT_NOOK] / 15);
          rememberWorldEvent(dayEpoch, EVENT_HOUSE_COLLAPSED, OBJECT_NOOK, BEHAVIOR_REST, -65);
        }
      }
    } else if (buddyHouse.phase == HOUSE_RUIN &&
               dayEpoch - buddyHouse.collapsedEpoch >= 30UL * 86400UL) {
      buddyHouse.phase = HOUSE_EMPTY;
    }
    buddyHouse.lastDay = day;
    saveBuddyHouse();
  }
}
