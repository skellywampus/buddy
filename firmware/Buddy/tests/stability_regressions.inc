// Regression cases uncovered by the October 13 read-only audit.
static void testAuditStabilization() {
  uint32_t randomBefore=fakeRandomState;
  start();ticks=0;usb=false;previousBatteryV=0;
  risingBatterySamples=fallingBatterySamples=0;batteryChargeTrend=false;
  updateBuddyPowerEstimate(3.8f);
  for(uint32_t minute=1;minute<10;++minute){ticks=minute*60000;updateBuddyPowerEstimate(3.8f+minute*.001f);}
  assert(risingBatterySamples==0 && !batteryChargeTrend);
  ticks=600000;updateBuddyPowerEstimate(3.81f);assert(risingBatterySamples==1);
  ticks+=60000;updateBuddyPowerEstimate(0);assert(!risingBatterySamples&&!previousBatteryV);
  // Recovery cannot supersede the independently saved Sound OFF switch.
  start(); setupBuddyAmbition(rawEpoch);
  buddyAmbition.kind=OBJECT_PLANT; buddyAmbition.startedEpoch=rawEpoch;
  failNvsRemove=true; endBuddyAmbition(rawEpoch+3600,AMBITION_FULFILLED);
  assert(buddyCompletionPending);
  assert(setBuddySoundMode(SOUND_OFF) && micStops);
  failNvsRemove=false;
  buddy={};buddyAmbition={};buddyLegacy={};saveFailed=false;
  loadOrCreateBuddy(0);loadBuddySoundMode();
  setupBuddyAmbition(rawEpoch+3600);setupBuddyLegacy(rawEpoch+3600);
  assert(recoverBuddyCompletion());
  assert(!buddy.soundEnabled && buddySoundSample(1000,SOUND_OFF)==0);
  touchBuddyListeningWindow();assert(!buddyListeningWindow.open);

  // Actual catch-up loop, interrupted after a completion, must not replay
  // already applied hourly actions. Both formats survive real loader replay.
  for (int cut=0;cut<=5;++cut) {
    start(); buddy.birthEpoch=rawEpoch-20UL*86400UL; buddy.currentStage=STAGE_CHILD;
    buddy.possessions=1<<OBJECT_PLANT;buddy.visits[OBJECT_PLANT]=10;
    buddy.affection[OBJECT_PLANT]=500;
    preferences.remove("aim-a");preferences.remove("aim-b");
    setupBuddyAmbition(rawEpoch);
    buddyAmbition.kind=OBJECT_PLANT;buddyAmbition.startedEpoch=rawEpoch;
    buddyAmbition.progress=95;saveBuddyAmbition();saveBuddy();
    uint32_t target=rawEpoch+120UL*3600UL;
    auto initialNvs=nvs;
    updateBuddySimulation(target);
    BuddyState uninterrupted=buddy;
    BuddyHouseState uninterruptedHouse=buddyHouse;
    BuddyAmbitionState uninterruptedAmbition=buddyAmbition;
    BuddyLegacyState uninterruptedLegacy=buddyLegacy;
    nvs=initialNvs;buddy={};buddyHouse={};buddyAmbition={};buddyLegacy={};
    activeSaveSlot=activeAmbitionSlot=activeLegacySlot=activeHouseSlot=-1;
    saveSequence=ambitionSequence=legacySequence=houseSequence=0;
    loadOrCreateBuddy(0);setupBuddyHouse(rawEpoch);setupBuddyAmbition(rawEpoch);setupBuddyLegacy(rawEpoch);
    // Allow earlier non-completion hours, then fail commit removal: this
    // recreates the 132-actions-for-120-hours bug from the audit.
    failNvsRemove=true;
    updateBuddySimulation(target);
    for(unsigned transactions=0;buddyCompletionPending&&!buddyAmbition.lastEndedEpoch;++transactions) {
      assert(transactions<120);
      failNvsRemove=false;assert(recoverBuddyCompletion());failNvsRemove=true;
      updateBuddySimulation(target);
    }
    assert(buddyAmbition.lastEndedEpoch);
    assert(buddyCompletionPending && buddy.lastUpdateEpoch>rawEpoch);
    uint32_t checkpoint=buddy.lastUpdateEpoch;
    failNvsRemove=false;failNvsAfter=cut;
    recoverBuddyCompletion(); // fault each replay write, including house
    failNvsAfter=-1;
    buddy={};buddyAmbition={};buddyLegacy={};buddyHouse={};
    buddyCompletionPending=buddyCompletionPreparing=buddyCompletionApplying=false;
    activeSaveSlot=activeAmbitionSlot=activeLegacySlot=activeHouseSlot=-1;
    saveSequence=ambitionSequence=legacySequence=houseSequence=0;
    saveFailed=ambitionSaveFailed=legacySaveFailed=houseSaveFailed=false;
    loadOrCreateBuddy(0);setupBuddyHouse(checkpoint);setupBuddyAmbition(checkpoint);setupBuddyLegacy(checkpoint);
    assert(recoverBuddyCompletion());
    updateBuddySimulation(target);
    unsigned hours=0;for(uint32_t count:buddy.behaviorCounts)hours+=count;
    assert(hours==120 && buddy.lastUpdateEpoch==target);
    assert(!memcmp(&buddy,&uninterrupted,sizeof(buddy)));
    uninterruptedHouse.sequence=buddyHouse.sequence;uninterruptedHouse.checksum=buddyHouse.checksum;
    uninterruptedAmbition.sequence=buddyAmbition.sequence;uninterruptedAmbition.checksum=buddyAmbition.checksum;
    uninterruptedLegacy.sequence=buddyLegacy.sequence;uninterruptedLegacy.checksum=buddyLegacy.checksum;
    assert(!memcmp(&buddyHouse,&uninterruptedHouse,sizeof(buddyHouse)));
    assert(!memcmp(&buddyAmbition,&uninterruptedAmbition,sizeof(buddyAmbition)));
    assert(!memcmp(&buddyLegacy,&uninterruptedLegacy,sizeof(buddyLegacy)));
  }
  // Blank stopped RTC is seeded; existing/corrupt history never is.
  reset();rtcOscillator=false;loadOrCreateBuddy(0);
  assert(introRequired && buddyNow() && !clockFault && rtcSetCalls==1);
  reset();loadOrCreateBuddy(0);
  assert(introRequired && buddyNow()==rawEpoch && rtcSetCalls==0);
  reset();rawEpoch=0;loadOrCreateBuddy(0);
  assert(buddyNow() && !clockFault && rtcSetCalls==1);
  reset();rtcOscillator=false;rtcSetGood=false;loadOrCreateBuddy(0);
  assert(!buddyNow() && clockFault && rtcSetCalls==1);
  assert(!buddyNow() && rtcSetCalls==1);
  ticks=30001;rtcSetGood=true;assert(buddyNow() && !clockFault && rtcSetCalls==2);
  reset();rtcGood=false;loadOrCreateBuddy(0);
  assert(!buddyNow() && clockFault && rtcSetCalls==0);
  reset();rtcOscillator=false;loadOrCreateBuddy(0);
  uint8_t orphan=1;preferences.putBytes("site-a",&orphan,1);
  assert(!buddyNow() && clockFault && rtcSetCalls==0);
  for (uint8_t object = 0; object < OBJECT_COUNT; ++object) {
    start();
    buddy.generation=2;buddy.birthEpoch=rawEpoch-20UL*86400UL;
    buddy.eventCount=0;buddy.inheritedObject=OBJECT_COUNT;ambitionLocked=true;
    buddyLegacy={};buddyLegacy.magic=BUDDY_LEGACY_MAGIC;buddyLegacy.version=BUDDY_LEGACY_VERSION;
    buddyLegacy.condition[object]=72;buddyLegacy.makerGeneration[object]=1;
    buddyLegacy.lastCareDay[object]=UINT32_MAX;
    buddy.possessions=1<<object;buddy.affection[object]=900;
    buddy.traits.curiosity=buddy.traits.sensitivity=buddy.traits.sociability=buddy.traits.playfulness=95;
    static const BuddyMoment moments[]={MOMENT_POLISH,MOMENT_WATER,MOMENT_TIDY,MOMENT_MEND,
      MOMENT_NATURE,MOMENT_SMELL_FLOWERS,MOMENT_JOURNAL,MOMENT_MUSIC};
    visibleMoment=moments[object];visibleBehavior=behaviorForMoment(visibleMoment);
    assert(buddyLegacyInvitation(buddy,buddyLegacy,buddyNow())==object);
    assert(buddyMomentText() && strlen(buddyMomentText())>8);
    currentView=VIEW_BUDDY;renderBuddy(true);
  }
  start();sdAvailable=false;needsRender=false;
  uint32_t death=buddy.birthEpoch+(uint32_t)buddyLifespanDays(buddy.seed)*86400UL;
  buddy.lastUpdateEpoch=death-3600;buddy.lastWorldEpoch=buddy.lastUpdateEpoch;
  uint16_t generation=buddy.generation;
  updateBuddySimulation(death);
  assert(!buddy.alive && needsRender && !buddy.memorialShown);
  renderBuddy(true);assert(buddy.memorialShown);
  updateBuddySimulation(death+86401);
  assert(buddy.alive && buddy.generation==generation+1);

  for(uint32_t age: {346UL,347UL,400UL}) {
    start();buddy.seed=0x5c95d946UL;
    buddy.lastUpdateEpoch=buddy.birthEpoch+age*86400UL;
    buddy.lastWorldEpoch=buddy.lastUpdateEpoch;
    BuddySaveV2 old=oldV2Save(buddy,41);
    nvs.clear();preferences.putBytes("life-a",&old,sizeof(old));
    buddy={};activeSaveSlot=-1;saveSequence=0;loadOrCreateBuddy(0);
    uint32_t now=buddy.lastUpdateEpoch+7200;
    updateBuddySimulation(now);
    assert(!buddy.alive && buddy.lastUpdateEpoch<=now);
    BuddySaveV2 rollback={};
    assert(preferences.getBytes("life-a",&rollback,sizeof(rollback))==sizeof(rollback));
    assert(validSaveV2(rollback)); // retained even after simulation/storage writes
    buddy={};loadOrCreateBuddy(0);saveBuddy();assert(migrationProtectedLifeSlot==0);
    assert(preferences.getBytesLength("life-a")==sizeof(BuddySaveV2));
    releaseBuddyMigrationProtection();saveBuddy();
    assert(preferences.getBytesLength("life-a")==sizeof(BuddySave));
  }

  start();uint32_t origin=buddyNow();rtcSetGood=false;
  for(int second=1;second<=125;++second){ticks=second*1000UL;buddyNow();}
  assert(clockFault && clockFaultReason==CLOCK_REASON_BACKWARD);
  assert(buddy.lastUpdateEpoch==origin);
  start();origin=buddyNow();rtcSetGood=false;
  for(int second=1;second<=4;++second){ticks=second*1000UL;rawEpoch+=60;buddyNow();}
  assert(clockFault && clockFaultReason==CLOCK_REASON_FORWARD);
  assert(buddy.lastUpdateEpoch==origin);
  start();origin=buddyNow();
  for(int second=1;second<=600;++second){ticks=second*1000UL;rawEpoch=origin+second;assert(buddyNow()==rawEpoch&&!clockFault);}

  for(uint8_t object=OBJECT_GROVE;object<OBJECT_COUNT;++object) {
    start();buddy.possessions=1<<object;buddy.eventCount=buddy.eventHead=0;
    static const LifeEvent events[]={EVENT_NATURE,EVENT_FLOWERS,EVENT_JOURNAL,EVENT_MUSIC};
    uint32_t now=rawEpoch+3UL*86400UL;
    for(uint8_t day=1;day<=3;++day) {
      WorldMemory &event=buddy.events[buddy.eventHead++];
      event.epoch=rawEpoch+day*86400UL;event.object=object;event.event=events[object-OBJECT_GROVE];
      ++buddy.eventCount;
    }
    assert(buddyRoutineObject(now,nullptr)==object);
  }

  // Cut each durable completion write, then reboot into the real loaders.
  for(int cut=0;cut<=4;++cut) {
    start();setupBuddyAmbition(rawEpoch);
    buddy.mood=500;buddy.affection[OBJECT_PLANT]=100;
    buddyAmbition.kind=OBJECT_PLANT;buddyAmbition.startedEpoch=rawEpoch;
    buddyAmbition.progress=95;saveBuddyAmbition();saveBuddy();
    uint32_t epoch=rawEpoch+3600;
    failNvsAfter=cut;
    endBuddyAmbition(epoch,AMBITION_FULFILLED);
    failNvsAfter=-1;buddy={};buddyAmbition={};buddyLegacy={};
    activeSaveSlot=activeAmbitionSlot=activeLegacySlot=-1;
    saveSequence=ambitionSequence=legacySequence=0;
    buddyCompletionPending=buddyCompletionApplying=buddyCompletionPreparing=false;
    saveFailed=ambitionSaveFailed=legacySaveFailed=false;
    loadOrCreateBuddy(0);setupBuddyAmbition(epoch);setupBuddyLegacy(epoch);
    assert(recoverBuddyCompletion());
    if(cut==0) assert(buddyAmbition.kind==OBJECT_PLANT && buddy.mood==500);
    else {
      assert(buddyAmbition.lastOutcome==AMBITION_FULFILLED && buddy.mood==570);
      assert(buddy.affection[OBJECT_PLANT]==190);
      assert(buddyLegacy.makerGeneration[OBJECT_PLANT]==buddy.generation);
      uint8_t count=buddy.eventCount;
      assert(recoverBuddyCompletion() && buddy.mood==570 && buddy.eventCount==count);
    }
    assert(!preferences.getBytesLength("aim-commit"));
  }
  start();setupBuddyAmbition(rawEpoch);buddyAmbition.kind=OBJECT_PLANT;
  buddyAmbition.startedEpoch=rawEpoch;saveBuddyAmbition();saveBuddy();
  failNvsRemove=true;endBuddyAmbition(rawEpoch+3600,AMBITION_FULFILLED);
  assert(buddyCompletionPending);uint16_t mood=buddy.mood;
  failNvsRemove=false;assert(recoverBuddyCompletion());assert(buddy.mood==mood);
  start();setupBuddyAmbition(rawEpoch);buddyAmbition.kind=OBJECT_PLANT;
  buddyAmbition.startedEpoch=rawEpoch;saveBuddyAmbition();saveBuddy();
  failNvsRemove=true;endBuddyAmbition(rawEpoch+3600,AMBITION_FULFILLED);
  assert(buddyCompletionPending);
  uint32_t identity=buddy.seed;
  nvs["buddy-life/aim-commit"].back()^=1;
  buddy={};buddyCompletionPending=false;loadOrCreateBuddy(0);
  assert(storageLocked && buddy.seed==identity); // corrupt journal never rerolls

  start();sdAvailable=true;buddy.lastLoggedDay=UINT32_MAX;
  char path[96];lifePath(path,sizeof(path),"daily.csv");
  files[path]=std::string(128UL*1024UL+1,'x');
  appendDailyJournal(rawEpoch);
  assert(!sdAvailable && buddy.lastLoggedDay==UINT32_MAX && buddy.alive);

  BuddyListeningWindow window;
  buddyListeningTouch(window,UINT32_MAX-30000);
  assert(buddyListeningDue(window,window.deadlineMs));
  assert(!buddyListeningGraceExpired(window,window.deadlineMs+2999));
  assert(buddyListeningGraceExpired(window,window.deadlineMs+3000));
  BuddyCableRestartNotice notice={};
  buddyCableNoticeSet(notice,CABLE_ERROR_UART);
  assert(buddyCableNoticeTake(notice,true,CABLE_ERROR_UART)==CABLE_ERROR_UART);

  std::vector<uint8_t> image(64,0);image[0]=0xE9;image[12]=9;
  const char *tag=BUDDY_IMAGE_TAG;
  image.insert(image.end(),tag,tag+strlen(tag)+1);
  for(size_t split=0;split<=image.size();++split) {
    BuddyUpdateImageCheck check(BUDDY_FIRMWARE_VERSION);
    check.feed(image.data(),split);check.feed(image.data()+split,image.size()-split);
    assert(check.valid());
  }
  BuddyUpdateImageCheck wrong(BUDDY_FIRMWARE_VERSION+1);
  wrong.feed(image.data(),image.size());assert(!wrong.valid());
  image.pop_back();BuddyUpdateImageCheck incomplete(BUDDY_FIRMWARE_VERSION);
  incomplete.feed(image.data(),image.size());assert(!incomplete.valid());
  puts("PASS: audit regressions: eight captions, unattended memorial/succession, migrated lifespan, rollback checkpoint, RTC drift, new routines, completion power cuts, noise grace and image/header binding");
  fakeRandomState=randomBefore;
}
