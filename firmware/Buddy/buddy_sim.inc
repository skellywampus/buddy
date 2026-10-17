static uint16_t clamp1000(int32_t value) {
  if (value < 0) return 0;
  if (value > 1000) return 1000;
  return (uint16_t)value;
}

static uint32_t buddyRandom() {
  uint32_t x = buddy.rngState ? buddy.rngState : 0x6D2B79F5UL;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  buddy.rngState = x;
  return x;
}

static int randomRange(int low, int high) {
  if (high <= low) return low;
  return low + (buddyRandom() % (uint32_t)(high - low + 1));
}

const char *stageName(BuddyStage stage) {
  switch (stage) {
    case STAGE_BABY: return "BABY";
    case STAGE_CHILD: return "CHILD";
    case STAGE_YOUNG: return "YOUNG";
    case STAGE_ADULT: return "ADULT";
    case STAGE_ELDER: return "ELDER";
    default: return "REMEMBERED";
  }
}

const char *behaviorName(BuddyBehavior behavior) {
  switch (behavior) {
    case BEHAVIOR_SLEEP: return "SLEEPING";
    case BEHAVIOR_REST: return "RESTING";
    case BEHAVIOR_PLAY: return "PLAYING";
    case BEHAVIOR_EXPLORE: return "EXPLORING";
    case BEHAVIOR_OBSERVE: return "WATCHING";
    case BEHAVIOR_DAYDREAM: return "DAYDREAMING";
    case BEHAVIOR_SELF_SOOTHE: return "REFLECTING";
    case BEHAVIOR_WARM_UP: return "KEEPING WARM";
    case BEHAVIOR_COOL_DOWN: return "COOLING DOWN";
    default: return "BEING";
  }
}

uint32_t buddyAgeDays(uint32_t now) {
  if (!buddy.birthEpoch || now < buddy.birthEpoch) return 0;
  uint32_t end = buddy.alive ? now : buddy.deathEpoch;
  return (end - buddy.birthEpoch) / 86400UL;
}

BuddyStage stageForAge(uint32_t days) {
  if (!buddy.alive) return STAGE_GONE;
  if (days < 15) return STAGE_BABY;
  if (days < 61) return STAGE_CHILD;
  if (days < 151) return STAGE_YOUNG;
  if (days < min<uint32_t>(301, buddyLifespanDays(buddy.seed) - 45)) return STAGE_ADULT;
  return STAGE_ELDER;
}

void rememberEvent(MemoryType type, int8_t intensity, uint32_t epoch) {
  BuddyMemory &memory = buddy.memories[buddy.memoryHead % 16];
  memory.epoch = epoch;
  memory.type = type;
  memory.intensity = intensity;
  buddy.memoryHead = (buddy.memoryHead + 1) % 16;
  if (buddy.memoryCount < 16) buddy.memoryCount++;
}

static uint8_t mutatedTrait(uint8_t inherited) {
  return (uint8_t)constrain((int)inherited + randomRange(-18, 18), 5, 95);
}

static uint8_t buddyHouseShelter() {
  return !houseLocked && buddyHouse.magic == BUDDY_HOUSE_MAGIC &&
    buddyHouse.phase == HOUSE_OCCUPIED ? buddyHouse.condition / 10 : 0;
}

void createBuddyLife(uint32_t now, bool reincarnation) {
  if (buddyCompletionPending) return;
  if (!now || clockFault || storageLocked) return;
  // Do not overwrite an unarchived life, even when the SD card is absent.
  if (reincarnation && !queueMemorial()) { archiveBlocked = true; return; }
  archiveBlocked = false;
  uint8_t parentEar = buddy.familyEar, parentMarking = buddy.familyMarking;
  uint8_t parentObject = buddy.favoriteObject;
  bool stayAwake = buddy.stayAwake;
  PriorLife previous = buddy.prior;
  uint16_t generation = reincarnation ? previous.generation + 1 : 1;
  bool sound = reincarnation ? buddy.soundEnabled : true;
  bool welcomed = !reincarnation && introRequired && buddyIntro.magic == BUDDY_INTRO_MAGIC &&
    buddyIntro.step == INTRO_COMPLETE && buddyIntro.seed && buddyIntro.birthEpoch == now;
  uint32_t seed = welcomed ? buddyIntro.seed : esp_random() ^ now ^
    ((uint32_t)generation << 16) ^ (reincarnation ? 0 : buddyDeviceSalt());
  memset(&buddy, 0, sizeof(buddy));
  buddy.magic = BUDDY_MAGIC;
  buddy.version = BUDDY_STATE_VERSION;
  buddy.alive = true;
  buddy.soundEnabled = sound;
  buddy.generation = generation;
  buddy.seed = seed;
  buddy.rngState = seed;
  buddy.birthEpoch = now;
  buddy.lastUpdateEpoch = now;
  buddy.lastLoggedDay = UINT32_MAX;
  buddy.prior = previous;

  if (reincarnation && previous.generation) {
    buddy.traits.curiosity = mutatedTrait(previous.traits.curiosity);
    buddy.traits.sociability = mutatedTrait(previous.traits.sociability);
    buddy.traits.playfulness = mutatedTrait(previous.traits.playfulness);
    buddy.traits.resilience = mutatedTrait(previous.traits.resilience);
    buddy.traits.sensitivity = mutatedTrait(previous.traits.sensitivity);
    buddy.traits.independence = mutatedTrait(previous.traits.independence);
    uint8_t inherited = buddyRandom() % 6;
    uint8_t *newTraits = (uint8_t *)&buddy.traits;
    const uint8_t *oldTraits = (const uint8_t *)&previous.traits;
    newTraits[inherited] = (uint8_t)constrain((int)oldTraits[inherited] + randomRange(-6, 6), 5, 95);
  } else {
    uint8_t *traits = (uint8_t *)&buddy.traits;
    for (uint8_t i = 0; i < sizeof(BuddyTraits); i++) traits[i] = randomRange(20, 80);
    if (welcomed) applyBuddySpark(buddy.traits, buddyIntro.spark);
  }

  // A new installation is a distinct individual from its first hour. These
  // ranges are safe for a baby; reincarnations still keep their family path.
  buddy.energy = reincarnation ? 820 : randomRange(740, 900);
  buddy.health = reincarnation ? 1000 : randomRange(920, 1000);
  buddy.social = reincarnation ? 720 : randomRange(560, 840);
  buddy.play = reincarnation ? 700 : randomRange(540, 840);
  buddy.curiosityNeed = reincarnation ? 760 : randomRange(590, 880);
  buddy.security = reincarnation ? 850 : randomRange(720, 910);
  buddy.comfort = reincarnation ? 800 : randomRange(730, 900);
  buddy.mood = reincarnation ? 800 : randomRange(680, 860);
  buddy.bond = reincarnation ? 60 : 20;
  buddy.currentBehavior = BEHAVIOR_OBSERVE;
  buddy.currentStage = STAGE_BABY;
  initializeBuddyWorld(reincarnation);
  if (!reincarnation && (welcomed || buddyRandom() % 3 == 0)) {
    uint8_t firstFind = welcomed ? buddyIntro.gift : buddyRandom() % OBJECT_COUNT;
    buddy.possessions |= 1 << firstFind;
    buddy.visits[firstFind] = 1;
    rememberWorldEvent(now, EVENT_FOUND, firstFind, BEHAVIOR_OBSERVE, 30);
  }
  buddy.stayAwake = stayAwake;
  if (reincarnation) {
    // One exact visual family feature; other features vary with the new seed.
    if (generation % 2) buddy.familyEar = parentEar;
    else buddy.familyMarking = parentMarking;
    buddy.inheritedObject = parentObject;
    if (parentObject < OBJECT_COUNT) {
      buddy.affection[parentObject] = 35;
      rememberWorldEvent(now, EVENT_INHERITED, parentObject, BEHAVIOR_OBSERVE, 20);
    }
    buddy.behaviorLearning[previous.dominantBehavior] = 8;
    if (!houseLocked && buddyHouse.magic == BUDDY_HOUSE_MAGIC && buddyHouse.phase == HOUSE_OCCUPIED) {
      buddy.security = clamp1000(buddy.security + 55);
      buddy.comfort = clamp1000(buddy.comfort + 35);
      rememberWorldEvent(now, EVENT_HOUSE_INHERITED, OBJECT_NOOK, BEHAVIOR_REST, 45);
    }
  }
  rememberEvent(MEMORY_BIRTH, 100, now);
  saveBuddy();
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  if (!saveFailed && !buddyTestClockBindLife()) {
    storageLocked = true; // fail closed; never run a life on a stale rate
    return;
  }
#endif
  if (!saveFailed) setupBuddyAmbition(now);
  writeBirthRecord();
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  cleanPartialRequested = true;
  needsRender = true;
#endif
}

const char *temperatureFeeling(float tempC) {
  if (!latestClimateValid) return "UNKNOWN";
  float shift = (buddy.traits.sensitivity - 50) * 0.03f;
  if (tempC < 5.0f) return "VERY COLD";
  if (tempC < 15.0f) return "COLD";
  if (tempC < 19.0f + shift) return "COOL";
  if (tempC > 40.0f) return "VERY HOT";
  if (tempC > 34.0f) return "HOT";
  if (tempC > 29.0f - shift) return "WARM";
  return "COZY";
}

static BuddyBehavior chooseBehavior(uint8_t hour, uint32_t epoch) {
  int32_t score[BEHAVIOR_COUNT] = {};
  BuddyInnerState inner = buddyInnerStateOf(buddy, epoch);
  bool night = hour >= 22 || hour < 7;
  score[BEHAVIOR_SLEEP] = (1000 - buddy.energy) * 2 + (night ? 500 : 0);
  score[BEHAVIOR_REST] = (1000 - buddy.energy) + (1000 - buddy.health) / 2;
  score[BEHAVIOR_PLAY] = (1000 - buddy.play) + buddy.traits.playfulness * 7;
  score[BEHAVIOR_EXPLORE] = (1000 - buddy.curiosityNeed) + buddy.traits.curiosity * 8;
  score[BEHAVIOR_OBSERVE] = buddy.traits.curiosity * 4 + buddy.traits.independence * 3;
  score[BEHAVIOR_DAYDREAM] = buddy.traits.sensitivity * 4 + buddy.traits.independence * 3;
  score[BEHAVIOR_SELF_SOOTHE] = (1000 - buddy.social) / 2 + (1000 - buddy.security);
  score[BEHAVIOR_WARM_UP] = latestClimateValid && latestTempC < 19.0f ? (int32_t)((19.0f - latestTempC) * 160) : 0;
  score[BEHAVIOR_COOL_DOWN] = latestClimateValid && latestTempC > 29.0f ? (int32_t)((latestTempC - 29.0f) * 160) : 0;
  uint8_t shelter = buddyHouseShelter();
  if (shelter && (hour >= 17 || hour < 8)) {
    score[BEHAVIOR_REST] += shelter + (1000 - buddy.security) / 6;
    score[BEHAVIOR_SELF_SOOTHE] += shelter / 2;
  }
  uint8_t pursuit = buddyDailyPursuit(epoch);
  if (pursuit < OBJECT_COUNT && hour >= 7 && hour < 22 && buddy.energy > 300)
    score[pursuitBehavior(pursuit)] += buddyActiveProject(epoch).object < OBJECT_COUNT ? 420 : 210;
  if (!ambitionLocked && buddyAmbition.lifeSeed == buddy.seed &&
      buddyAmbition.birthEpoch == buddy.birthEpoch && buddyAmbition.kind < OBJECT_COUNT &&
      hour >= 7 && hour < 22 && buddy.energy > 400 && buddy.health > 400)
    score[pursuitBehavior(buddyAmbition.kind)] += 110;
  if (!legacyLocked && buddyLegacy.magic == BUDDY_LEGACY_MAGIC &&
      hour >= 7 && hour < 22 && buddy.energy > 300 && buddy.health > 350) {
    uint8_t oldWork = buddyLegacyInvitation(buddy, buddyLegacy, epoch);
    if (oldWork < OBJECT_COUNT) score[pursuitBehavior(oldWork)] += 190;
  }
  if (buddy.energy >= 300 && buddy.health >= 350 && buddy.comfort >= 300) {
    uint8_t routineStrength = 0;
    uint8_t routine = buddyRoutineObject(epoch, &routineStrength);
    if (routine < OBJECT_COUNT) score[pursuitBehavior(routine)] += routineStrength * 35;
    if (buddyFamilyTraditionWindow(epoch))
      score[pursuitBehavior(buddy.inheritedObject)] += 260;
  }
  for (uint8_t i = 0; i < BEHAVIOR_COUNT; i++) {
    score[i] += buddy.behaviorLearning[i] * 4 + memoryBehaviorBias((BuddyBehavior)i, epoch) +
      buddyInnerBehaviorBias(buddy, inner, (BuddyBehavior)i) + randomRange(0, 160);
  }
  // Sleep is entry zero and must participate in the maximum search.
  BuddyBehavior choice = BEHAVIOR_SLEEP;
  for (uint8_t i = 1; i < BEHAVIOR_COUNT; i++) {
    if (score[i] > score[choice]) choice = (BuddyBehavior)i;
  }
  return choice;
}

static void applyClimateHour(uint32_t epoch) {
  if (!latestClimateValid) return;
  const char *feeling = temperatureFeeling(latestTempC);
  if (strcmp(feeling, "COZY") == 0) {
    buddy.comfort = clamp1000(buddy.comfort + 35);
    buddy.comfortableHours++;
  } else if (strstr(feeling, "COLD") || strcmp(feeling, "COOL") == 0) {
    int loss = strcmp(feeling, "COOL") == 0 ? 10 : (strstr(feeling, "VERY") ? 65 : 30);
    if (buddyHouseShelter() && (buddy.currentBehavior == BEHAVIOR_REST ||
        buddy.currentBehavior == BEHAVIOR_SLEEP)) loss -= min(loss / 3, (int)buddyHouseShelter() / 3);
    buddy.comfort = clamp1000((int)buddy.comfort - loss);
    buddy.coldHours++;
    if (buddy.coldHours % 24 == 1) rememberEvent(MEMORY_COLD, -30, epoch);
  } else {
    int loss = strcmp(feeling, "WARM") == 0 ? 10 : (strstr(feeling, "VERY") ? 65 : 30);
    if (buddyHouseShelter() && (buddy.currentBehavior == BEHAVIOR_REST ||
        buddy.currentBehavior == BEHAVIOR_SLEEP)) loss -= min(loss / 3, (int)buddyHouseShelter() / 3);
    buddy.comfort = clamp1000((int)buddy.comfort - loss);
    buddy.hotHours++;
    if (buddy.hotHours % 24 == 1) rememberEvent(MEMORY_HOT, -30, epoch);
  }
  if (latestTempC < 5.0f || latestTempC > 40.0f) {
    buddy.health = clamp1000((int)buddy.health - (buddy.traits.resilience >= 65 ? 1 : 2));
  }
}

static void performBehaviorHour(BuddyBehavior behavior, uint32_t epoch) {
  BuddyInnerState inner = buddyInnerStateOf(buddy, epoch);
  int needBefore = buddyInnerNeed(buddy, inner.concern);
  int before = buddy.mood;
  buddy.energy = clamp1000((int)buddy.energy - 10);
  buddy.social = clamp1000((int)buddy.social - max(2, 7 - buddy.traits.independence / 15));
  buddy.play = clamp1000((int)buddy.play - 6);
  buddy.curiosityNeed = clamp1000((int)buddy.curiosityNeed - 5);
  buddy.security = clamp1000((int)buddy.security - 2);

  switch (behavior) {
    case BEHAVIOR_SLEEP:
      buddy.energy = clamp1000(buddy.energy + 95);
      buddy.health = clamp1000(buddy.health + 3);
      break;
    case BEHAVIOR_REST:
      buddy.energy = clamp1000(buddy.energy + 48);
      buddy.security = clamp1000(buddy.security + 12);
      break;
    case BEHAVIOR_PLAY:
      buddy.play = clamp1000(buddy.play + 75);
      buddy.energy = clamp1000((int)buddy.energy - 22);
      break;
    case BEHAVIOR_EXPLORE:
      buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 82);
      buddy.play = clamp1000(buddy.play + 20);
      buddy.energy = clamp1000((int)buddy.energy - 18);
      break;
    case BEHAVIOR_OBSERVE:
      buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 42);
      buddy.security = clamp1000(buddy.security + 8);
      break;
    case BEHAVIOR_DAYDREAM:
      buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 25);
      buddy.energy = clamp1000(buddy.energy + 18);
      break;
    case BEHAVIOR_SELF_SOOTHE:
      buddy.social = clamp1000(buddy.social + 22 + buddy.traits.independence / 2);
      buddy.security = clamp1000(buddy.security + 48);
      break;
    case BEHAVIOR_WARM_UP:
    case BEHAVIOR_COOL_DOWN:
      buddy.comfort = clamp1000(buddy.comfort + 55);
      buddy.energy = clamp1000((int)buddy.energy - 5);
      break;
    default: break;
  }

  uint8_t shelter = buddyHouseShelter();
  if (shelter && (behavior == BEHAVIOR_SLEEP || behavior == BEHAVIOR_REST ||
      behavior == BEHAVIOR_SELF_SOOTHE || behavior == BEHAVIOR_WARM_UP ||
      behavior == BEHAVIOR_COOL_DOWN)) {
    buddy.comfort = clamp1000(buddy.comfort + shelter / 5);
    buddy.security = clamp1000(buddy.security + shelter / 8);
  }

  // An attachment is a real coping resource, not just a score bonus. Which
  // activity helps depends on the possession this individual came to love.
  if (inner.concern != CONCERN_NONE && buddyInnerAnchoredBy(buddy, behavior) &&
      buddy.affection[buddy.favoriteObject] >= 100) {
    uint16_t attachment = buddy.affection[buddy.favoriteObject];
    buddy.social = clamp1000(buddy.social + 5 + attachment / 40);
    buddy.security = clamp1000(buddy.security + 5 + attachment / 50);
  }
  // Stories and private reflection can ease solitude without requiring a
  // human or another device, especially for a sensitive, attached Buddy.
  if (inner.concern == CONCERN_LONELY && behavior == BEHAVIOR_DAYDREAM) {
    int remembered = buddy.favoriteObject < OBJECT_COUNT ?
      buddy.affection[buddy.favoriteObject] / 45 : 0;
    buddy.social = clamp1000(buddy.social + 6 + buddy.traits.sensitivity / 6 + remembered);
  }
  if (inner.concern == CONCERN_GRIEVING && behavior == BEHAVIOR_DAYDREAM)
    buddy.security = clamp1000(buddy.security + 6 + buddy.traits.sensitivity / 4 +
      (buddy.prior.generation ? 5 : 0));

  int satisfaction = (buddy.energy + buddy.health + buddy.social + buddy.play +
                      buddy.curiosityNeed + buddy.security + buddy.comfort) / 7;
  int temperament = 500 + buddy.traits.resilience * 2 + buddy.traits.playfulness;
  int activityLift = behavior == BEHAVIOR_PLAY ? 65 : behavior == BEHAVIOR_EXPLORE ? 35 :
    behavior == BEHAVIOR_DAYDREAM ? 15 : behavior == BEHAVIOR_SELF_SOOTHE ? 25 :
    behavior == BEHAVIOR_WARM_UP || behavior == BEHAVIOR_COOL_DOWN ? -20 : 0;
  int griefWeight = inner.concern == CONCERN_GRIEVING ? inner.pressure / 8 : 0;
  int targetMood = constrain((satisfaction * 2 + temperament) / 3 + activityLift - griefWeight +
    randomRange(-35, 35), 0, 1000);
  buddy.mood = clamp1000((buddy.mood * 3 + targetMood) / 4);
  int reward = buddyInnerLearningDelta(inner, needBefore, buddyInnerNeed(buddy, inner.concern),
                                       before, buddy.mood);
  buddy.behaviorLearning[behavior] = constrain(buddy.behaviorLearning[behavior] + reward, -50, 50);
  buddy.behaviorCounts[behavior]++;
  buddy.currentBehavior = behavior;
}

void endBuddyLife(uint32_t deathEpoch) {
  buddy.alive = false;
  buddy.deathEpoch = deathEpoch;
  buddy.currentStage = STAGE_GONE;
  buddy.currentBehavior = BEHAVIOR_REST;
  buddy.prior.generation = buddy.generation;
  buddy.prior.livedDays = buddyLifespanDays(buddy.seed);
  buddy.prior.finalBond = buddy.bond;
  buddy.prior.traits = buddy.traits;
  buddy.prior.deathEpoch = deathEpoch;
  uint8_t dominant = 0;
  for (uint8_t i = 1; i < BEHAVIOR_COUNT; i++) {
    if (buddy.behaviorCounts[i] > buddy.behaviorCounts[dominant]) dominant = i;
  }
  buddy.prior.dominantBehavior = dominant;
  rememberEvent(MEMORY_DEATH, 100, deathEpoch);
  queueMemorial();
  archiveLife();
  saveBuddy();
  cleanPartialRequested = true;
  needsRender = true;
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  Serial.printf("LIFE_TEST death gen=%u seed=%08lx days=%u sim_epoch=%lu real_epoch=%lu\n",
    buddy.generation, (unsigned long)buddy.seed, buddy.prior.livedDays,
    (unsigned long)deathEpoch, (unsigned long)clockLastEpoch);
#endif
}

void updateBuddySimulation(uint32_t now) {
  if (buddyCompletionPending && !recoverBuddyCompletion()) return;
  if (!now || clockFault || storageLocked || buddy.magic != BUDDY_MAGIC) return;
  if (!buddy.alive) {
    advanceBuddyHouseTo(now);
    advanceBuddyLegacyTo(now);
#ifdef BUDDY_ACCELERATED_LIFE_TEST
    if (buddyTestMemorialReady(clockLastEpoch, buddy.deathEpoch) &&
        now > buddy.deathEpoch + 86400UL) {
      if (!buddy.memorialShown) { buddy.memorialShown = true; saveBuddy(); }
      createBuddyLife(now, true);
    }
#else
    if (buddy.memorialShown && now > buddy.deathEpoch + 86400UL) createBuddyLife(now, true);
#endif
    return;
  }
  if (now < buddy.lastUpdateEpoch) {
    return;
  }
  uint32_t oldAge = buddyAgeDays(buddy.lastUpdateEpoch);
  uint8_t oldStage = buddy.currentStage;
  uint32_t age = buddyAgeDays(now);
  uint32_t lifespan = buddyLifespanDays(buddy.seed);
  uint32_t death = buddy.birthEpoch + lifespan * 86400UL;
  // Older fixed-lifespan saves can already be past the new seeded deadline.
  // Reconcile before subtraction, and never invent future simulation hours.
  if (buddy.lastUpdateEpoch >= death) {
    endBuddyLife(death);
    return;
  }
  uint32_t end = min(now, death);
  uint32_t elapsedHours = (end - buddy.lastUpdateEpoch) / 3600UL;
  if (!elapsedHours) {
    if (now >= death) endBuddyLife(death);
    return;
  }
  elapsedHours = min<uint32_t>(elapsedHours, 24UL * lifespan);
  uint32_t cursor = buddy.lastUpdateEpoch;
  for (uint32_t i = 0; i < elapsedHours; i++) {
    BuddyState beforeLife = buddy;
    BuddyAmbitionState beforeAmbition = buddyAmbition;
    BuddyLegacyState beforeLegacy = buddyLegacy;
    BuddyHouseState beforeHouse = buddyHouse;
    buddySimulationStepActive = buddyCompletionPreparing = true;
    buddySimulationStepCompleted = false;
    cursor += 3600UL;
    uint8_t estimatedHour = (cursor / 3600UL) % 24;
    // Current sensor readings describe the present, not a long offline absence.
#ifdef BUDDY_ACCELERATED_LIFE_TEST
    bool climateKnown = latestClimateValid; // 10 real seconds of recent climate
#else
    bool climateKnown = latestClimateValid && now - cursor < 3600UL;
#endif
    bool climateValid = latestClimateValid;
    latestClimateValid = climateKnown;
    if (climateKnown) applyClimateHour(cursor);
    buddy.currentStage = stageForAge(buddyAgeDays(cursor));
    advanceBuddyLegacyTo(cursor);
    performBehaviorHour(chooseBehavior(estimatedHour, cursor), cursor);
    liveWorldHour(cursor, (BuddyBehavior)buddy.currentBehavior, climateKnown);
    advanceBuddyHouseTo(cursor);
    buddy.lastUpdateEpoch = cursor;
    latestClimateValid = climateValid;
    buddyCompletionPreparing = false;
    // Side records must never reach disk ahead of the life cursor/rewards
    // that produced them. Use the same whole-hour afterimage for ordinary
    // progress, care, wear and building as for a completed ambition.
    bool sideChanged = memcmp(&beforeAmbition, &buddyAmbition, sizeof(buddyAmbition)) ||
      memcmp(&beforeLegacy, &buddyLegacy, sizeof(buddyLegacy)) ||
      memcmp(&beforeHouse, &buddyHouse, sizeof(buddyHouse));
    if (buddySimulationStepCompleted || sideChanged) {
      bool committed = persistBuddyCompletion();
      buddySimulationStepActive = false;
      if (!committed) {
        buddy = beforeLife; buddyAmbition = beforeAmbition;
        buddyLegacy = beforeLegacy; buddyHouse = beforeHouse;
        buddySpecialPending = false; ambitionSaveFailed = true;
        return;
      }
      if (!recoverBuddyCompletion()) return;
    } else {
      buddySimulationStepActive = false;
    }
  }
  if (now >= death) { endBuddyLife(death); return; }
  BuddyStage stage = stageForAge(age);
  if (stage != oldStage) {
    buddy.currentStage = stage;
    rememberEvent(MEMORY_STAGE, 70, now);
    rememberWorldEvent(now, EVENT_GREW, OBJECT_COUNT, BEHAVIOR_OBSERVE, 35);
    buddySpecialPending = true;
    buddySpecialEpoch = now;
  }
  if (age != oldAge) appendDailyJournal(now);
  saveBuddy();
}

void interactWithBuddy(uint32_t now) {
  if (buddyCompletionPending) return;
  if (!now || clockFault || storageLocked) return;
  if (!buddy.alive) {
    buddy.memorialShown = true;
    saveBuddy();
    return;
  }
  buddy.totalInteractions++;
  if (now - buddy.lastAttentionEpoch >= 3600UL) {
    buddy.development[1] += 6;
    rememberWorldEvent(now, EVENT_ATTENTION, OBJECT_COUNT, (BuddyBehavior)buddy.currentBehavior, 30);
  }
  buddy.lastAttentionEpoch = now;
  int socialGain = 35 + buddy.traits.sociability / 2;
  buddy.social = clamp1000(buddy.social + socialGain);
  buddy.security = clamp1000(buddy.security + 45);
  buddy.play = clamp1000(buddy.play + 25 + buddy.traits.playfulness / 3);
  buddy.bond = clamp1000(buddy.bond + 3 + buddy.traits.sociability / 25);
  buddy.mood = clamp1000(buddy.mood + 30);
  rememberEvent(MEMORY_INTERACTION, 45, now);
  saveBuddy();
}

const char *moodName() {
  if (!buddy.alive) return "AT PEACE";
  if (buddy.health < 350) return "UNWELL";
  if (buddy.comfort < 300) return latestTempC < 20 ? "CHILLY" : "OVERHEATED";
  if (buddy.mood >= 850) return "DELIGHTED";
  if (buddy.mood >= 680) return "HAPPY";
  if (buddy.mood >= 480) return "CONTENT";
  if (buddy.mood >= 300) return "PENSIVE";
  return "LOW";
}

const char *dominantTraitName() {
  const uint8_t *traits = (const uint8_t *)&buddy.traits;
  static const char *names[] = {"CURIOUS", "SOCIAL", "PLAYFUL", "RESILIENT", "SENSITIVE", "INDEPENDENT"};
  uint8_t best = 0;
  for (uint8_t i = 1; i < 6; i++) if (traits[i] > traits[best]) best = i;
  return names[best];
}
