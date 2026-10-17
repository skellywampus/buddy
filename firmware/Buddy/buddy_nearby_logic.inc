// Local-only encounter history. The life and house save ABIs are not touched.
static uint32_t nearbySequence = 0;
static int8_t activeNearbySlot = -1;
static const uint32_t NEAR_MEET_COOLDOWN = 6UL * 3600UL;
static const uint32_t NEAR_HANDSHAKE_MS = 12UL * 60UL * 1000UL;
static const uint32_t NEAR_SCENE_MS = 4UL * 60UL * 1000UL;

bool nearbyDiscoveryActive() {
  return cableFriendsActive();
}

static uint32_t nearbyChecksum(const NearbyState &state) {
  NearbyState copy = state;
  copy.checksum = 0;
  uint32_t hash = 2166136261UL;
  const uint8_t *bytes = (const uint8_t *)&copy;
  for (size_t i = 0; i < sizeof(copy); i++) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}

static bool validNearbyState(const NearbyState &state) {
  if (state.magic != NEAR_MAGIC || state.version != NEAR_VERSION ||
      !state.lifeSeed || !state.selfId || state.checksum != nearbyChecksum(state)) return false;
  for (const NearbyContact &contact : state.contacts)
    if (contact.affinity > 100 || contact.lastActivity > NEAR_WATCH) return false;
  return true;
}

static bool readNearbyState(const char *key, NearbyState &state) {
  return preferences.getBytesLength(key) == sizeof(state) &&
    preferences.getBytes(key, &state, sizeof(state)) == sizeof(state) && validNearbyState(state);
}

void saveNearbyState() {
  if (storageLocked || nearbyStorageLocked || nearbyState.magic != NEAR_MAGIC) return;
  NearbyState next = nearbyState, check = {};
  next.sequence = nearbySequence + 1;
  next.checksum = nearbyChecksum(next);
  int8_t slot = activeNearbySlot == 0 ? 1 : 0;
  const char *key = slot == 0 ? "near-a" : "near-b";
  nearbySaveFailed = preferences.putBytes(key, &next, sizeof(next)) != sizeof(next) ||
    !readNearbyState(key, check) || check.sequence != next.sequence;
  if (!nearbySaveFailed) {
    nearbyState = next;
    nearbySequence = next.sequence;
    activeNearbySlot = slot;
  }
}

static void newNearbyLife() {
  nearbyState = {};
  nearbyState.magic = NEAR_MAGIC;
  nearbyState.version = NEAR_VERSION;
  nearbyState.lifeSeed = buddy.seed;
  nearbyState.selfId = esp_random() ^ buddy.seed ^ 0x4E454152UL;
  if (!nearbyState.selfId) nearbyState.selfId = 1;
  nearbyEncounter = {};
  memset(nearbySeen, 0, sizeof(nearbySeen));
  saveNearbyState();
  if (!nearbySaveFailed) saveNearbyState();
}

void setupNearbyStorage() {
  if (storageLocked || buddy.magic != BUDDY_MAGIC) return;
  nearbyEnabled = preferences.getUChar("near-on", 0) == 1;
  NearbyState a = {}, b = {};
  bool hasA = readNearbyState("near-a", a), hasB = readNearbyState("near-b", b);
  if (hasA || hasB) {
    bool useB = hasB && (!hasA || (int32_t)(b.sequence - a.sequence) > 0);
    nearbyState = useB ? b : a;
    activeNearbySlot = useB ? 1 : 0;
    nearbySequence = nearbyState.sequence;
    nearbyStorageLocked = false;
    nearbySaveFailed = false;
    if (nearbyState.lifeSeed != buddy.seed) newNearbyLife();
    return;
  }
  if (preferences.getBytesLength("near-a") || preferences.getBytesLength("near-b")) {
    nearbyStorageLocked = true; // do not erase unreadable social history
    return;
  }
  if (nearbyEnabled) newNearbyLife();
}

bool setNearbyEnabled(bool enabled) {
  if (storageLocked || buddy.magic != BUDDY_MAGIC) return false;
  if (!enabled) nearbyVoiceSearchUntilMs = 0;
  if (!enabled) stopNearbyRadio();
  nearbyEnabled = enabled;
  nearbySettingSaveFailed = preferences.putUChar("near-on", enabled ? 1 : 0) != 1 ||
    preferences.getUChar("near-on", !enabled) != (enabled ? 1 : 0);
  if (nearbySettingSaveFailed) {
    if (enabled) nearbyEnabled = false;
    return false;
  }
  if (enabled) {
    setupNearbyStorage();
    if (!nearbyStorageLocked && !nearbySaveFailed && !hourlyDozeWake) startNearbyRadio();
  } else {
    nearbyEncounter = {};
    memset(nearbySeen, 0, sizeof(nearbySeen));
  }
  return true;
}

void serviceNearbyLife() {
  if (!nearbyDiscoveryActive() || storageLocked || nearbyStorageLocked || !buddy.alive) return;
  if (nearbyState.magic != NEAR_MAGIC || nearbyState.lifeSeed != buddy.seed) {
    stopNearbyRadio();
    newNearbyLife();
    if (!nearbySaveFailed) refreshCablePacket();
  }
}

static const NearbyContact *nearbyContact(uint32_t id) {
  for (const NearbyContact &contact : nearbyState.contacts)
    if (contact.peerId == id) return &contact;
  return nullptr;
}

static bool readyForNearbyMeeting(uint32_t peerId, uint32_t now) {
  const NearbyContact *contact = nearbyContact(peerId);
  return !contact || !contact->lastMeetEpoch ||
    (now >= contact->lastMeetEpoch && now - contact->lastMeetEpoch >= NEAR_MEET_COOLDOWN);
}

static uint32_t nearbyMix(uint32_t value) {
  value ^= value >> 16; value *= 0x7FEB352DUL;
  value ^= value >> 15; value *= 0x846CA68BUL;
  return value ^ (value >> 16);
}

static uint8_t chooseNearbyActivity(uint32_t peerId, uint32_t now) {
  int score[8] = {};
  score[NEAR_PLAY] = buddy.traits.playfulness * 4 + (1000 - buddy.play) / 3;
  score[NEAR_EXPLORE] = buddy.traits.curiosity * 4 + (1000 - buddy.curiosityNeed) / 3;
  score[NEAR_REST] = (1000 - buddy.energy) / 2 + buddy.traits.independence * 3;
  score[NEAR_STORY] = buddy.traits.sociability * 3 + buddy.traits.sensitivity * 2;
  score[NEAR_BUILD] = buddyHouse.phase == HOUSE_BUILDING && buddy.energy >= 450 &&
    buddy.currentStage >= STAGE_CHILD ? 550 + buddy.traits.resilience * 2 : -10000;
  score[NEAR_GARDEN] = (buddy.possessions & (1 << OBJECT_PLANT)) ?
    220 + buddy.traits.sensitivity * 3 : -10000;
  score[NEAR_WATCH] = 120 + buddy.traits.independence * 2 + buddy.traits.curiosity;
  uint8_t chosen = NEAR_PLAY;
  for (uint8_t activity = NEAR_PLAY; activity <= NEAR_WATCH; activity++) {
    score[activity] += nearbyMix(buddy.seed ^ peerId ^ (now / 86400UL) ^ (activity * 0x9E3779B9UL)) % 100;
    if (score[activity] > score[chosen]) chosen = activity;
  }
  return chosen;
}

static bool acceptsNearbyActivity(uint8_t activity, uint32_t peerId, uint32_t now) {
  if (!buddy.alive || buddy.health < 300 || buddy.energy < 240 || activity == NEAR_NONE ||
      activity > NEAR_WATCH) return false;
  if (activity == NEAR_BUILD && (buddy.currentStage < STAGE_CHILD || buddy.energy < 440)) return false;
  if (buddy.traits.independence > 80 && buddy.social > 750 &&
      (activity == NEAR_PLAY || activity == NEAR_STORY) &&
      nearbyMix(buddy.seed ^ peerId ^ (now / 86400UL)) % 3 == 0) return false;
  return true;
}

const char *nearbyActivityText(uint8_t activity) {
  switch (activity) {
    case NEAR_PLAY: return "PLAYING TOGETHER";
    case NEAR_EXPLORE: return "EXPLORING TOGETHER";
    case NEAR_REST: return "RESTING TOGETHER";
    case NEAR_STORY: return "SHARING STORIES";
    case NEAR_BUILD: return "BUILDING TOGETHER";
    case NEAR_GARDEN: return "TENDING A GARDEN";
    default: return "WATCHING TOGETHER";
  }
}

NearbyPacket currentNearbyPacket() {
  NearbyPacket packet = {};
  packet.sender = nearbyState.selfId;
  packet.visual = nearbyVisualGenome();
  packet.stage = buddy.currentStage;
  if (nearbyEncounter.phase != NEAR_IDLE) {
    packet.target = nearbyEncounter.peer.sender;
    packet.activity = nearbyEncounter.activity;
    packet.confirmed = nearbyEncounter.phase != NEAR_INVITING;
  }
  return packet;
}

static void rememberNearbyMeeting(uint32_t peerId, uint8_t activity, uint32_t now) {
  NearbyContact *contact = nullptr;
  for (NearbyContact &entry : nearbyState.contacts)
    if (entry.peerId == peerId) { contact = &entry; break; }
  if (!contact) {
    contact = &nearbyState.contacts[0];
    for (NearbyContact &entry : nearbyState.contacts)
      if (!entry.peerId || entry.lastMeetEpoch < contact->lastMeetEpoch) contact = &entry;
    *contact = {};
    contact->peerId = peerId;
    contact->affinity = 30;
  }
  contact->lastMeetEpoch = now;
  if (contact->meetings < UINT16_MAX) contact->meetings++;
  contact->affinity = min<int>(100, contact->affinity + 3 + buddy.traits.sociability / 18);
  contact->lastActivity = activity;
  saveNearbyState();
  // Populate both A/B slots for the new meeting. This is intentional: a
  // single damaged slot must not erase the latest known friendship.
  if (!nearbySaveFailed) saveNearbyState();

  buddy.social = clamp1000(buddy.social + 30 + buddy.traits.sociability / 2);
  switch (activity) {
    case NEAR_PLAY: buddy.play = clamp1000(buddy.play + 70); buddy.energy = clamp1000((int)buddy.energy - 25); break;
    case NEAR_EXPLORE: buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 65); buddy.energy = clamp1000((int)buddy.energy - 18); break;
    case NEAR_REST: buddy.energy = clamp1000(buddy.energy + 55); buddy.security = clamp1000(buddy.security + 20); break;
    case NEAR_STORY: buddy.curiosityNeed = clamp1000(buddy.curiosityNeed + 30); buddy.security = clamp1000(buddy.security + 18); break;
    case NEAR_BUILD:
      buddy.energy = clamp1000((int)buddy.energy - 30);
      buddy.security = clamp1000(buddy.security + 20);
      if (!houseLocked && buddyHouse.phase == HOUSE_BUILDING && buddyHouse.work < BUDDY_HOUSE_WORK_TARGET - 5) {
        buddyHouse.work += 5; // help matters without replacing months of autonomous work
        saveBuddyHouse();
      }
      break;
    case NEAR_GARDEN: buddy.plantGrowth = min<int>(100, buddy.plantGrowth + 2); buddy.comfort = clamp1000(buddy.comfort + 20); break;
    default: buddy.security = clamp1000(buddy.security + 10); break;
  }
  buddy.mood = clamp1000(buddy.mood + 20);
  rememberWorldEvent(now, contact->meetings == 1 ? EVENT_MET_BUDDY : EVENT_SHARED_ACTIVITY,
                     OBJECT_COUNT, (BuddyBehavior)buddy.currentBehavior, 35);
  saveBuddy();
}

static void beginNearbyScene(uint32_t now) {
  nearbyEncounter.phase = NEAR_SHARED;
  nearbyEncounter.sharedMs = millis();
  if (readyForNearbyMeeting(nearbyEncounter.peer.sender, now))
    rememberNearbyMeeting(nearbyEncounter.peer.sender, nearbyEncounter.activity, now);
  cleanPartialRequested = true;
  needsRender = true;
  refreshNearbyAdvertisement();
}

static void rememberNearbySeen(const NearbyPacket &packet, int8_t rssi) {
  NearbySeen *slot = nullptr;
  for (NearbySeen &seen : nearbySeen) if (seen.packet.sender == packet.sender) { slot = &seen; break; }
  if (!slot) {
    slot = &nearbySeen[0];
    for (NearbySeen &seen : nearbySeen)
      if (!seen.packet.sender || (uint32_t)(millis() - seen.lastMs) > (uint32_t)(millis() - slot->lastMs)) slot = &seen;
  }
  slot->packet = packet;
  slot->lastMs = millis();
  slot->rssi = rssi;
}

uint8_t nearbySeenCount() {
  uint8_t count = 0;
  for (const NearbySeen &seen : nearbySeen)
    if (seen.packet.sender && millis() - seen.lastMs < 15UL * 60UL * 1000UL) count++;
  return count;
}

bool nearbySceneVisible() {
  if (cableFriendsActive() && cableSession.phase == CABLE_CONNECTED &&
      cableScenePeer.sender && validNearbyVisual(cableScenePeer.visual)) return true;
  return nearbyDiscoveryActive() && nearbyEncounter.phase == NEAR_SHARED &&
    !nearbyEncounter.sceneClosed && millis() - nearbyEncounter.sharedMs < NEAR_SCENE_MS;
}

void observeNearbyPacket(const NearbyPacket &packet, int8_t rssi) {
  if (!nearbyDiscoveryActive() || nearbyStorageLocked || clockFault || !buddy.alive ||
      packet.sender == nearbyState.selfId || !packet.sender || !validNearbyVisual(packet.visual)) return;
  uint32_t now = buddyNow();
  if (!now) return;
  rememberNearbySeen(packet, rssi);
  if (nearbyEncounter.phase != NEAR_IDLE) {
    if (packet.sender != nearbyEncounter.peer.sender) return;
    nearbyEncounter.peer = packet;
    nearbyEncounter.lastPeerMs = millis();
    if (nearbyEncounter.phase == NEAR_INVITING && packet.target == nearbyState.selfId &&
        packet.confirmed && packet.activity == nearbyEncounter.activity) beginNearbyScene(now);
    else if (nearbyEncounter.phase == NEAR_ACCEPTING && packet.target == nearbyState.selfId &&
             packet.confirmed && packet.activity == nearbyEncounter.activity) beginNearbyScene(now);
    return;
  }
  // Contact cooldown limits repeat affinity/life rewards, not the encounter
  // itself. Buddies should still recognize one another and share the scene on
  // every manually initiated Cable Friends session.
  if (packet.sender < nearbyState.selfId && packet.target == nearbyState.selfId &&
      !packet.confirmed && acceptsNearbyActivity(packet.activity, packet.sender, now)) {
    nearbyEncounter = {packet, millis(), millis(), 0, packet.activity, NEAR_ACCEPTING, false};
    refreshNearbyAdvertisement();
  } else if (nearbyState.selfId < packet.sender && packet.target == 0) {
    uint8_t activity = chooseNearbyActivity(packet.sender, now);
    nearbyEncounter = {packet, millis(), millis(), 0, activity, NEAR_INVITING, false};
    refreshNearbyAdvertisement();
  }
}

void serviceNearbyEncounter() {
  if (nearbyEncounter.phase == NEAR_IDLE) return;
  if (millis() - nearbyEncounter.startedMs > NEAR_HANDSHAKE_MS) {
    bool wasVisible = nearbySceneVisible();
    nearbyEncounter = {};
    refreshNearbyAdvertisement();
    if (wasVisible) { cleanPartialRequested = true; needsRender = true; }
  } else if (nearbyEncounter.phase == NEAR_SHARED && !nearbyEncounter.sceneClosed &&
             millis() - nearbyEncounter.sharedMs >= NEAR_SCENE_MS) {
    // End the temporary scene but retain the confirmation until the peer has
    // had another scan window to see it.
    nearbyEncounter.sceneClosed = true;
    cleanPartialRequested = true;
    needsRender = true;
  }
}
