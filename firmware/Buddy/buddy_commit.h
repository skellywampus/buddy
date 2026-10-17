#pragma once
// A short-lived write-ahead afterimage, not a change to any existing save ABI.
// It survives reset between life/ambition/site/house writes and is replayed exactly.
struct BuddyCompletionCommit {
  uint32_t magic, checksum;
  BuddyState life;
  BuddyAmbitionState ambition;
  BuddyLegacyState legacy;
};
static bool buddyCompletionPending = false;
static bool buddyCompletionPreparing = false;
static bool buddyCompletionApplying = false;
// Complete an hourly step before publishing any changed side-record afterimage.
// Ordinary progress and completed ambitions share the same journal. The original
// commit remains readable for updates from 2026101301.
struct BuddyCompletionStepCommit {
  BuddyCompletionCommit base;
  BuddyHouseState house;
};
static bool buddySimulationStepActive = false;
static bool buddySimulationStepCompleted = false;
static uint32_t buddyCompletionHash(const BuddyCompletionCommit &record) {
  BuddyCompletionCommit copy = record;
  copy.checksum = 0;
  uint32_t hash = 2166136261UL;
  const uint8_t *bytes = (const uint8_t *)&copy;
  for (size_t i = 0; i < sizeof(copy); ++i) hash = (hash ^ bytes[i]) * 16777619UL;
  return hash;
}
bool persistBuddyCompletion();
bool readBuddyCompletion(BuddyCompletionCommit &record, BuddyHouseState &house, bool &hasHouse);
bool recoverBuddyCompletion();
void releaseBuddyMigrationProtection();
