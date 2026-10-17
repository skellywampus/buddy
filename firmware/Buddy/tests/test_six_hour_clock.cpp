// Fault-injected persistence tests for the disposable six-hour clock.
#include <cassert>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "../buddy_types.h"
#include "../buddy_test_clock.h"

static std::map<std::string, std::vector<uint8_t>> flash;
static bool failWrite = false;
class Preferences {
  std::string space;
 public:
  bool begin(const char *name, bool) { space = name; return true; }
  void end() {}
  size_t getBytesLength(const char *key) {
    auto it = flash.find(space + "/" + key);
    return it == flash.end() ? 0 : it->second.size();
  }
  size_t getBytes(const char *key, void *out, size_t size) {
    auto it = flash.find(space + "/" + key);
    if (it == flash.end() || size < it->second.size()) return 0;
    memcpy(out, it->second.data(), it->second.size());
    return it->second.size();
  }
  size_t putBytes(const char *key, const void *data, size_t size) {
    if (failWrite) return 0;
    const uint8_t *first = (const uint8_t *)data;
    flash[space + "/" + key] = std::vector<uint8_t>(first, first + size);
    return size;
  }
};
struct FakeSerial {
  void printf(const char *, ...) {}
} Serial;
static BuddyState buddy = {};
static bool storageLocked = false, clockFault = false;
#include "../buddy_test_clock.inc"
static uint32_t buddyClockFloor() { return buddy.lastUpdateEpoch; }

static void reboot() {
  testLifeTime = {};
  testClockSequence = 0;
  testClockSlot = -1;
  testClockReady = false;
  storageLocked = clockFault = false;
}

int main() {
  Preferences life;
  life.begin("buddy-life", false);
  uint32_t original = 0x12345678UL;
  assert(life.putBytes("life-a", &original, sizeof(original)) == sizeof(original));
  assert(!buddyTestStoragePreflight(life)); // cannot adopt a production life
  assert(!life.getBytesLength("test-mode"));
  flash.clear();
  assert(buddyTestStoragePreflight(life));
  assert(buddyTestStoragePreflight(life));
  flash["buddy-life/test-mode"][0] ^= 1;
  assert(!buddyTestStoragePreflight(life)); // corrupt marker fails closed
  flash.clear();
  assert(buddyTestStoragePreflight(life));

  const uint32_t firstReal = 800000000UL;
  buddy.magic = BUDDY_MAGIC;
  buddy.generation = 1;
  buddy.seed = 0x1234abcdUL;
  buddy.birthEpoch = buddy.lastUpdateEpoch = firstReal;
  assert(buddyTestClockLoad()); // recover first birth before its clock commit
  uint32_t simulated = 0;
  assert(buddyAcceleratedNow(testLifeTime,
    firstReal + BUDDY_TEST_REAL_LIFE_SECONDS, simulated));
  assert(simulated == firstReal +
    (uint32_t)buddyLifespanDays(buddy.seed) * 86400UL);
  uint32_t oldSeed = buddy.seed;
  reboot();
  assert(buddyTestClockLoad() && testLifeTime.seed == oldSeed);
  assert(buddyTestPhysicalFloor() == firstReal);

  // Life save commits first, clock rebase second. Simulate power loss in that
  // window: the old A/B clock reconstructs the successor's real birth.
  uint32_t nextReal = firstReal + BUDDY_TEST_REAL_LIFE_SECONDS + 180UL;
  uint32_t nextSim = 0;
  assert(buddyAcceleratedNow(testLifeTime, nextReal, nextSim));
  buddy.generation = 2;
  buddy.seed = 0x98765432UL;
  buddy.birthEpoch = buddy.lastUpdateEpoch = nextSim;
  reboot();
  assert(buddyTestClockLoad());
  assert(testLifeTime.generation == 2 && testLifeTime.realBirth == nextReal);
  assert(testLifeTime.simulatedBirth == nextSim);
  uint32_t secondSeed = buddy.seed;
  reboot();
  assert(buddyTestClockLoad() && testLifeTime.seed == secondSeed);

  // A failed rebase write does not silently switch to the new rate.
  nextReal += BUDDY_TEST_REAL_LIFE_SECONDS + 180UL;
  assert(buddyAcceleratedNow(testLifeTime, nextReal, nextSim));
  buddy.generation = 3;
  buddy.seed = 0x10203040UL;
  buddy.birthEpoch = buddy.lastUpdateEpoch = nextSim;
  failWrite = true;
  assert(!buddyTestClockBindLife());
  failWrite = false;
  reboot();
  assert(buddyTestClockLoad() && testLifeTime.generation == 3 &&
         testLifeTime.realBirth == nextReal);
  uint32_t thirdSeed = buddy.seed;
  reboot();
  assert(buddyTestClockLoad() && testLifeTime.seed == thirdSeed);

  // Record validation catches corruption; no arbitrary new life is made.
  flash["buddy-test/clock-a"].clear();
  flash["buddy-test/clock-b"].clear();
  reboot();
  assert(!buddyTestClockLoad());
}
