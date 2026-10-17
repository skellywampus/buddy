const char *objectName(uint8_t object) {
  static const char *names[] = {"SHINY STONE", "LITTLE PLANT", "QUIET NOOK", "HOMEMADE TOY",
    "WILD GROVE", "FLOWER PATCH", "STORY JOURNAL", "LITTLE INSTRUMENT"};
  return object < OBJECT_COUNT ? names[object] : "NOT DISCOVERED";
}

void rememberWorldEvent(uint32_t epoch, LifeEvent event, uint8_t object, BuddyBehavior behavior, int pleasure) {
  WorldMemory &memory = buddy.events[buddy.eventHead];
  memory = {epoch, (uint8_t)event, object, (uint8_t)behavior, (int8_t)constrain(pleasure, -100, 100)};
  buddy.eventHead = (buddy.eventHead + 1) % 24;
  if (buddy.eventCount < 24) buddy.eventCount++;
  buddyLegacyOnEvent(epoch, event, object);
  buddyAmbitionOnEvent(epoch, event, object);
}

void describeWorldEvent(const WorldMemory &event, char *out, size_t length) {
  const char *verb = "VISITED";
  switch (event.event) {
    case EVENT_BORN: snprintf(out, length, "A NEW LIFE BEGINS"); return;
    case EVENT_ATTENTION: snprintf(out, length, "ENJOYED YOUR COMPANY"); return;
    case EVENT_GREW: snprintf(out, length, "GREW A LITTLE WISER"); return;
    case EVENT_FOUND: verb = "FOUND"; break;
    case EVENT_TENDED: verb = "TENDED"; break;
    case EVENT_PLAYED: verb = "PLAYED WITH"; break;
    case EVENT_RESTED: verb = "RESTED IN"; break;
    case EVENT_INHERITED: snprintf(out, length, "A FAMILIAR FAMILY PREFERENCE"); return;
    case EVENT_WATERED: verb = "WATERED"; break;
    case EVENT_POLISHED: verb = "POLISHED"; break;
    case EVENT_MENDED: verb = "MENDED"; break;
    case EVENT_TIDIED: verb = "TIDIED"; break;
    case EVENT_INVENTED: snprintf(out, length, "BUILT A TINY CONTRAPTION"); return;
    case EVENT_PRACTICED: snprintf(out, length, "PRACTICED A NEW TRICK"); return;
    case EVENT_GARDENED: snprintf(out, length, "TENDED A TINY GARDEN"); return;
    case EVENT_SCULPTED: snprintf(out, length, "CARVED A LITTLE SCULPTURE"); return;
    case EVENT_BUILT_FORT: snprintf(out, length, "BUILT A FORT IN THE NOOK"); return;
    case EVENT_CHARTED: snprintf(out, length, "MAPPED A CORNER OF THE WORLD"); return;
    case EVENT_COLLECTED: snprintf(out, length, "FOUND A TINY TREASURE"); return;
    case EVENT_TOLD_STORY: snprintf(out, length, "MADE UP A STORY"); return;
    case EVENT_DECORATED: snprintf(out, length, "DECORATED THE QUIET NOOK"); return;
    case EVENT_MEDITATED: snprintf(out, length, "FOUND A QUIET MOMENT"); return;
    case EVENT_PROJECT_STARTED: snprintf(out, length, "BEGAN WORK ON %s", objectName(event.object)); return;
    case EVENT_PROJECT_WORKED: snprintf(out, length, "RETURNED TO %s", objectName(event.object)); return;
    case EVENT_PROJECT_FINISHED: snprintf(out, length, "FINISHED WORK ON %s", objectName(event.object)); return;
    case EVENT_HOUSE_STARTED: snprintf(out, length, "BEGAN BUILDING A HOME"); return;
    case EVENT_HOUSE_PROGRESS: snprintf(out, length, "WORKED ON THE HOME"); return;
    case EVENT_HOUSE_FINISHED: snprintf(out, length, "FINISHED THE HOME"); return;
    case EVENT_HOUSE_REPAIRED: snprintf(out, length, "CARED FOR THE HOME"); return;
    case EVENT_HOUSE_COLLAPSED: snprintf(out, length, "THE OLD HOME FELL"); return;
    case EVENT_HOUSE_INHERITED: snprintf(out, length, "CAME HOME TO THE FAMILY HOUSE"); return;
    case EVENT_MET_BUDDY: snprintf(out, length, "MET ANOTHER BUDDY"); return;
    case EVENT_SHARED_ACTIVITY: snprintf(out, length, "SPENT TIME WITH A FAMILIAR BUDDY"); return;
    case EVENT_FAMILY_CUSTOM: snprintf(out, length, "KEPT A FAMILY CUSTOM WITH %s", objectName(event.object)); return;
    case EVENT_AMBITION_STARTED: snprintf(out, length, "BEGAN A LONG DREAM FOR %s", objectName(event.object)); return;
    case EVENT_AMBITION_FULFILLED: snprintf(out, length, "FULFILLED A LONG DREAM FOR %s", objectName(event.object)); return;
    case EVENT_AMBITION_RELEASED: snprintf(out, length, "LET GO OF A DREAM FOR %s", objectName(event.object)); return;
    case EVENT_LEGACY_LOST: snprintf(out, length, "AN OLD WORK OF %s FADED", objectName(event.object)); return;
    case EVENT_NATURE: snprintf(out, length, "ENJOYED THE WILD GROVE"); return;
    case EVENT_FLOWERS: snprintf(out, length, "STOPPED TO SMELL FLOWERS"); return;
    case EVENT_JOURNAL: snprintf(out, length, "ADDED A PAGE TO THE JOURNAL"); return;
    case EVENT_MUSIC: snprintf(out, length, "PLAYED A LITTLE TUNE"); return;
    default: break;
  }
  snprintf(out, length, "%s %s", verb, objectName(event.object));
}

void initializeBuddyWorld(bool inherited) {
  buddy.worldVersion = 2;
  buddy.favoriteObject = OBJECT_COUNT;
  buddy.inheritedObject = OBJECT_COUNT;
  buddy.birthTraits = buddy.traits;
  buddy.lastDevelopmentDay = buddyAgeDays(buddy.lastUpdateEpoch);
  buddy.lastWorldEpoch = buddy.lastUpdateEpoch;
  uint32_t mixed = buddy.seed ^ ((uint32_t)buddy.generation * 0x9E3779B9UL);
  buddy.familyEar = buddy.traits.sensitivity > 78 ? 2 : (mixed >> 4) % 6;
  buddy.familyMarking = (mixed >> 12) % 6;
  rememberWorldEvent(buddy.lastUpdateEpoch, EVENT_BORN, OBJECT_COUNT, BEHAVIOR_OBSERVE, 40);
  (void)inherited; // Parent features and preference are assigned by createBuddyLife.
}

int memoryBehaviorBias(BuddyBehavior behavior, uint32_t epoch) {
  int bias = 0;
  for (uint8_t i = 0; i < buddy.eventCount; i++) {
    const WorldMemory &memory = buddy.events[i];
    if (memory.behavior != behavior || memory.epoch > epoch) continue;
    uint32_t ageDays = (epoch - memory.epoch) / 86400UL;
    if (ageDays < 30) bias += memory.pleasure / (1 + ageDays / 4);
  }
  // A remembered favorite remains influential beyond the short memory ring.
  if (buddy.favoriteObject < OBJECT_COUNT) {
    static const BuddyBehavior preferred[] = {BEHAVIOR_EXPLORE, BEHAVIOR_OBSERVE,
      BEHAVIOR_SELF_SOOTHE, BEHAVIOR_PLAY, BEHAVIOR_EXPLORE, BEHAVIOR_OBSERVE,
      BEHAVIOR_DAYDREAM, BEHAVIOR_PLAY};
    if (behavior == preferred[buddy.favoriteObject]) bias += buddy.affection[buddy.favoriteObject] / 8;
  }
  return constrain(bias, -160, 160);
}

BuddyBehavior pursuitBehavior(uint8_t object) {
  static const BuddyBehavior behaviors[OBJECT_COUNT] = {
    BEHAVIOR_EXPLORE, BEHAVIOR_OBSERVE, BEHAVIOR_REST, BEHAVIOR_PLAY,
    BEHAVIOR_EXPLORE, BEHAVIOR_OBSERVE, BEHAVIOR_DAYDREAM, BEHAVIOR_PLAY
  };
  return object < OBJECT_COUNT ? behaviors[object] : BEHAVIOR_OBSERVE;
}

// The life save already holds recent timestamped, meaningful actions. Three
// different days with the same possession in the same six-hour window create
// a routine. It fades naturally when the finite event ring fills with new days.
uint8_t buddyRoutineObject(uint32_t epoch, uint8_t *strength) {
  uint8_t best = OBJECT_COUNT, bestStrength = 0;
  if (strength) *strength = 0;
  if (!buddy.alive) return best;
  uint8_t window = ((epoch / 3600UL) % 24) / 6;
  for (uint8_t object = 0; object < OBJECT_COUNT; object++) {
    if (!(buddy.possessions & (1 << object))) continue;
    uint32_t lastDay = UINT32_MAX;
    uint8_t days = 0;
    for (uint8_t i = 0; i < buddy.eventCount; i++) {
      const WorldMemory &event = buddy.events[(buddy.eventHead + 24 - buddy.eventCount + i) % 24];
      if (event.object != object || event.epoch > epoch ||
          epoch - event.epoch > 14UL * 86400UL ||
          ((event.epoch / 3600UL) % 24) / 6 != window) continue;
      bool livedAction = (event.event >= EVENT_VISITED && event.event <= EVENT_MEDITATED &&
                          event.event != EVENT_INHERITED && event.event != EVENT_GREW) ||
                         event.event == EVENT_PROJECT_WORKED ||
                         event.event == EVENT_NATURE || event.event == EVENT_FLOWERS ||
                         event.event == EVENT_JOURNAL || event.event == EVENT_MUSIC;
      if (!livedAction) continue;
      uint32_t day = event.epoch / 86400UL;
      if (day != lastDay) { lastDay = day; days++; }
    }
    if (days >= 3 && (days > bestStrength ||
        (days == bestStrength && (best == OBJECT_COUNT || buddy.affection[object] > buddy.affection[best])))) {
      best = object;
      bestStrength = days;
    }
  }
  if (strength) *strength = min<uint8_t>(bestStrength, 6);
  return best;
}

// A family custom is an inherited preference that the new individual has
// actually adopted. Affection and repeated visits matter more than ancestry;
// a strongly different favorite can displace it. The family site sets the
// recurring time, even if the old house has since fallen.
int buddyFamilyTraditionAffinity() {
  uint8_t object = buddy.inheritedObject;
  if (buddy.generation < 2 || !buddy.alive || buddy.currentStage < STAGE_CHILD ||
      object >= OBJECT_COUNT || !(buddy.possessions & (1 << object))) return 0;
  int trait = object == OBJECT_STONE || object == OBJECT_GROVE ? buddy.traits.curiosity :
    object == OBJECT_PLANT || object == OBJECT_FLOWERS ? buddy.traits.sensitivity :
    object == OBJECT_NOOK ? buddy.traits.independence :
    object == OBJECT_JOURNAL ? (buddy.traits.sensitivity + buddy.traits.curiosity) / 2 :
    buddy.traits.playfulness;
  int affinity = trait + buddy.affection[object] / 8 +
    min<uint16_t>(buddy.visits[object], 20) * 3;
  if (buddy.prior.dominantBehavior == pursuitBehavior(object)) affinity += 25;
  if (buddy.favoriteObject < OBJECT_COUNT && buddy.favoriteObject != object &&
      buddy.affection[buddy.favoriteObject] > buddy.affection[object] + 120) affinity -= 90;
  return affinity;
}

bool buddyFamilyTraditionWindow(uint32_t epoch) {
  if (buddyFamilyTraditionAffinity() < 150) return false;
  uint32_t family = buddyHouse.magic == BUDDY_HOUSE_MAGIC && !houseLocked ?
    buddyHouse.siteSeed : buddy.prior.deathEpoch;
  uint32_t day = epoch / 86400UL;
  return day % 7 == (family + buddy.inheritedObject) % 7 &&
    ((epoch / 3600UL) % 24) / 6 == (family >> 3) % 4;
}

bool buddyFamilyTraditionRecent(uint32_t epoch) {
  for (uint8_t i = 0; i < buddy.eventCount; i++) {
    const WorldMemory &event = buddy.events[i];
    if (event.event == EVENT_FAMILY_CUSTOM && event.epoch <= epoch &&
        epoch - event.epoch < 4UL * 3600UL) return true;
  }
  return false;
}

// A project lives in the existing world-event ring, not a new save field. Its
// start, repeated work, and finish survive a reboot with the rest of his life.
BuddyProject buddyActiveProject(uint32_t epoch) {
  BuddyProject project = {OBJECT_COUNT, 0, 0};
  for (uint8_t i = 0; i < buddy.eventCount; i++) {
    const WorldMemory &event = buddy.events[(buddy.eventHead + 24 - buddy.eventCount + i) % 24];
    if (event.epoch > epoch) continue;
    if (event.event == EVENT_PROJECT_STARTED && event.object < OBJECT_COUNT)
      project = {event.object, 0, event.epoch};
    else if (event.event == EVENT_PROJECT_WORKED && event.object == project.object)
      project.steps++;
    else if (event.event == EVENT_PROJECT_FINISHED && event.object == project.object)
      project = {OBJECT_COUNT, 0, 0};
  }
  return project;
}

uint32_t buddyRecentProjectCompletion(uint32_t epoch) {
  uint32_t latest = 0;
  for (uint8_t i = 0; i < buddy.eventCount; i++) {
    const WorldMemory &event = buddy.events[i];
    if (event.event == EVENT_PROJECT_FINISHED && event.epoch <= epoch && event.epoch > latest)
      latest = event.epoch;
  }
  return latest;
}

static uint8_t chooseBuddyProject(uint32_t epoch) {
  uint8_t best = OBJECT_COUNT;
  int bestScore = -10000;
  uint32_t day = (epoch - buddy.birthEpoch) / 86400UL;
  for (uint8_t object = 0; object < OBJECT_COUNT; object++) {
    if (!(buddy.possessions & (1 << object)) || buddy.visits[object] < 2) continue;
    uint32_t mixed = buddy.seed ^ day * 0x9E3779B9UL ^ object;
    mixed ^= mixed >> 16; mixed *= 0x7FEB352DUL; mixed ^= mixed >> 15;
    int score = buddy.affection[object] / 4 +
      (object == OBJECT_STONE || object == OBJECT_GROVE ? buddy.traits.curiosity * 3 + (1000 - buddy.curiosityNeed) / 6 :
       object == OBJECT_PLANT ? buddy.traits.sensitivity * 3 + (100 - buddy.plantGrowth) :
       object == OBJECT_FLOWERS ? buddy.traits.sensitivity * 3 + (1000 - buddy.comfort) / 6 :
       object == OBJECT_NOOK ? buddy.traits.independence * 3 + (1000 - buddy.security) / 6 :
       object == OBJECT_JOURNAL ? buddy.traits.sensitivity * 2 + buddy.traits.curiosity + (1000 - buddy.social) / 6 :
       buddy.traits.playfulness * 3 + (1000 - buddy.play) / 6) +
      (int)(mixed % 90);
    if (object == buddy.inheritedObject) score += 35;
    if (score > bestScore) { bestScore = score; best = object; }
  }
  return best;
}

// A reproducible daily interest, built from this life rather than a new save
// field. Affection and memories let the pursuit evolve; the seed and day give
// each generation its own rhythm even when it owns the same four things.
uint8_t buddyDailyPursuit(uint32_t epoch) {
  if (!buddy.alive || epoch < buddy.birthEpoch) return OBJECT_COUNT;
  BuddyProject project = buddyActiveProject(epoch);
  if (project.object < OBJECT_COUNT) return project.object;
  uint32_t day = (epoch - buddy.birthEpoch) / 86400UL;
  uint8_t best = OBJECT_COUNT;
  int bestScore = -1;
  uint8_t routineStrength = 0;
  uint8_t routine = buddyRoutineObject(epoch, &routineStrength);
  bool familyTime = buddyFamilyTraditionWindow(epoch);
  uint8_t oldWork = !legacyLocked && buddyLegacy.magic == BUDDY_LEGACY_MAGIC ?
    buddyLegacyInvitation(buddy, buddyLegacy, epoch) : OBJECT_COUNT;
  for (uint8_t object=0; object<OBJECT_COUNT; object++) {
    if (!(buddy.possessions & (1 << object))) continue;
    int temperament = object == OBJECT_STONE ? buddy.traits.curiosity + buddy.traits.independence :
      object == OBJECT_PLANT ? buddy.traits.sensitivity + buddy.traits.resilience :
      object == OBJECT_NOOK ? buddy.traits.independence + buddy.traits.sensitivity :
      object == OBJECT_GROVE ? buddy.traits.curiosity + buddy.traits.resilience :
      object == OBJECT_FLOWERS ? buddy.traits.sensitivity + buddy.traits.curiosity :
      object == OBJECT_JOURNAL ? buddy.traits.sensitivity + buddy.traits.independence :
      buddy.traits.playfulness + buddy.traits.sociability;
    uint32_t mixed = buddy.seed ^ (day * 0x9E3779B9UL) ^ ((uint32_t)(object+1) * 0x85EBCA6BUL);
    mixed ^= mixed >> 16; mixed *= 0x7FEB352DUL; mixed ^= mixed >> 15;
    int score = temperament * 2 + buddy.affection[object] / 6 +
      (int)((mixed >> 16) % 200) + (object == buddy.inheritedObject ? 55 : 0) +
      (object == routine ? routineStrength * 42 : 0) +
      (object == oldWork ? 145 : 0) +
      (familyTime && object == buddy.inheritedObject ? 220 : 0);
    if (!legacyLocked && buddyLegacy.magic == BUDDY_LEGACY_MAGIC)
      score += buddyLegacy.condition[object] / 3;
    if (!ambitionLocked && buddyAmbition.lifeSeed == buddy.seed &&
        buddyAmbition.birthEpoch == buddy.birthEpoch && buddyAmbition.kind == object)
      score += 95;
    // Unfamiliar possessions can become interests without erasing old favorites.
    if (buddy.visits[object] < 12) score += 36 - buddy.visits[object] * 3;
    for (uint8_t i=0; i<buddy.eventCount; i++) {
      const WorldMemory &memory = buddy.events[i];
      if (memory.object == object && memory.epoch <= epoch && epoch-memory.epoch < 7UL*86400UL)
        score += max(0, (int)memory.pleasure) / 4;
    }
    if (score > bestScore) { bestScore = score; best = object; }
  }
  return best;
}

static bool practiceFamilyCustom(uint32_t epoch, BuddyBehavior behavior) {
  uint8_t object = buddy.inheritedObject;
  if (!buddyFamilyTraditionWindow(epoch) || buddy.energy < 300 || buddy.health < 350 ||
      behavior != pursuitBehavior(object)) return false;
  buddy.visits[object] = min<uint32_t>(65535, (uint32_t)buddy.visits[object] + 1);
  buddy.affection[object] = clamp1000(buddy.affection[object] + 18);
  buddy.mood = clamp1000(buddy.mood + 18);
  if (!houseLocked && buddyHouse.phase == HOUSE_OCCUPIED)
    buddy.security = clamp1000(buddy.security + 14);
  rememberWorldEvent(epoch, EVENT_FAMILY_CUSTOM, object, behavior, 45);
  return true;
}

static void developBuddy(uint32_t epoch, BuddyBehavior behavior, bool climateKnown) {
  uint8_t trait = behavior == BEHAVIOR_EXPLORE || behavior == BEHAVIOR_OBSERVE ? 0 :
    behavior == BEHAVIOR_PLAY ? 2 : behavior == BEHAVIOR_SELF_SOOTHE ? 5 : 3;
  if (buddy.development[trait] < 30000) buddy.development[trait]++;
  if (climateKnown && buddy.comfort < 400) {
    buddy.development[3]++;
    buddy.development[4]++;
  } else if (climateKnown && buddy.comfort > 750) {
    buddy.development[4]--;
  }
  uint32_t day = buddyAgeDays(epoch);
  if (day == buddy.lastDevelopmentDay) return;
  buddy.lastDevelopmentDay = day;
  int threshold = day < 61 ? 12 : day < 151 ? 28 : 60;
  uint8_t *traits = (uint8_t *)&buddy.traits;
  const uint8_t *baseline = (const uint8_t *)&buddy.birthTraits;
  for (uint8_t i = 0; i < 6; i++) {
    if (abs(buddy.development[i]) < threshold) continue;
    int direction = buddy.development[i] > 0 ? 1 : -1;
    // Preserve identity: experience can shift each trait by at most 20 points.
    traits[i] = constrain((int)traits[i] + direction, max(5, (int)baseline[i]-20), min(95, (int)baseline[i]+20));
    buddy.development[i] -= direction * threshold;
  }
}

void liveWorldHour(uint32_t epoch, BuddyBehavior behavior, bool climateKnown) {
  developBuddy(epoch, behavior, climateKnown);
  serviceBuddyAmbition(epoch);
  // Events are actual state changes, at most one every six simulated hours.
  if (epoch - buddy.lastWorldEpoch < 6UL * 3600UL || behavior == BEHAVIOR_SLEEP) return;
  buddy.lastWorldEpoch = epoch;
  if (practiceFamilyCustom(epoch, behavior)) return;
  BuddyProject project = buddyActiveProject(epoch);
  if (project.object < OBJECT_COUNT && behavior == pursuitBehavior(project.object) && buddy.energy > 300) {
    bool complete = project.steps >= 2;
    buddy.energy = clamp1000((int)buddy.energy - 12);
    buddy.affection[project.object] = clamp1000(buddy.affection[project.object] + (complete ? 55 : 8));
    if (complete) {
      if (project.object == OBJECT_PLANT) buddy.plantGrowth = min<int>(100, buddy.plantGrowth + 9);
      else if (project.object == OBJECT_TOY || project.object == OBJECT_INSTRUMENT)
        buddy.play = clamp1000(buddy.play + 65);
      else if (project.object == OBJECT_STONE || project.object == OBJECT_GROVE)
        buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 65);
      else if (project.object == OBJECT_FLOWERS) buddy.comfort = clamp1000(buddy.comfort + 65);
      else if (project.object == OBJECT_JOURNAL) buddy.social = clamp1000(buddy.social + 65);
      else buddy.security = clamp1000(buddy.security + 65);
      buddy.mood = clamp1000(buddy.mood + 35);
      buddy.behaviorLearning[behavior] = constrain((int)buddy.behaviorLearning[behavior] + 5, -50, 50);
      buddySpecialPending = true;
      buddySpecialEpoch = epoch;
    }
    rememberWorldEvent(epoch, complete ? EVENT_PROJECT_FINISHED : EVENT_PROJECT_WORKED,
                       project.object, behavior, complete ? 70 : 18);
    return;
  }
  uint32_t recentFinish = buddyRecentProjectCompletion(epoch);
  if (project.object == OBJECT_COUNT && buddy.currentStage >= STAGE_CHILD && buddy.energy > 360 &&
      (!recentFinish || epoch - recentFinish >= 2UL * 86400UL)) {
    uint8_t chosen = chooseBuddyProject(epoch);
    if (chosen < OBJECT_COUNT) {
      rememberWorldEvent(epoch, EVENT_PROJECT_STARTED, chosen, pursuitBehavior(chosen), 22);
      return;
    }
  }
  uint8_t object = behavior == BEHAVIOR_PLAY ?
    (buddy.possessions & (1 << OBJECT_INSTRUMENT) && buddy.traits.sensitivity > buddy.traits.playfulness ?
      OBJECT_INSTRUMENT : OBJECT_TOY) :
    behavior == BEHAVIOR_OBSERVE ?
      (buddy.possessions & (1 << OBJECT_FLOWERS) && buddy.traits.sensitivity > 60 ?
        OBJECT_FLOWERS : OBJECT_PLANT) :
    behavior == BEHAVIOR_DAYDREAM && buddy.currentStage >= STAGE_YOUNG ? OBJECT_JOURNAL :
    behavior == BEHAVIOR_REST || behavior == BEHAVIOR_SELF_SOOTHE ? OBJECT_NOOK : OBJECT_STONE;
  if (behavior == BEHAVIOR_EXPLORE) {
    object = randomRange(0, OBJECT_COUNT - 1);
    if (buddy.inheritedObject < OBJECT_COUNT && randomRange(0, 3) == 0) object = buddy.inheritedObject;
  }
  uint8_t pursuit = buddyDailyPursuit(epoch);
  if (pursuit < OBJECT_COUNT && behavior == pursuitBehavior(pursuit)) object = pursuit;
  bool found = !(buddy.possessions & (1 << object));
  if (found) { buddySpecialPending = true; buddySpecialEpoch = epoch; }
  buddy.possessions |= 1 << object;
  buddy.visits[object] = min<uint32_t>(65535, (uint32_t)buddy.visits[object] + 1);
  int pleasure = 8 + buddy.mood / 80 + (object == buddy.inheritedObject ? 5 : 0);
  buddy.affection[object] = clamp1000(buddy.affection[object] + pleasure);
  // Mild forgetting makes preferences capable of changing over a long life.
  for (uint8_t i = 0; i < OBJECT_COUNT; i++) if (i != object && buddy.affection[i]) buddy.affection[i]--;
  buddy.favoriteObject = object;
  for (uint8_t i = 0; i < OBJECT_COUNT; i++)
    if (buddy.affection[i] > buddy.affection[buddy.favoriteObject]) buddy.favoriteObject = i;
  LifeEvent event = EVENT_VISITED;
  if (found) event = EVENT_FOUND;
  else if (object == OBJECT_PLANT) {
    event = buddy.visits[object] % 3 ? EVENT_WATERED : EVENT_TENDED;
    buddy.plantGrowth = min<int>(100, buddy.plantGrowth + 2);
    buddy.security = clamp1000(buddy.security + 6);
  } else if (object == OBJECT_TOY) {
    event = buddy.visits[object] % 4 == 0 ? EVENT_MENDED : EVENT_PLAYED;
    buddy.play = clamp1000(buddy.play + 18);
    if (event == EVENT_MENDED) buddy.security = clamp1000(buddy.security + 8);
  } else if (object == OBJECT_NOOK) {
    event = buddy.visits[object] % 3 == 0 ? EVENT_TIDIED : EVENT_RESTED;
    buddy.security = clamp1000(buddy.security + (event == EVENT_TIDIED ? 25 : 15));
  } else if (object == OBJECT_STONE) {
    event = buddy.visits[object] % 3 == 0 ? EVENT_POLISHED : EVENT_VISITED;
    buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 12);
  } else if (object == OBJECT_GROVE) {
    event = EVENT_NATURE;
    buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 28);
    buddy.comfort = clamp1000(buddy.comfort + (climateKnown && latestTempC >= 18 && latestTempC <= 30 ? 18 : 8));
  } else if (object == OBJECT_FLOWERS) {
    event = EVENT_FLOWERS;
    buddy.comfort = clamp1000(buddy.comfort + 25);
    buddy.mood = clamp1000(buddy.mood + 16);
  } else if (object == OBJECT_JOURNAL) {
    event = EVENT_JOURNAL;
    buddy.social = clamp1000(buddy.social + 18);
    buddy.security = clamp1000(buddy.security + (buddy.eventCount >= 3 ? 16 : 8));
  } else if (object == OBJECT_INSTRUMENT) {
    event = EVENT_MUSIC;
    buddy.play = clamp1000(buddy.play + 25);
    buddy.social = clamp1000(buddy.social + 10);
  }
  bool canInvent = !found && object == OBJECT_TOY && behavior == BEHAVIOR_EXPLORE &&
    buddy.currentStage >= STAGE_YOUNG &&
    (buddy.possessions & (1 << OBJECT_TOY)) && (buddy.possessions & (1 << OBJECT_STONE));
  if (canInvent && randomRange(0,3) == 0) {
    event = EVENT_INVENTED;
    buddy.play = clamp1000(buddy.play + 28);
    buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 35);
    buddy.affection[OBJECT_TOY] = clamp1000(buddy.affection[OBJECT_TOY] + 8);
    buddy.affection[OBJECT_STONE] = clamp1000(buddy.affection[OBJECT_STONE] + 8);
  } else if (!found && behavior == BEHAVIOR_PLAY && object == OBJECT_TOY && buddy.visits[object] % 5 == 0) {
    event = EVENT_PRACTICED;
    buddy.health = clamp1000(buddy.health + 2);
  } else if (!found && object == OBJECT_PLANT && buddy.currentStage >= STAGE_CHILD && buddy.visits[object] % 5 == 0) {
    event = EVENT_GARDENED;
    buddy.plantGrowth = min<int>(100, buddy.plantGrowth + 3);
    buddy.health = clamp1000(buddy.health + 1);
  } else if (!found && object == OBJECT_STONE && buddy.currentStage >= STAGE_YOUNG && buddy.visits[object] % 5 == 0) {
    event = EVENT_SCULPTED;
    buddy.mood = clamp1000(buddy.mood + 12);
  } else if (!found && object == OBJECT_NOOK && behavior == BEHAVIOR_EXPLORE &&
             (buddy.possessions & (1 << OBJECT_TOY)) && buddy.visits[object] % 4 == 0) {
    event = EVENT_BUILT_FORT;
    buddy.security = clamp1000(buddy.security + 32);
    buddy.play = clamp1000(buddy.play + 15);
  } else if (!found && object == OBJECT_NOOK && __builtin_popcount((unsigned)buddy.possessions) >= 2 &&
             buddy.visits[object] % 5 == 0) {
    event = EVENT_DECORATED;
    buddy.security = clamp1000(buddy.security + 20);
  } else if (!found && behavior == BEHAVIOR_EXPLORE && buddy.currentStage >= STAGE_YOUNG &&
             buddy.visits[object] % 7 == 0) {
    event = EVENT_CHARTED;
    buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 30);
  } else if (!found && behavior == BEHAVIOR_EXPLORE && buddy.visits[object] % 4 == 0) {
    event = EVENT_COLLECTED;
    buddy.affection[object] = clamp1000(buddy.affection[object] + 6);
  } else if (!found && behavior == BEHAVIOR_DAYDREAM && buddy.currentStage >= STAGE_YOUNG &&
             buddy.eventCount >= 3 && buddy.visits[object] % 4 == 0) {
    event = EVENT_TOLD_STORY;
    buddy.social = clamp1000(buddy.social + 15);
    buddy.mood = clamp1000(buddy.mood + 10);
  } else if (!found && behavior == BEHAVIOR_DAYDREAM && buddy.visits[object] % 3 == 0) {
    event = EVENT_MEDITATED;
    buddy.security = clamp1000(buddy.security + 22);
    buddy.energy = clamp1000(buddy.energy + 10);
  }
  rememberWorldEvent(epoch, event, object, behavior, pleasure);
}
