// Host checks for the advertising log (BTLEAdvAdvLog.hpp). Commands only; no controller.
#include <BTLEAdvAdvLog.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace BTLEAdv;

int main() {
    unsigned cases = 0;
    static AdvLog log{};
    // LE Set Extended Advertising Parameters, handle 2: legacy connectable (0x13), 30 ms both ways (48 units),
    // channels 37-39, TX power 127 (no preference), 1M PHY.
    uint8_t params[3 + 25] = {0x36, 0x20, 25, 2, 0x13, 0x00, 48, 0, 0, 48, 0, 0, 0x07, 1, 0,
                              0, 0, 0, 0, 0, 0, 0, 127, 1, 0, 1, 0, 0};
    log.onCommand(params, sizeof(params), 1000);
    assert(log.paramsSeen == 1 && log.sets[2].known);
    assert(log.sets[2].intervalMin == 48 && log.sets[2].intervalMax == 48);
    assert(log.sets[2].channelMap == 7 && log.sets[2].txPower == 127 && log.sets[2].primaryPhy == 1);
    assert(log.ring[0].kind == 'P' && log.ring[0].handle == 2 && log.ring[0].intervalMs10 == 300);
    cases += 4;
    // Enable sets 0, 2 and 3 with no duration.
    const uint8_t enable[3 + 2 + 12] = {0x39, 0x20, 14, 1, 3, 0, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0};
    log.onCommand(enable, sizeof(enable), 1001);
    assert(log.enablesSeen == 1 && log.sets[2].enabled && log.sets[2].enables == 1);
    assert(log.ring[1].kind == 'E' && log.ring[1].handle == 0 && log.ring[3].handle == 3);
    cases += 2;
    // New parameters keep the set's enable state and counts.
    params[9] = 0x40; params[10] = 0x06;   // max interval 1600 units = 1 s
    log.onCommand(params, sizeof(params), 2000);
    assert(log.sets[2].enabled && log.sets[2].enables == 1 && log.sets[2].intervalMax == 0x640);
    assert(log.ring[4].intervalMs10 == 10000);
    cases += 2;
    // Disable all sets (0 sets).
    const uint8_t disableAll[5] = {0x39, 0x20, 2, 0, 0};
    log.onCommand(disableAll, sizeof(disableAll), 3000);
    assert(!log.sets[2].enabled && log.sets[2].disables == 1 && log.ring[5].kind == 'D' && log.ring[5].handle == 0xff);
    cases += 1;
    // Truncated commands are counted, not read past their end; other opcodes are ignored.
    const uint8_t shortParams[3 + 10] = {0x36, 0x20, 25, 1};
    log.onCommand(shortParams, sizeof(shortParams), 4000);
    const uint8_t shortEnable[3 + 4] = {0x39, 0x20, 6, 1, 2, 5, 0};
    log.onCommand(shortEnable, sizeof(shortEnable), 4001);
    const uint8_t other[4] = {0x0c, 0x20, 1, 0};
    log.onCommand(other, sizeof(other), 4002);
    log.onCommand(nullptr, 0, 4003);
    assert(log.malformed == 2 && log.paramsSeen == 2 && log.enablesSeen == 2);
    cases += 1;
    // The ring wraps.
    for (unsigned i = 0; i < kAdvLogRing + 5; ++i)
        log.onCommand(params, sizeof(params), 5000 + i);
    assert(log.ringNext > kAdvLogRing && log.paramsSeen == 2 + kAdvLogRing + 5);
    cases += 1;
    printf("btleadv_advlog: %u cases passed\n", cases);
    return 0;
}
