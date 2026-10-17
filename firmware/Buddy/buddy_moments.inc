static uint32_t momentHash(uint32_t value) {
  value ^= value >> 16; value *= 0x7FEB352DUL;
  value ^= value >> 15; value *= 0x846CA68BUL;
  return value ^ (value >> 16);
}

static BuddyBehavior behaviorForMoment(BuddyMoment moment) {
  switch (moment) {
    case MOMENT_TOY: case MOMENT_DANCE: case MOMENT_MUSIC: return BEHAVIOR_PLAY;
    case MOMENT_MEND: case MOMENT_EXERCISE: case MOMENT_TRICK: case MOMENT_BALANCE:
    case MOMENT_CELEBRATE: return BEHAVIOR_PLAY;
    case MOMENT_WANDER: case MOMENT_BUILD: case MOMENT_PATROL: case MOMENT_FORT:
    case MOMENT_MAP: case MOMENT_COLLECT: case MOMENT_DECORATE: case MOMENT_NATURE: return BEHAVIOR_EXPLORE;
    case MOMENT_HUM: case MOMENT_DAYDREAM: case MOMENT_MEDITATE: case MOMENT_STARGAZE:
    case MOMENT_SING: case MOMENT_STORY: case MOMENT_JOURNAL: return BEHAVIOR_DAYDREAM;
    case MOMENT_NOOK: case MOMENT_GROOM: case MOMENT_STRETCH: case MOMENT_TIDY:
    case MOMENT_SORT: case MOMENT_REPAIR_NOOK: return BEHAVIOR_REST;
    case MOMENT_VISIT: case MOMENT_POLISH: case MOMENT_SCULPT: return BEHAVIOR_SELF_SOOTHE;
    case MOMENT_WATER: case MOMENT_STUDY: case MOMENT_GARDEN: case MOMENT_SKETCH:
    case MOMENT_SMELL_FLOWERS:
      return BEHAVIOR_OBSERVE;
    case MOMENT_NAP: return BEHAVIOR_SLEEP;
    case MOMENT_WARM: return BEHAVIOR_WARM_UP;
    case MOMENT_COOL: return BEHAVIOR_COOL_DOWN;
    default: return BEHAVIOR_OBSERVE;
  }
}

static uint8_t objectForMoment(BuddyMoment moment) {
  switch (moment) {
    case MOMENT_STONE: case MOMENT_POLISH: case MOMENT_SCULPT: case MOMENT_SKETCH:
    case MOMENT_MAP: case MOMENT_COLLECT: return OBJECT_STONE;
    case MOMENT_PLANT: case MOMENT_WATER: case MOMENT_GARDEN: return OBJECT_PLANT;
    case MOMENT_NOOK: case MOMENT_TIDY: case MOMENT_FORT: case MOMENT_MEDITATE:
    case MOMENT_STORY: case MOMENT_DECORATE: case MOMENT_REPAIR_NOOK: return OBJECT_NOOK;
    case MOMENT_TOY: case MOMENT_MEND: case MOMENT_BUILD: case MOMENT_TRICK:
    case MOMENT_SORT: return OBJECT_TOY;
    case MOMENT_NATURE: return OBJECT_GROVE;
    case MOMENT_SMELL_FLOWERS: return OBJECT_FLOWERS;
    case MOMENT_JOURNAL: return OBJECT_JOURNAL;
    case MOMENT_MUSIC: return OBJECT_INSTRUMENT;
    default: return OBJECT_COUNT;
  }
}

static bool momentAvailable(BuddyMoment moment, uint32_t now) {
  switch (moment) {
    case MOMENT_TOY: return buddy.possessions & (1 << OBJECT_TOY);
    case MOMENT_PLANT: return buddy.possessions & (1 << OBJECT_PLANT);
    case MOMENT_STONE: return buddy.possessions & (1 << OBJECT_STONE);
    case MOMENT_NOOK: return buddy.possessions & (1 << OBJECT_NOOK);
    case MOMENT_VISIT: return buddy.lastAttentionEpoch && now >= buddy.lastAttentionEpoch &&
      now - buddy.lastAttentionEpoch < 20UL * 60UL;
    case MOMENT_NAP: return buddy.energy < 620 || ((now / 3600UL) % 24 >= 22) || ((now / 3600UL) % 24 < 7);
    case MOMENT_WARM: return latestClimateValid && latestTempC < 19.0f;
    case MOMENT_COOL: return latestClimateValid && latestTempC > 29.0f;
    case MOMENT_WATER: return buddy.possessions & (1 << OBJECT_PLANT);
    case MOMENT_POLISH: return buddy.possessions & (1 << OBJECT_STONE);
    case MOMENT_MEND: return buddy.possessions & (1 << OBJECT_TOY);
    case MOMENT_TIDY: return buddy.possessions & (1 << OBJECT_NOOK);
    case MOMENT_BUILD: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage < STAGE_GONE &&
      (buddy.possessions & (1 << OBJECT_TOY)) && (buddy.possessions & (1 << OBJECT_STONE));
    case MOMENT_EXERCISE: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage != STAGE_ELDER &&
      buddy.energy > 380 && buddy.health > 300;
    case MOMENT_STUDY: return buddy.currentStage >= STAGE_YOUNG && buddy.currentStage < STAGE_GONE &&
      buddy.possessions != 0;
    case MOMENT_PATROL: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage < STAGE_GONE &&
      buddy.energy > 300;
    case MOMENT_GARDEN: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage < STAGE_GONE &&
      (buddy.possessions & (1 << OBJECT_PLANT));
    case MOMENT_SCULPT: return buddy.currentStage >= STAGE_YOUNG && buddy.currentStage < STAGE_GONE &&
      (buddy.possessions & (1 << OBJECT_STONE));
    case MOMENT_TRICK: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage < STAGE_GONE &&
      (buddy.possessions & (1 << OBJECT_TOY)) && buddy.energy > 350;
    case MOMENT_FORT: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage < STAGE_GONE &&
      (buddy.possessions & (1 << OBJECT_NOOK)) && (buddy.possessions & (1 << OBJECT_TOY));
    case MOMENT_SORT: return __builtin_popcount((unsigned)buddy.possessions) >= 2;
    case MOMENT_MEDITATE: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage < STAGE_GONE;
    case MOMENT_STARGAZE: { uint8_t hour=(now/3600UL)%24; return hour >= 20 || hour < 6; }
    case MOMENT_SING: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage < STAGE_GONE;
    case MOMENT_SKETCH: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage < STAGE_GONE &&
      (buddy.possessions & (1 << OBJECT_STONE));
    case MOMENT_MAP: return buddy.currentStage >= STAGE_YOUNG && buddy.currentStage < STAGE_GONE;
    case MOMENT_COLLECT: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage < STAGE_GONE;
    case MOMENT_STORY: return buddy.currentStage >= STAGE_YOUNG && buddy.currentStage < STAGE_GONE && buddy.eventCount >= 3;
    case MOMENT_DECORATE: return buddy.currentStage >= STAGE_YOUNG && buddy.currentStage < STAGE_GONE &&
      (buddy.possessions & (1 << OBJECT_NOOK)) && __builtin_popcount((unsigned)buddy.possessions) >= 2;
    case MOMENT_REPAIR_NOOK: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage < STAGE_GONE &&
      (buddy.possessions & (1 << OBJECT_NOOK));
    case MOMENT_BALANCE: return buddy.currentStage >= STAGE_CHILD && buddy.currentStage != STAGE_ELDER &&
      buddy.energy > 420;
    case MOMENT_CELEBRATE: {
      uint32_t finished = buddyRecentProjectCompletion(now);
      bool ambitionFinished = !ambitionLocked && buddyAmbition.lifeSeed == buddy.seed &&
        buddyAmbition.birthEpoch == buddy.birthEpoch &&
        buddyAmbition.lastOutcome == AMBITION_FULFILLED && buddyAmbition.lastEndedEpoch &&
        now >= buddyAmbition.lastEndedEpoch && now - buddyAmbition.lastEndedEpoch < 3UL * 3600UL;
      return (finished && now - finished < 2UL * 3600UL) ||
        ambitionFinished || (buddy.mood > 720 && buddy.possessions == (1 << OBJECT_COUNT)-1);
    }
    case MOMENT_NATURE: return buddy.possessions & (1 << OBJECT_GROVE);
    case MOMENT_SMELL_FLOWERS: return buddy.possessions & (1 << OBJECT_FLOWERS);
    case MOMENT_JOURNAL: return buddy.currentStage >= STAGE_CHILD &&
      (buddy.possessions & (1 << OBJECT_JOURNAL));
    case MOMENT_MUSIC: return buddy.possessions & (1 << OBJECT_INSTRUMENT);
    default: return true;
  }
}

static int momentScore(BuddyMoment moment, uint32_t now, uint32_t bucket) {
  if (!momentAvailable(moment, now)) return -10000;
  int score = 80 + (momentHash(buddy.seed ^ bucket * 0x9E3779B9UL ^ moment * 0x85EBCA6BUL) & 0x1FF);
  BuddyInnerState inner = buddyInnerStateOf(buddy, now);
  switch (moment) {
    case MOMENT_TOY: case MOMENT_DANCE:
      score += buddy.traits.playfulness * 2 + (1000 - buddy.play) / 5; break;
    case MOMENT_WANDER:
      score += buddy.traits.curiosity * 2 + (1000 - buddy.curiosityNeed) / 5; break;
    case MOMENT_PLANT: case MOMENT_STONE: case MOMENT_WATCH: case MOMENT_LISTEN:
      score += buddy.traits.curiosity + buddy.traits.sensitivity; break;
    case MOMENT_HUM: case MOMENT_DAYDREAM:
      score += buddy.traits.sensitivity * 2; break;
    case MOMENT_NOOK:
      score += buddy.traits.independence + (1000 - buddy.security) / 4; break;
    case MOMENT_VISIT:
      score += 500 + buddy.traits.sociability * 2; break;
    case MOMENT_NAP:
      score += (1000 - buddy.energy) / 2; break;
    case MOMENT_WARM: case MOMENT_COOL:
      score += 450; break;
    case MOMENT_WATER:
      score += 180 + buddy.traits.sensitivity * 3 + (100 - buddy.plantGrowth); break;
    case MOMENT_POLISH:
      score += 160 + buddy.traits.independence * 2 + buddy.traits.sensitivity; break;
    case MOMENT_MEND:
      score += 170 + buddy.traits.resilience * 2 + buddy.traits.playfulness; break;
    case MOMENT_TIDY:
      score += 150 + buddy.traits.independence * 2 + (1000 - buddy.security) / 5; break;
    case MOMENT_BUILD:
      score += 190 + buddy.traits.curiosity * 2 + buddy.traits.playfulness * 2; break;
    case MOMENT_EXERCISE:
      score += 130 + buddy.traits.resilience * 3 + buddy.traits.playfulness; break;
    case MOMENT_STUDY:
      score += 175 + buddy.traits.curiosity * 3 + buddy.traits.sensitivity; break;
    case MOMENT_PATROL:
      score += 145 + buddy.traits.curiosity * 2 + buddy.traits.independence * 2; break;
    case MOMENT_GARDEN:
      score += 175 + buddy.traits.sensitivity * 3 + (100-buddy.plantGrowth); break;
    case MOMENT_SCULPT:
      score += 155 + buddy.traits.sensitivity * 2 + buddy.traits.independence * 2; break;
    case MOMENT_TRICK:
      score += 185 + buddy.traits.playfulness * 4 + buddy.behaviorLearning[BEHAVIOR_PLAY]*2; break;
    case MOMENT_FORT:
      score += 180 + buddy.traits.resilience * 2 + buddy.traits.playfulness * 2; break;
    case MOMENT_SORT:
      score += 135 + buddy.traits.independence * 3 + (1000-buddy.security)/6; break;
    case MOMENT_MEDITATE:
      score += 150 + buddy.traits.sensitivity * 3 + (1000-buddy.security)/5; break;
    case MOMENT_STARGAZE:
      score += 210 + buddy.traits.curiosity * 2 + buddy.traits.sensitivity * 2; break;
    case MOMENT_SING:
      score += 145 + buddy.traits.sociability * 2 + buddy.traits.sensitivity * 2; break;
    case MOMENT_SKETCH:
      score += 165 + buddy.traits.curiosity * 2 + buddy.traits.sensitivity * 2; break;
    case MOMENT_MAP:
      score += 180 + buddy.traits.curiosity * 4; break;
    case MOMENT_COLLECT:
      score += 170 + buddy.traits.curiosity * 3 + buddy.traits.independence; break;
    case MOMENT_STORY:
      score += 150 + buddy.traits.sociability * 3 + buddy.traits.sensitivity; break;
    case MOMENT_DECORATE:
      score += 150 + buddy.traits.sensitivity * 2 + buddy.traits.playfulness * 2; break;
    case MOMENT_REPAIR_NOOK:
      score += 160 + buddy.traits.resilience * 3 + (1000-buddy.security)/6; break;
    case MOMENT_BALANCE:
      score += 140 + buddy.traits.resilience * 2 + buddy.traits.playfulness * 2; break;
    case MOMENT_CELEBRATE:
      score += 260 + buddy.traits.playfulness * 3 + buddy.traits.sociability * 2; break;
    case MOMENT_NATURE:
      score += 215 + buddy.traits.curiosity * 3 + (1000 - buddy.curiosityNeed) / 5; break;
    case MOMENT_SMELL_FLOWERS:
      score += 220 + buddy.traits.sensitivity * 3 + (1000 - buddy.comfort) / 5; break;
    case MOMENT_JOURNAL:
      score += 175 + buddy.traits.sensitivity * 2 + buddy.traits.independence * 2; break;
    case MOMENT_MUSIC:
      score += 190 + buddy.traits.playfulness * 2 + buddy.traits.sociability * 2; break;
    default: break;
  }
  uint8_t object = objectForMoment(moment);
  score += buddyInnerMomentBias(inner, moment);
  if (inner.concern != CONCERN_NONE && object < OBJECT_COUNT &&
      object == buddy.favoriteObject && (buddy.possessions & (1 << object)))
    score += buddy.affection[object] / 3;
  BuddyProject project = buddyActiveProject(now);
  // Unstructured moments should remain possible even with every possession
  // discovered; this is a baseline, not a phantom active-project reward.
  if (object == OBJECT_COUNT) score += 140;
  if (project.object < OBJECT_COUNT && object == project.object) score += 180;
  if (!ambitionLocked && buddyAmbition.lifeSeed == buddy.seed &&
      buddyAmbition.birthEpoch == buddy.birthEpoch && buddyAmbition.kind == object &&
      buddy.energy > 380 && buddy.health > 350) score += 160;
  uint8_t routineStrength = 0;
  uint8_t routine = buddyRoutineObject(now, &routineStrength);
  if (object < OBJECT_COUNT && object == routine) score += routineStrength * 45;
  if (object == buddy.inheritedObject && buddyFamilyTraditionWindow(now)) score += 240;
  if (object == buddy.inheritedObject && buddyFamilyTraditionRecent(now)) score += 350;
  if (object < OBJECT_COUNT && !legacyLocked && buddyLegacy.magic == BUDDY_LEGACY_MAGIC &&
      object == buddyLegacyInvitation(buddy, buddyLegacy, now)) score += 170;
  if (!houseLocked && buddyHouse.phase == HOUSE_OCCUPIED &&
      (moment == MOMENT_NOOK || moment == MOMENT_TIDY || moment == MOMENT_DECORATE ||
       moment == MOMENT_REPAIR_NOOK)) {
    score += buddyHouse.condition < 350 && moment == MOMENT_REPAIR_NOOK ? 190 :
      buddyHouse.condition >= 350 ? 80 : 0;
  }
  uint32_t finished = buddyRecentProjectCompletion(now);
  if (moment == MOMENT_CELEBRATE && finished && now >= finished && now - finished < 2UL * 3600UL)
    score += 850;
  if (moment == MOMENT_CELEBRATE && !ambitionLocked &&
      buddyAmbition.lifeSeed == buddy.seed && buddyAmbition.birthEpoch == buddy.birthEpoch &&
      buddyAmbition.lastOutcome == AMBITION_FULFILLED && buddyAmbition.lastEndedEpoch &&
      now >= buddyAmbition.lastEndedEpoch && now - buddyAmbition.lastEndedEpoch < 3UL * 3600UL)
    score += 1000;
  if (object < OBJECT_COUNT && (buddy.possessions & (1 << object))) {
    if (object == buddyDailyPursuit(now)) score += 180;
    score += buddy.affection[object] / 8;
    if (buddy.eventCount) {
      const WorldMemory &recent = buddy.events[(buddy.eventHead + 23) % 24];
      if (recent.object == object && recent.epoch <= now && now-recent.epoch < 3UL*3600UL)
        score += 260;
    }
  }
  // The displayed activity should usually belong to Buddy's current hourly
  // behavior, while independent evaluations change the pose and prop.
  if (behaviorForMoment(moment) == buddy.currentBehavior) score += 230;
  if (moment == visibleMoment) score -= 300;
  return score;
}

// Cable Friends draws from the same availability and personality scoring as
// solo life. Every available moment has nonzero lottery weight, so a favorite
// can be common without making the rest of Buddy's repertoire unreachable.
static BuddyMoment chooseCableMoment(uint32_t now, uint32_t randomValue,
                                     BuddyMoment previous) {
  uint32_t weights[MOMENT_COUNT] = {};
  uint32_t total = 0;
  for (uint8_t i = 0; i < MOMENT_COUNT; ++i) {
    BuddyMoment candidate = (BuddyMoment)i;
    if (!momentAvailable(candidate, now)) continue;
    int score = momentScore(candidate, now, now / 60UL);
    uint32_t weight = score > 0 ? (uint32_t)(score / 100 + 1) : 1;
    if (candidate == previous) weight = max<uint32_t>(1, weight / 4);
    weights[i] = weight;
    total += weight;
  }
  if (!total) return MOMENT_WATCH;
  uint32_t ticket = randomValue % total;
  for (uint8_t i = 0; i < MOMENT_COUNT; ++i) {
    if (ticket < weights[i]) return (BuddyMoment)i;
    ticket -= weights[i];
  }
  return MOMENT_WATCH;
}

static uint8_t cableActivityForMoment(BuddyMoment moment) {
  if (moment == MOMENT_GARDEN || moment == MOMENT_PLANT || moment == MOMENT_WATER ||
      moment == MOMENT_NATURE || moment == MOMENT_SMELL_FLOWERS) return NEAR_GARDEN;
  if (moment == MOMENT_BUILD || moment == MOMENT_FORT || moment == MOMENT_MEND ||
      moment == MOMENT_SCULPT || moment == MOMENT_DECORATE ||
      moment == MOMENT_REPAIR_NOOK) return NEAR_BUILD;
  if (moment == MOMENT_STORY || moment == MOMENT_JOURNAL || moment == MOMENT_SING ||
      moment == MOMENT_MUSIC || moment == MOMENT_HUM) return NEAR_STORY;
  BuddyBehavior behavior = behaviorForMoment(moment);
  if (behavior == BEHAVIOR_PLAY) return NEAR_PLAY;
  if (behavior == BEHAVIOR_EXPLORE) return NEAR_EXPLORE;
  if (behavior == BEHAVIOR_SLEEP || behavior == BEHAVIOR_REST ||
      behavior == BEHAVIOR_SELF_SOOTHE) return NEAR_REST;
  return NEAR_WATCH;
}

void updateBuddyMoment(uint32_t now, bool force, bool allowClockFaultForDisplay) {
  if (!buddy.alive || !now || (clockFault && !allowClockFaultForDisplay)) return;
  uint32_t bucket = now / 60UL;
  if (!force && bucket == visibleMomentBucket) return;
  BuddyMoment best = MOMENT_WATCH;
  int bestScore = -10000;
  for (uint8_t i = 0; i < MOMENT_COUNT; i++) {
    int score = momentScore((BuddyMoment)i, now, bucket);
    if (score > bestScore) { bestScore = score; best = (BuddyMoment)i; }
  }
  visibleMoment = best;
  visibleBehavior = behaviorForMoment(best);
  visibleMomentBucket = bucket;
  uint8_t object = objectForMoment(best);
  uint8_t routineStrength = 0;
  uint8_t routine = buddyRoutineObject(now, &routineStrength);
  uint16_t affection = object < OBJECT_COUNT && (buddy.possessions & (1 << object)) ?
    buddy.affection[object] : 0;
  uint32_t duration = buddyMomentDurationMs(best, (BuddyStage)buddy.currentStage,
    buddy.energy, buddy.comfort, buddy.traits, affection,
    object == routine ? routineStrength : 0, esp_random());
  nextMomentMs = millis() + duration;
  momentScheduled = true;
}

const char *buddyMomentText(BuddyMoment requested = MOMENT_COUNT) {
  BuddyMoment moment = requested == MOMENT_COUNT ? visibleMoment : requested;
  if (requested == MOMENT_COUNT) {
  uint32_t now = buddyNow();
  if (!ambitionLocked && buddyAmbition.lifeSeed == buddy.seed &&
      buddyAmbition.birthEpoch == buddy.birthEpoch && buddyAmbition.lastEndedEpoch &&
      now >= buddyAmbition.lastEndedEpoch && now - buddyAmbition.lastEndedEpoch < 3UL * 3600UL) {
    if (buddyAmbition.lastOutcome == AMBITION_FULFILLED && visibleMoment == MOMENT_CELEBRATE)
      return "A LONG DREAM CAME TRUE";
    if (buddyAmbition.lastOutcome == AMBITION_ABANDONED &&
        objectForMoment(visibleMoment) == buddyAmbition.lastKind)
      return "CHOOSING A NEW PATH";
  }
  if (buddyFamilyTraditionRecent(now) && objectForMoment(visibleMoment) == buddy.inheritedObject)
    return "KEEPING A FAMILY CUSTOM";
  BuddyProject project = buddyActiveProject(now);
  if (project.object < OBJECT_COUNT && objectForMoment(visibleMoment) == project.object) {
    static const char *projectWords[] = {
      "SHAPING THE SHINY STONE", "TENDING THE LITTLE GARDEN",
      "FIXING UP THE QUIET NOOK", "WORKING ON THE HOMEMADE TOY",
      "EXPLORING THE WILD GROVE", "TENDING THE FLOWER PATCH",
      "WRITING A LIFE STORY", "PRACTICING LITTLE TUNES"
    };
    return projectWords[project.object];
  }
  if (!ambitionLocked && buddyAmbition.lifeSeed == buddy.seed &&
      buddyAmbition.birthEpoch == buddy.birthEpoch &&
      buddyAmbition.kind < OBJECT_COUNT && objectForMoment(visibleMoment) == buddyAmbition.kind &&
      buddy.energy > 380 && buddy.health > 350) {
    static const char *dreamWords[] = {
      "MAPPING A BIGGER WORLD", "GROWING A LITTLE GARDEN",
      "MAKING A WELCOMING HOME", "MASTERING A NEW TRICK",
      "KNOWING THE WILD GROVE", "GROWING WILD FLOWERS",
      "WRITING A LONG STORY", "MASTERING A NEW SONG"
    };
    return dreamWords[buddyAmbition.kind];
  }
  uint8_t legacyObject = objectForMoment(visibleMoment);
  if (!legacyLocked && buddyLegacy.magic == BUDDY_LEGACY_MAGIC &&
      legacyObject < OBJECT_COUNT &&
      legacyObject == buddyLegacyInvitation(buddy, buddyLegacy, now) &&
      buddyLegacy.lastCareDay[legacyObject] != now / 86400UL) {
    static const char *callingWords[] = {
      "RETURNING TO AN OLD MAP", "VISITING THE OLD GARDEN",
      "LOOKING AFTER OLD HOME", "REVISITING AN OLD GAME",
      "RETURNING TO AN OLD TRAIL", "VISITING OLD FLOWERS",
      "OPENING AN OLD JOURNAL", "REVISITING AN OLD SONG"
    };
    static_assert(sizeof(callingWords) / sizeof(callingWords[0]) == OBJECT_COUNT,
                  "Every inherited anchor needs a caption");
    return callingWords[legacyObject];
  }
  if (!legacyLocked && buddyLegacy.magic == BUDDY_LEGACY_MAGIC &&
      buddyLegacyUsable(buddyLegacy, legacyObject) &&
      buddyLegacy.makerGeneration[legacyObject] != buddy.generation &&
      buddyLegacy.lastCareDay[legacyObject] == now / 86400UL) {
    static const char *inheritedWords[] = {
      "FOLLOWING AN OLD MAP", "TENDING THE OLD GARDEN",
      "KEEPING HOME WARM", "PLAYING AN OLD GAME",
      "WALKING AN OLD TRAIL", "SMELLING OLD FLOWERS",
      "READING AN OLD JOURNAL", "PLAYING AN OLD SONG"
    };
    return inheritedWords[legacyObject];
  }
  BuddyInnerState inner = buddyInnerStateOf(buddy, now);
  if (inner.concern == CONCERN_GRIEVING && inner.pressure >= 300 &&
      (visibleMoment == MOMENT_WATCH || visibleMoment == MOMENT_DAYDREAM ||
       visibleMoment == MOMENT_STORY)) return "REMEMBERING THE OLD HOME";
  }
  switch (moment) {
    case MOMENT_STRETCH: return "STRETCHING";
    case MOMENT_WANDER: return "WANDERING AROUND";
    case MOMENT_HUM: return "HUMMING SOFTLY";
    case MOMENT_TOY: return "PLAYING WITH HOMEMADE TOY";
    case MOMENT_PLANT: return "CHECKING ON LITTLE PLANT";
    case MOMENT_STONE: return "ADMIRING SHINY STONE";
    case MOMENT_NOOK: return buddyHouse.phase == HOUSE_OCCUPIED && !houseLocked ?
      "RESTING AT HOME" : "CURLED UP IN QUIET NOOK";
    case MOMENT_WATCH: return "WATCHING THE ROOM";
    case MOMENT_DAYDREAM: return "LOST IN A DAYDREAM";
    case MOMENT_GROOM: return "GROOMING";
    case MOMENT_DANCE: return "DOING A LITTLE DANCE";
    case MOMENT_LISTEN: return "LISTENING CLOSELY";
    case MOMENT_VISIT: return "THINKING ABOUT YOU";
    case MOMENT_NAP: return "TAKING A QUICK NAP";
    case MOMENT_WARM: return "TRYING TO WARM UP";
    case MOMENT_COOL: return "COOLING OFF";
    case MOMENT_WATER: return "WATERING THE LITTLE PLANT";
    case MOMENT_POLISH: return "POLISHING THE SHINY STONE";
    case MOMENT_MEND: return "MENDING THE HOMEMADE TOY";
    case MOMENT_TIDY: return buddyHouse.phase == HOUSE_OCCUPIED && !houseLocked ?
      "TIDYING THE HOME" : "TIDYING THE QUIET NOOK";
    case MOMENT_BUILD: return "BUILDING A TINY CONTRAPTION";
    case MOMENT_EXERCISE: return "EXERCISING";
    case MOMENT_STUDY: return "STUDYING THE COLLECTION";
    case MOMENT_PATROL: return "PATROLLING THE LITTLE WORLD";
    case MOMENT_GARDEN: return "TENDING THE TINY GARDEN";
    case MOMENT_SCULPT: return "CARVING A LITTLE SCULPTURE";
    case MOMENT_TRICK: return "PRACTICING A NEW TOY TRICK";
    case MOMENT_FORT: return "BUILDING A FORT IN THE NOOK";
    case MOMENT_SORT: return "SORTING THE TREASURES";
    case MOMENT_MEDITATE: return "SITTING QUIETLY TO THINK";
    case MOMENT_STARGAZE: return "LOOKING UP AT THE STARS";
    case MOMENT_SING: return "PRACTICING A LITTLE SONG";
    case MOMENT_SKETCH: return "SKETCHING A NEW IDEA";
    case MOMENT_MAP: return "MAPPING THE LITTLE WORLD";
    case MOMENT_COLLECT: return "SEARCHING FOR TINY TREASURES";
    case MOMENT_STORY: return "MAKING UP A STORY";
    case MOMENT_DECORATE: return buddyHouse.phase == HOUSE_OCCUPIED && !houseLocked ?
      "MAKING HOME THEIR OWN" : "DECORATING THE QUIET NOOK";
    case MOMENT_REPAIR_NOOK: return buddyHouse.phase == HOUSE_OCCUPIED && !houseLocked ?
      "CARING FOR THE HOME" : "FIXING UP THE QUIET NOOK";
    case MOMENT_BALANCE: return "PRACTICING BALANCE";
    case MOMENT_CELEBRATE: return "CELEBRATING A GOOD DAY";
    case MOMENT_NATURE: return "ENJOYING NATURE";
    case MOMENT_SMELL_FLOWERS: return "SMELLING THE FLOWERS";
    case MOMENT_JOURNAL: return "WRITING IN THE JOURNAL";
    case MOMENT_MUSIC: return "PLAYING A LITTLE TUNE";
    default: return behaviorName((BuddyBehavior)buddy.currentBehavior);
  }
}

const char *visibleMoodName(uint32_t now) {
  if (buddy.health < 350) return "UNWELL";
  if (buddy.comfort < 300) return latestTempC < 20 ? "CHILLY" : "OVERHEATED";
  uint32_t finished = buddyRecentProjectCompletion(now);
  if (!ambitionLocked && buddyAmbition.lifeSeed == buddy.seed &&
      buddyAmbition.birthEpoch == buddy.birthEpoch && buddyAmbition.lastEndedEpoch &&
      buddyAmbition.lastOutcome == AMBITION_FULFILLED && now >= buddyAmbition.lastEndedEpoch &&
      now - buddyAmbition.lastEndedEpoch < 3UL * 3600UL) return "PROUD";
  if (visibleMoment == MOMENT_CELEBRATE && finished && now >= finished && now - finished < 2UL * 3600UL)
    return "PROUD";
  if (buddy.lastAttentionEpoch && now >= buddy.lastAttentionEpoch &&
      now - buddy.lastAttentionEpoch < 20UL * 60UL) return "DELIGHTED";
  if (buddyFamilyTraditionRecent(now) && objectForMoment(visibleMoment) == buddy.inheritedObject)
    return "ROOTED";
  BuddyInnerState inner = buddyInnerStateOf(buddy, now);
  if (inner.pressure >= 300) {
    switch (inner.concern) {
      case CONCERN_EXHAUSTED: return "TIRED";
      case CONCERN_UNEASY: return "UNEASY";
      case CONCERN_LONELY: return "LONELY";
      case CONCERN_RESTLESS: return "RESTLESS";
      case CONCERN_BORED: return "BORED";
      case CONCERN_GRIEVING: return "MISSING HOME";
      default: break;
    }
  }
  if (buddy.mood < 360) return "LOW";
  if (buddy.mood < 500) return "PENSIVE";
  switch (visibleMoment) {
    case MOMENT_TOY: case MOMENT_DANCE: case MOMENT_MEND: case MOMENT_BUILD: case MOMENT_TRICK:
    case MOMENT_FORT: case MOMENT_BALANCE: case MOMENT_CELEBRATE: return "PLAYFUL";
    case MOMENT_WANDER: case MOMENT_STRETCH: case MOMENT_EXERCISE: case MOMENT_PATROL:
    case MOMENT_MAP: case MOMENT_COLLECT: return "ENERGETIC";
    case MOMENT_PLANT: case MOMENT_STONE: case MOMENT_WATCH: case MOMENT_LISTEN: case MOMENT_STUDY:
    case MOMENT_SKETCH: return "CURIOUS";
    case MOMENT_HUM: case MOMENT_DAYDREAM: case MOMENT_STARGAZE: case MOMENT_SING:
    case MOMENT_STORY: return "DREAMY";
    case MOMENT_NOOK: case MOMENT_GROOM: case MOMENT_WATER: case MOMENT_POLISH: case MOMENT_TIDY:
    case MOMENT_GARDEN: case MOMENT_SCULPT: case MOMENT_SORT: case MOMENT_MEDITATE:
    case MOMENT_DECORATE: case MOMENT_REPAIR_NOOK: return "PURPOSEFUL";
    case MOMENT_NAP: return "SLEEPY";
    case MOMENT_WARM: return "CHILLY";
    case MOMENT_COOL: return "TOO WARM";
    default: break;
  }
  if (buddy.mood >= 820) return "CHEERFUL";
  if (buddy.mood >= 620) return "CONTENT";
  if (buddy.mood >= 420) return "QUIET";
  return "LOW";
}
