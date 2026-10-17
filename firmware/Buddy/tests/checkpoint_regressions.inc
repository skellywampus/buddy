// Compare real save/load recovery against uninterrupted ordinary simulation.
static void rebootCheckpointTest(uint32_t now) {
  buddy={};buddyHouse={};buddyAmbition={};buddyLegacy={};
  activeSaveSlot=activeHouseSlot=activeAmbitionSlot=activeLegacySlot=-1;
  saveSequence=houseSequence=ambitionSequence=legacySequence=0;
  saveFailed=houseSaveFailed=ambitionSaveFailed=legacySaveFailed=false;
  buddyCompletionPending=buddyCompletionPreparing=buddyCompletionApplying=false;
  buddySimulationStepActive=buddySimulationStepCompleted=false;
  loadOrCreateBuddy(0);setupBuddyHouse(now);setupBuddyAmbition(now);setupBuddyLegacy(now);
  assert(recoverBuddyCompletion());
}
static void testOrdinaryCheckpointRecovery() {
  uint32_t randomBefore=fakeRandomState;
  for(unsigned scenario=0;scenario<4;++scenario) {
    start();buddy.birthEpoch=rawEpoch-20UL*86400UL;buddy.currentStage=STAGE_CHILD;
    buddy.possessions=(1<<OBJECT_NOOK)|(1<<OBJECT_STONE)|(1<<OBJECT_FLOWERS);
    buddy.visits[OBJECT_NOOK]=buddy.visits[OBJECT_FLOWERS]=10;
    buddy.affection[OBJECT_FLOWERS]=500;
    buddyHouse.phase=scenario==1 ? HOUSE_BUILDING : HOUSE_OCCUPIED;
    buddyHouse.work=scenario==1 ? 98 : BUDDY_HOUSE_WORK_TARGET;
    buddyHouse.condition=scenario==1 ? 0 : scenario==2 ? 1 : 600;
    buddyHouse.builtEpoch=scenario==1 ? 0 : rawEpoch-86400;
    buddyHouse.builderGeneration=scenario==1 ? 0 : buddy.generation;saveBuddyHouse();
    if(scenario==3) {
      buddyLegacy.condition[OBJECT_FLOWERS]=1;
      buddyLegacy.makerGeneration[OBJECT_FLOWERS]=buddy.generation;
      buddyLegacy.lastWeatherDay=rawEpoch/86400UL-6;
      buddyLegacy.lastCareDay[OBJECT_FLOWERS]=buddyLegacy.lastWeatherDay;
      saveBuddyLegacy();
    }
    preferences.remove("aim-a");preferences.remove("aim-b");setupBuddyAmbition(rawEpoch);
    buddyAmbition.kind=OBJECT_NOOK;buddyAmbition.startedEpoch=rawEpoch;
    buddyAmbition.lastWorkDay=rawEpoch/86400UL;buddyAmbition.progress=0;
    saveBuddyAmbition();saveBuddy();latestClimateValid=false;
    auto initial=nvs;uint32_t target=rawEpoch+48UL*3600UL;
    updateBuddySimulation(target);
    assert(!buddyCompletionPending&&!buddyAmbition.lastEndedEpoch);
    BuddyState expectedLife=buddy;BuddyHouseState expectedHouse=buddyHouse;
    BuddyAmbitionState expectedAim=buddyAmbition;BuddyLegacyState expectedSite=buddyLegacy;
    if(scenario==1) assert(expectedHouse.work>98 && expectedAim.progress>0);
    if(scenario==2) assert(expectedHouse.phase==HOUSE_RUIN);
    if(scenario==3) assert(!expectedSite.condition[OBJECT_FLOWERS]);
    // Cuts include journal creation and each subsequent life/aim/site/home
    // write, across several ordinary (not completed-project) transactions.
    for(int cut=0;cut<=24;++cut) {
      nvs=initial;rebootCheckpointTest(rawEpoch);
      failNvsAfter=cut;updateBuddySimulation(target);failNvsAfter=-1;
      rebootCheckpointTest(rawEpoch);updateBuddySimulation(target);
      assert(!buddyCompletionPending&&!storageLocked);
      assert(!memcmp(&buddy,&expectedLife,sizeof(buddy)));
      BuddyHouseState house=buddyHouse;house.sequence=expectedHouse.sequence;house.checksum=expectedHouse.checksum;
      BuddyAmbitionState aim=buddyAmbition;aim.sequence=expectedAim.sequence;aim.checksum=expectedAim.checksum;
      BuddyLegacyState site=buddyLegacy;site.sequence=expectedSite.sequence;site.checksum=expectedSite.checksum;
      assert(!memcmp(&house,&expectedHouse,sizeof(house)));
      assert(!memcmp(&aim,&expectedAim,sizeof(aim)));
      assert(!memcmp(&site,&expectedSite,sizeof(site)));
    }
    // A durable journal whose removal fails is safe to replay repeatedly.
    nvs=initial;rebootCheckpointTest(rawEpoch);failNvsRemove=true;
    updateBuddySimulation(target);assert(buddyCompletionPending);
    failNvsRemove=false;rebootCheckpointTest(rawEpoch);
    assert(recoverBuddyCompletion());updateBuddySimulation(target);
    assert(!memcmp(&buddy,&expectedLife,sizeof(buddy)));
  }
  // No side change means no extra journal/replay writes for an ordinary hour.
  start();buddyAmbition.kind=OBJECT_PLANT;buddyAmbition.startedEpoch=rawEpoch;
  buddyAmbition.lastWorkDay=rawEpoch/86400UL;saveBuddyAmbition();
  buddy.lastWorldEpoch=rawEpoch+3600;saveBuddy();
  uint32_t beforeSequence=saveSequence;
  updateBuddySimulation(rawEpoch+3600);
  assert(saveSequence==beforeSequence+1&&!buddyCompletionPending);
  fakeRandomState=randomBefore;
  puts("PASS: ordinary house wear/build/collapse, unfinished ambition progress and site loss survive checkpoint cuts with identical life and world state");
}
