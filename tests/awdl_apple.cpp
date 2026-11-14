// Host checks for the Apple-style schedule: planned sync TLV, own-away send gate, TLV 18 plan.
#include <assert.h>
#include <stdio.h>
#include <AWDLLocalSchedule.hpp>
#include <AWDLPeerSchedule.hpp>
#include <AWDLPeerProbe.hpp>
using namespace ItlAWDL;
int main() {
    // Planned sync TLV: adopted counter and down counter, per-entry channels.
    SyncPrefix s = {44,44,0,0,16,110,0x1000,16,16,0,3,3,3,3,{2,2,3,4,5,6}};
    uint8_t ch[16]; for (unsigned i = 0; i < 16; ++i) ch[i] = (i == 6 || i == 7 || i == 14 || i == 15) ? 36 : 44;
    uint8_t out[73]; size_t n = 0;
    assert(buildLocalPlannedSchedule(s, 0x1234, 40, ch, out, sizeof(out), &n) == BuildResult::Ready && n == 73);
    assert(out[29] == 0x34 && out[30] == 0x12);
    for (unsigned i = 0; i < 16; ++i) assert(out[40 + 2*i] == ch[i] && out[39 + 2*i] == 0x1b);
    assert(buildLocalPlannedSchedule(s, 1, 0, ch, out, sizeof(out), &n) == BuildResult::Invalid);   // down 0
    assert(buildLocalPlannedSchedule(s, 1, 65, ch, out, sizeof(out), &n) == BuildResult::Invalid);  // > 4 AWs
    uint8_t bad[16]; for (unsigned i = 0; i < 16; ++i) bad[i] = 52;  // radar channel can't be described
    assert(buildLocalPlannedSchedule(s, 1, 10, bad, out, sizeof(out), &n) == BuildResult::Invalid);

    // Own-away entries pause sending but not calibration; they survive reset().
    PeerSchedule ps{}; ps.assumeZero = true;
    PeerSyncInfo info{}; info.awPeriodTU = 16; info.downCounterTU = 64; info.awCounter = 0;
    for (unsigned i = 0; i < 16; ++i) info.channels[i] = 44;
    ps.note(info, 1000000, 44);
    assert(ps.presentInEntry(6) && ps.presentForSend(6));
    ps.ownAway[6] = 1;
    assert(ps.presentInEntry(6) && !ps.presentForSend(6) && ps.presentForSend(5));
    ps.reset(); assert(ps.ownAway[6] == 1 && ps.assumeZero);

    // TLV 18 with a plan.
    uint8_t tlvs[512] = {24, 28, 0}; size_t len = 31;   // a minimal election v2 TLV
    uint8_t cls[16]; for (unsigned i = 0; i < 16; ++i) cls[i] = ch[i] == 36 ? 115 : 128;
    const uint8_t addr[6] = {2, 0, 0, 0, 0, 1};
    assert(extendWithPeerTLVs(tlvs, &len, sizeof(tlvs), addr, 44, 128, false, ch, cls)); {
        for (unsigned i = 0; i < 16; ++i) assert(tlvs[24 + 9 + 2*i] == ch[i] && tlvs[24 + 10 + 2*i] == cls[i]);
    }
    printf("AWDL Apple-style schedule: passed (host only)\n");
    return 0;
}
