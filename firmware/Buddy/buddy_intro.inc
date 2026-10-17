static uint32_t introSequence = 0;
static int8_t activeIntroSlot = -1;

static uint32_t introChecksum(const BuddyIntroState &state) {
  BuddyIntroState copy = state;
  copy.checksum = 0;
  uint32_t hash = 2166136261UL;
  const uint8_t *bytes = (const uint8_t *)&copy;
  for (size_t i = 0; i < sizeof(copy); i++) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}

static bool validBuddyIntro(const BuddyIntroState &state) {
  if (state.magic != BUDDY_INTRO_MAGIC || state.version != BUDDY_INTRO_VERSION ||
      state.step > INTRO_COMPLETE || state.checksum != introChecksum(state)) return false;
  if (state.step < INTRO_COMPLETE && state.cursor >= introOptionCount(state.step)) return false;
  if ((state.step >= INTRO_GIFT && state.sex > BUDDY_GIRL) ||
      (state.step >= INTRO_SPARK && state.gift >= OBJECT_COUNT) ||
      (state.step >= INTRO_COMPLETE && state.spark > SPARK_FRIEND)) return false;
  if ((state.seed == 0) != (state.birthEpoch == 0)) return false;
  return !state.seed || buddySexFromSeed(state.seed) == state.sex;
}

static bool readBuddyIntro(const char *key, BuddyIntroState &state) {
  return preferences.getBytesLength(key) == sizeof(state) &&
    preferences.getBytes(key, &state, sizeof(state)) == sizeof(state) &&
    validBuddyIntro(state);
}

void saveBuddyIntro() {
  if (storageLocked || introLocked || buddyIntro.magic != BUDDY_INTRO_MAGIC) return;
  BuddyIntroState next = buddyIntro, check = {};
  next.sequence = introSequence + 1;
  next.checksum = introChecksum(next);
  int8_t slot = activeIntroSlot == 0 ? 1 : 0;
  const char *key = slot == 0 ? "intro-a" : "intro-b";
  introSaveFailed = preferences.putBytes(key, &next, sizeof(next)) != sizeof(next) ||
    !readBuddyIntro(key, check) || check.sequence != next.sequence;
  if (!introSaveFailed) {
    buddyIntro = next;
    introSequence = next.sequence;
    activeIntroSlot = slot;
  }
}

void retryBuddyIntroSave() {
  if (introRequired && introSaveFailed && !introLocked) saveBuddyIntro();
}

void setupBuddyIntro() {
  if (storageLocked || buddy.magic == BUDDY_MAGIC) return;
  introRequired = true;
  BuddyIntroState a = {}, b = {};
  bool hasA = readBuddyIntro("intro-a", a), hasB = readBuddyIntro("intro-b", b);
  if (hasA || hasB) {
    bool useB = hasB && (!hasA || (int32_t)(b.sequence - a.sequence) > 0);
    buddyIntro = useB ? b : a;
    activeIntroSlot = useB ? 1 : 0;
    introSequence = buddyIntro.sequence;
    introLocked = false;
    introSaveFailed = false;
    return;
  }
  if (preferences.getBytesLength("intro-a") || preferences.getBytesLength("intro-b")) {
    introLocked = true; // Never guess at potentially completed choices.
    return;
  }
  buddyIntro = {};
  buddyIntro.magic = BUDDY_INTRO_MAGIC;
  buddyIntro.version = BUDDY_INTRO_VERSION;
  buddyIntro.sex = buddyIntro.gift = buddyIntro.spark = 255;
  introLocked = false;
  saveBuddyIntro();
  if (!introSaveFailed) saveBuddyIntro();
}

bool birthBuddyFromIntro(uint32_t now) {
  if (introSaveFailed) saveBuddyIntro();
  if (!introRequired || introLocked || introSaveFailed ||
      buddyIntro.step != INTRO_COMPLETE || !now || clockFault || storageLocked) return false;
  if (!buddyIntro.seed) {
    buddyIntro.birthEpoch = now;
    buddyIntro.seed = esp_random() ^ now ^ (1UL << 16) ^ buddyDeviceSalt();
    buddyIntro.seed = (buddyIntro.seed & ~1UL) | buddyIntro.sex;
    if (!buddyIntro.seed) buddyIntro.seed = 2; // boy; nonzero
    saveBuddyIntro();
    if (introSaveFailed) return false;
    saveBuddyIntro();
    if (introSaveFailed) return false;
  }
  createBuddyLife(buddyIntro.birthEpoch, false);
  introRequired = false; // failed life writes retry via normal storage service
  return buddy.magic == BUDDY_MAGIC;
}

void handleBuddyIntroButton(ButtonEvent event) {
  if (!introRequired || introLocked || buddyIntro.step >= INTRO_COMPLETE) return;
  uint8_t count = introOptionCount(buddyIntro.step);
  if (event == BUTTON_SHORT) buddyIntro.cursor = (buddyIntro.cursor + 1) % count;
  else if (event == BUTTON_DOUBLE) buddyIntro.cursor = (buddyIntro.cursor + count - 1) % count;
  else if (event == BUTTON_LONG) {
    if (buddyIntro.step == INTRO_FORM) buddyIntro.sex = buddyIntro.cursor;
    else if (buddyIntro.step == INTRO_GIFT) buddyIntro.gift = buddyIntro.cursor;
    else buddyIntro.spark = buddyIntro.cursor;
    buddyIntro.step++;
    buddyIntro.cursor = 0;
  } else return;
  saveBuddyIntro();
  needsRender = true;
  cleanPartialRequested = true;
}
