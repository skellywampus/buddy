// Ambitions belong to a life, but occupy their own recoverable NVS record.
// This lets a many-week goal outlive the 24-event ring without changing the
// established v2 life-save ABI or depending on an SD card.
static uint32_t ambitionSequence = 0;
static int8_t activeAmbitionSlot = -1;
static int8_t migrationProtectedAmbitionSlot = -1;

static uint32_t ambitionHash(const BuddyAmbitionState &state) {
  BuddyAmbitionState copy = state;
  copy.checksum = 0;
  uint32_t hash = 2166136261UL;
  const uint8_t *bytes = (const uint8_t *)&copy;
  for (size_t i = 0; i < sizeof(copy); i++) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}

static bool validAmbition(const BuddyAmbitionState &state) {
  uint8_t none = state.version == 1 ? OLD_OBJECT_COUNT : OBJECT_COUNT;
  if (state.magic != BUDDY_AMBITION_MAGIC ||
      (state.version != 1 && state.version != BUDDY_AMBITION_VERSION) ||
      !state.generation || !state.birthEpoch || state.kind > none ||
      state.lastKind > none || state.legacyKind > none ||
      state.lastOutcome > AMBITION_ABANDONED || state.legacyStrength > 2 ||
      state.progress >= BUDDY_AMBITION_TARGET || state.checksum != ambitionHash(state)) return false;
  if (state.kind == none)
    return !state.progress && !state.startedEpoch && !state.lastWorkDay;
  return state.startedEpoch >= state.birthEpoch &&
    (!state.lastWorkDay || state.lastWorkDay >= state.startedEpoch / 86400UL);
}

static bool readAmbition(const char *key, BuddyAmbitionState &state) {
  return preferences.getBytesLength(key) == sizeof(state) &&
    preferences.getBytes(key, &state, sizeof(state)) == sizeof(state) && validAmbition(state);
}

void saveBuddyAmbition() {
  if ((buddyCompletionPending && !buddyCompletionApplying) || buddyCompletionPreparing) return;
  if (storageLocked || ambitionLocked || buddyAmbition.magic != BUDDY_AMBITION_MAGIC) return;
  BuddyAmbitionState next = buddyAmbition, check = {};
  next.sequence = ambitionSequence + 1;
  next.checksum = ambitionHash(next);
  int8_t slot = activeAmbitionSlot == 0 ? 1 : 0;
  if (migrationProtectedAmbitionSlot >= 0) slot = 1 - migrationProtectedAmbitionSlot;
  const char *key = slot == 0 ? "aim-a" : "aim-b";
  ambitionSaveFailed = preferences.putBytes(key, &next, sizeof(next)) != sizeof(next) ||
    !readAmbition(key, check) || check.sequence != next.sequence;
  if (!ambitionSaveFailed) {
    buddyAmbition = next;
    ambitionSequence = next.sequence;
    activeAmbitionSlot = slot;
  }
}

void setupBuddyAmbition(uint32_t now) {
  if (storageLocked || buddy.magic != BUDDY_MAGIC || !now || clockFault) return;
  BuddyAmbitionState a = {}, b = {};
  bool hasA = readAmbition("aim-a", a), hasB = readAmbition("aim-b", b);
  migrationProtectedAmbitionSlot = hasA && a.version == 1 ? 0 :
    hasB && b.version == 1 ? 1 : -1;
  if (hasA || hasB) {
    bool useB = hasB && (!hasA || (int32_t)(b.sequence - a.sequence) > 0);
    if ((useB ? b : a).version == 1) migrationProtectedAmbitionSlot = useB ? 1 : 0;
    buddyAmbition = useB ? b : a;
    activeAmbitionSlot = useB ? 1 : 0;
    ambitionSequence = buddyAmbition.sequence;
    ambitionLocked = false;
    ambitionSaveFailed = false;
    if (buddyAmbition.version == 1) {
      if (buddyAmbition.kind == OLD_OBJECT_COUNT) buddyAmbition.kind = OBJECT_COUNT;
      if (buddyAmbition.lastKind == OLD_OBJECT_COUNT) buddyAmbition.lastKind = OBJECT_COUNT;
      if (buddyAmbition.legacyKind == OLD_OBJECT_COUNT) buddyAmbition.legacyKind = OBJECT_COUNT;
      buddyAmbition.version = BUDDY_AMBITION_VERSION;
      saveBuddyAmbition();
      if (!ambitionSaveFailed) saveBuddyAmbition();
    }
    if (buddyAmbition.lifeSeed == buddy.seed && buddyAmbition.birthEpoch == buddy.birthEpoch &&
        buddyAmbition.generation == buddy.generation) return;
    // A stale life slot must never overwrite a newer ambition record. If a
    // recent birth failed to save, wait for life storage to recover instead.
    if (buddyAmbition.generation >= buddy.generation || buddyAmbition.birthEpoch > buddy.birthEpoch) {
      ambitionLocked = true;
      return;
    }
  } else if (preferences.getBytesLength("aim-a") || preferences.getBytesLength("aim-b")) {
    ambitionLocked = true;
    return;
  }

  uint8_t legacyKind = OBJECT_COUNT, legacyStrength = 0;
  if (buddyAmbition.magic == BUDDY_AMBITION_MAGIC &&
      buddyAmbition.generation + 1 == buddy.generation) {
    if (buddyAmbition.kind < OBJECT_COUNT) {
      legacyKind = buddyAmbition.kind;
      legacyStrength = 1; // An unfinished wish is an invitation, not a command.
    } else if (buddyAmbition.lastOutcome != AMBITION_NONE) {
      legacyKind = buddyAmbition.lastKind;
      legacyStrength = buddyAmbition.lastOutcome == AMBITION_FULFILLED ? 2 : 0;
    }
  }
  buddyAmbition = {};
  buddyAmbition.magic = BUDDY_AMBITION_MAGIC;
  buddyAmbition.version = BUDDY_AMBITION_VERSION;
  buddyAmbition.lifeSeed = buddy.seed;
  buddyAmbition.birthEpoch = buddy.birthEpoch;
  buddyAmbition.generation = buddy.generation;
  buddyAmbition.kind = OBJECT_COUNT;
  buddyAmbition.lastKind = OBJECT_COUNT;
  buddyAmbition.legacyKind = legacyKind;
  buddyAmbition.legacyStrength = legacyStrength;
  ambitionLocked = false;
  saveBuddyAmbition();
  if (!ambitionSaveFailed) saveBuddyAmbition();
}

static bool ambitionForCurrentLife() {
  return !ambitionLocked && buddyAmbition.magic == BUDDY_AMBITION_MAGIC &&
    buddyAmbition.lifeSeed == buddy.seed && buddyAmbition.birthEpoch == buddy.birthEpoch &&
    buddyAmbition.generation == buddy.generation;
}

static uint8_t chooseBuddyAmbition(uint32_t epoch) {
  uint8_t best = OBJECT_COUNT;
  int bestScore = -10000;
  uint32_t day = epoch / 86400UL;
  for (uint8_t kind = 0; kind < OBJECT_COUNT; kind++) {
    if (buddy.visits[kind] < 3 || buddy.affection[kind] < 40) continue;
    int score = buddyAmbitionAffinity(buddy, kind);
    uint32_t mixed = buddy.seed ^ (day * 0x9E3779B9UL) ^ ((uint32_t)(kind + 1) * 0x85EBCA6BUL);
    mixed ^= mixed >> 16; mixed *= 0x7FEB352DUL; mixed ^= mixed >> 15;
    score += (mixed >> 16) % 80;
    if (kind == buddyAmbition.lastKind) score -= 110;
    if (kind == buddyAmbition.legacyKind)
      score += buddyAmbition.legacyStrength == 2 ? 95 :
        buddyAmbition.legacyStrength == 1 ? 45 : -55;
    if (!legacyLocked && buddyLegacy.magic == BUDDY_LEGACY_MAGIC)
      score += buddyLegacy.condition[kind] / 2;
    if (score > bestScore) { bestScore = score; best = kind; }
  }
  return best;
}

static void endBuddyAmbition(uint32_t epoch, BuddyAmbitionOutcome outcome) {
  uint8_t kind = buddyAmbition.kind;
  if (kind >= OBJECT_COUNT) return;
  BuddyState beforeLife = buddy;
  BuddyAmbitionState beforeAmbition = buddyAmbition;
  BuddyLegacyState beforeLegacy = buddyLegacy;
  buddyCompletionPreparing = true;
  buddyAmbition.kind = OBJECT_COUNT;
  buddyAmbition.lastKind = kind;
  buddyAmbition.lastOutcome = outcome;
  buddyAmbition.lastEndedEpoch = epoch;
  buddyAmbition.startedEpoch = 0;
  buddyAmbition.lastWorkDay = 0;
  buddyAmbition.progress = 0;
  if (outcome == AMBITION_FULFILLED) {
    buddyLegacyComplete(epoch, kind);
    buddy.affection[kind] = clamp1000(buddy.affection[kind] + 90);
    buddy.mood = clamp1000(buddy.mood + 70);
    BuddyBehavior practiced = pursuitBehavior(kind);
    buddy.behaviorLearning[practiced] = constrain((int)buddy.behaviorLearning[practiced] + 8, -50, 50);
    if (kind == OBJECT_STONE) {
      buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 90);
      buddy.development[0] += 8;
    } else if (kind == OBJECT_PLANT) {
      buddy.plantGrowth = min<int>(100, buddy.plantGrowth + 15);
      buddy.security = clamp1000(buddy.security + 50);
    } else if (kind == OBJECT_NOOK) {
      buddy.security = clamp1000(buddy.security + 90);
      buddy.social = clamp1000(buddy.social + 35);
    } else if (kind == OBJECT_GROVE) {
      buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 85);
      buddy.comfort = clamp1000(buddy.comfort + 35);
    } else if (kind == OBJECT_FLOWERS) {
      buddy.comfort = clamp1000(buddy.comfort + 85);
      buddy.mood = clamp1000(buddy.mood + 35);
    } else if (kind == OBJECT_JOURNAL) {
      buddy.social = clamp1000(buddy.social + 85);
      buddy.security = clamp1000(buddy.security + 35);
    } else buddy.play = clamp1000(buddy.play + 100);
    rememberEvent(MEMORY_MILESTONE, 75, epoch);
    buddySpecialPending = true;
    buddySpecialEpoch = epoch;
  } else {
    // Letting go is a real disappointment, not a permanent failure.
    buddy.mood = clamp1000((int)buddy.mood - 18);
    buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 20);
    rememberEvent(MEMORY_MILESTONE, -15, epoch);
  }
  rememberWorldEvent(epoch, outcome == AMBITION_FULFILLED ? EVENT_AMBITION_FULFILLED :
                    EVENT_AMBITION_RELEASED, kind, pursuitBehavior(kind),
                    outcome == AMBITION_FULFILLED ? 80 : -15);
  if (buddySimulationStepActive) {
    buddySimulationStepCompleted = true;
    return; // publish only after world, house and cursor finish this hour
  }
  buddyCompletionPreparing = false;
  if (!persistBuddyCompletion()) {
    buddy = beforeLife;
    buddyAmbition = beforeAmbition;
    buddyLegacy = beforeLegacy;
    buddySpecialPending = false;
    ambitionSaveFailed = true;
    return;
  }
  recoverBuddyCompletion();
}

void serviceBuddyAmbition(uint32_t epoch) {
  if (!ambitionForCurrentLife() || !buddy.alive || !epoch || clockFault) return;
  uint32_t day = epoch / 86400UL;
  if (buddyAmbition.kind < OBJECT_COUNT) {
    uint32_t last = buddyAmbition.lastWorkDay ? buddyAmbition.lastWorkDay :
      buddyAmbition.startedEpoch / 86400UL;
    if (day > last + 28 && buddy.energy > 350 && buddy.health > 400) {
      int current = buddyAmbitionAffinity(buddy, buddyAmbition.kind);
      int bestOther = -10000;
      for (uint8_t kind = 0; kind < OBJECT_COUNT; kind++)
        if (kind != buddyAmbition.kind && buddy.visits[kind] >= 3)
          bestOther = max(bestOther, buddyAmbitionAffinity(buddy, kind));
      if (day > last + 75 || bestOther > current + 40)
        endBuddyAmbition(epoch, AMBITION_ABANDONED);
    }
    return;
  }
  if (buddy.currentStage < STAGE_CHILD || buddy.health < 350 || buddy.energy < 350 ||
      (buddyAmbition.lastEndedEpoch && epoch - buddyAmbition.lastEndedEpoch < 14UL * 86400UL)) return;
  uint8_t kind = chooseBuddyAmbition(epoch);
  if (kind >= OBJECT_COUNT) return;
  buddyAmbition.kind = kind;
  buddyAmbition.startedEpoch = epoch;
  buddyAmbition.lastWorkDay = 0;
  buddyAmbition.progress = 0;
  rememberWorldEvent(epoch, EVENT_AMBITION_STARTED, kind, pursuitBehavior(kind), 32);
  saveBuddyAmbition();
}

void buddyAmbitionOnEvent(uint32_t epoch, LifeEvent event, uint8_t object) {
  if (!ambitionForCurrentLife() || !buddy.alive || buddyAmbition.kind >= OBJECT_COUNT ||
      epoch < buddyAmbition.startedEpoch) return;
  uint32_t day = epoch / 86400UL;
  if (buddyAmbition.lastWorkDay == day) return;
  uint8_t work = buddyAmbitionWork(buddyAmbition.kind, event, object);
  if (!work) return;
  uint16_t progress = buddyAmbition.progress + work;
  buddyAmbition.lastWorkDay = day;
  if (progress >= BUDDY_AMBITION_TARGET) endBuddyAmbition(epoch, AMBITION_FULFILLED);
  else {
    buddyAmbition.progress = progress;
    saveBuddyAmbition();
  }
}
