// Durable family-site marks. These are neither possessions nor house parts:
// they can be made without a built house and can survive its eventual ruin.
static uint32_t legacySequence = 0;
static int8_t activeLegacySlot = -1;
static int8_t migrationProtectedLegacySlot = -1;

template<typename State> static uint32_t legacyHash(const State &state) {
  State copy = state;
  copy.checksum = 0;
  uint32_t hash = 2166136261UL;
  const uint8_t *bytes = (const uint8_t *)&copy;
  for (size_t i = 0; i < sizeof(copy); i++) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}

static bool validLegacy(const BuddyLegacyState &state) {
  if (state.magic != BUDDY_LEGACY_MAGIC || state.version != BUDDY_LEGACY_VERSION ||
      state.lastWeatherDay > UINT32_MAX / 86400UL || state.checksum != legacyHash(state)) return false;
  for (uint8_t i = 0; i < OBJECT_COUNT; i++) {
    if (state.condition[i] > 100 ||
        (state.condition[i] && !state.makerGeneration[i]) ||
        (!state.condition[i] && (state.makerGeneration[i] || state.lastCareDay[i])) ||
        state.lastCareDay[i] > state.lastWeatherDay + 6) return false;
  }
  return true;
}
static bool validLegacyV1(const BuddyLegacyStateV1 &state) {
  if (state.magic != BUDDY_LEGACY_MAGIC || state.version != 1 ||
      state.lastWeatherDay > UINT32_MAX / 86400UL ||
      state.checksum != legacyHash(state)) return false;
  for (uint8_t i = 0; i < OLD_OBJECT_COUNT; i++) {
    if (state.condition[i] > 100 ||
        (state.condition[i] && !state.makerGeneration[i]) ||
        (!state.condition[i] && (state.makerGeneration[i] || state.lastCareDay[i])) ||
        state.lastCareDay[i] > state.lastWeatherDay + 6) return false;
  }
  return true;
}

static bool readLegacy(const char *key, BuddyLegacyState &state) {
  return preferences.getBytesLength(key) == sizeof(state) &&
    preferences.getBytes(key, &state, sizeof(state)) == sizeof(state) && validLegacy(state);
}
static bool readAnyLegacy(const char *key, BuddyLegacyState &state, bool &fromV1) {
  if (readLegacy(key, state)) { fromV1 = false; return true; }
  BuddyLegacyStateV1 old = {};
  if (preferences.getBytesLength(key) != sizeof(old) ||
      preferences.getBytes(key, &old, sizeof(old)) != sizeof(old) || !validLegacyV1(old)) return false;
  state = {};
  state.magic = old.magic;
  state.sequence = old.sequence;
  state.lastWeatherDay = old.lastWeatherDay;
  state.version = BUDDY_LEGACY_VERSION;
  for (uint8_t i = 0; i < OLD_OBJECT_COUNT; i++) {
    state.lastCareDay[i] = old.lastCareDay[i];
    state.makerGeneration[i] = old.makerGeneration[i];
    state.condition[i] = old.condition[i];
  }
  fromV1 = true;
  return true;
}

void saveBuddyLegacy() {
  if ((buddyCompletionPending && !buddyCompletionApplying) || buddyCompletionPreparing) return;
  if (storageLocked || legacyLocked || buddyLegacy.magic != BUDDY_LEGACY_MAGIC) return;
  BuddyLegacyState next = buddyLegacy, check = {};
  next.sequence = legacySequence + 1;
  next.checksum = legacyHash(next);
  int8_t slot = activeLegacySlot == 0 ? 1 : 0;
  if (migrationProtectedLegacySlot >= 0) slot = 1 - migrationProtectedLegacySlot;
  const char *key = slot == 0 ? "site-a" : "site-b";
  legacySaveFailed = preferences.putBytes(key, &next, sizeof(next)) != sizeof(next) ||
    !readLegacy(key, check) || check.sequence != next.sequence;
  if (!legacySaveFailed) {
    buddyLegacy = next;
    legacySequence = next.sequence;
    activeLegacySlot = slot;
  }
}

void setupBuddyLegacy(uint32_t now) {
  if (storageLocked || buddy.magic != BUDDY_MAGIC || !now || clockFault) return;
  BuddyLegacyState a = {}, b = {};
  bool oldA = false, oldB = false;
  bool hasA = readAnyLegacy("site-a", a, oldA), hasB = readAnyLegacy("site-b", b, oldB);
  migrationProtectedLegacySlot = hasA && oldA ? 0 : hasB && oldB ? 1 : -1;
  if (hasA || hasB) {
    bool useB = hasB && (!hasA || (int32_t)(b.sequence - a.sequence) > 0);
    if (useB ? oldB : oldA) migrationProtectedLegacySlot = useB ? 1 : 0;
    buddyLegacy = useB ? b : a;
    activeLegacySlot = useB ? 1 : 0;
    legacySequence = buddyLegacy.sequence;
    legacyLocked = legacySaveFailed = false;
    if (useB ? oldB : oldA) {
      saveBuddyLegacy();
      if (!legacySaveFailed) saveBuddyLegacy();
    }
    return;
  }
  if (preferences.getBytesLength("site-a") || preferences.getBytesLength("site-b")) {
    legacyLocked = true;
    return;
  }
  buddyLegacy = {};
  buddyLegacy.magic = BUDDY_LEGACY_MAGIC;
  buddyLegacy.version = BUDDY_LEGACY_VERSION;
  buddyLegacy.lastWeatherDay = now / 86400UL;
  legacyLocked = false;
  saveBuddyLegacy();
  if (!legacySaveFailed) saveBuddyLegacy();
}

static bool legacyAvailable(uint8_t object) {
  return !legacyLocked && buddyLegacy.magic == BUDDY_LEGACY_MAGIC &&
    buddyLegacyUsable(buddyLegacy, object);
}

void buddyLegacyComplete(uint32_t epoch, uint8_t object) {
  if (legacyLocked || buddyLegacy.magic != BUDDY_LEGACY_MAGIC ||
      object >= OBJECT_COUNT || !epoch || !buddy.alive) return;
  uint32_t day = epoch / 86400UL;
  if (day > buddyLegacy.lastWeatherDay) advanceBuddyLegacyTo(epoch);
  buddyLegacy.condition[object] = min<uint8_t>(100,
    max<uint8_t>(BUDDY_LEGACY_NEW_CONDITION, buddyLegacy.condition[object]) + 15);
  buddyLegacy.makerGeneration[object] = buddy.generation;
  buddyLegacy.lastCareDay[object] = day;
  if (buddyLegacy.lastWeatherDay > day) buddyLegacy.lastWeatherDay = day;
  saveBuddyLegacy();
}

void advanceBuddyLegacyTo(uint32_t epoch) {
  if (legacyLocked || buddyLegacy.magic != BUDDY_LEGACY_MAGIC || clockFault || !epoch) return;
  uint32_t day = epoch / 86400UL;
  if (day <= buddyLegacy.lastWeatherDay + 5) return;
  bool any = false;
  for (uint8_t i = 0; i < OBJECT_COUNT; i++) any |= buddyLegacy.condition[i] != 0;
  if (!any) { buddyLegacy.lastWeatherDay = day; return; }
  uint32_t steps = (day - buddyLegacy.lastWeatherDay) / 6;
  buddyLegacy.lastWeatherDay += steps * 6;
  for (uint8_t i = 0; i < OBJECT_COUNT; i++) {
    if (!buddyLegacy.condition[i]) continue;
    if (steps >= buddyLegacy.condition[i]) {
      buddyLegacy.condition[i] = 0;
      buddyLegacy.makerGeneration[i] = 0;
      buddyLegacy.lastCareDay[i] = 0;
      if (buddy.alive && buddy.visits[i] >= 5 && buddy.affection[i] >= 150) {
        buddy.mood = clamp1000((int)buddy.mood - 24);
        rememberWorldEvent(epoch, EVENT_LEGACY_LOST, i, pursuitBehavior(i), -35);
      }
    } else buddyLegacy.condition[i] -= steps;
  }
  saveBuddyLegacy();
}

void buddyLegacyOnEvent(uint32_t epoch, LifeEvent event, uint8_t object) {
  if (!buddy.alive || !legacyAvailable(object) || !epoch ||
      !buddyAmbitionWork(object, event, object)) return;
  uint32_t day = epoch / 86400UL;
  if (buddyLegacy.lastCareDay[object] == day) return;
  buddyLegacy.lastCareDay[object] = day;
  buddyLegacy.condition[object] = min<uint8_t>(100, buddyLegacy.condition[object] + 1);
  int benefit = 4 + buddyLegacy.condition[object] / 8;
  if (object == OBJECT_STONE || object == OBJECT_GROVE)
    buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + benefit);
  else if (object == OBJECT_PLANT || object == OBJECT_FLOWERS) {
    buddy.comfort = clamp1000(buddy.comfort + benefit);
    buddy.plantGrowth = min<int>(100, buddy.plantGrowth + 1);
  } else if (object == OBJECT_NOOK || object == OBJECT_JOURNAL) {
    buddy.security = clamp1000(buddy.security + benefit);
    buddy.social = clamp1000(buddy.social + benefit / 2);
  } else buddy.play = clamp1000(buddy.play + benefit);
  if (buddyLegacy.makerGeneration[object] != buddy.generation && buddy.affection[object] >= 120)
    buddy.mood = clamp1000(buddy.mood + 4);
  saveBuddyLegacy();
}
