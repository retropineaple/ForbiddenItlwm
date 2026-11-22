// -itlawdlslothop's planner on the plan the phone announced on 2026-11-22, and the hop-set behaviour of PeerSchedule.
#include <AWDLPeerProbe.hpp>
#include <AWDLSlotHop.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace ItlAWDL;
int main()
{
    // 80 MHz blocks: 149 153 157 161 are one, 36 40 44 48 another; 6, 165, 0 and a stray 150 are none.
    assert(block80Of(149) == 149 && block80Of(161) == 149 && block80Of(157) == 149 && block80Of(36) == 36 && block80Of(48) == 36);
    assert(block80Of(6) == 0 && block80Of(165) == 0 && block80Of(0) == 0 && block80Of(150) == 0 && block80Of(100) == 100);
    const uint8_t pair[2] = {149, 161}, cross[2] = {149, 44}, one[1] = {149};
    assert(slotHopSetValid(pair, 2) && !slotHopSetValid(cross, 2) && !slotHopSetValid(one, 1) && !slotHopSetValid(pair, 0));

    // The announced plan; entry k is the extended AW (eaw + 0) % 16, an extended AW is 4 x 16 TU = 65,536 us.
    const uint8_t plan[16] = {149, 149, 149, 149, 161, 161, 161, 161, 6, 149, 149, 149, 161, 161, 161, 161};
    PeerSchedule s{};
    s.valid = true; s.heardUS = 1000; s.eawUS = 65536; s.anchorEaw = 15; s.anchorEndUS = 1000 + 65536;   // eaw 16 starts at anchorEnd
    memcpy(s.channels, plan, 16);
    s.ourChannel = 161; s.assumeZero = true;
    assert(s.known());
    // Fixed channel (no hop set): entries on 161 are present, the rest away.
    assert(s.presentEntries() == 8 && s.calibrated == false);
    s.hopSet[0] = 149; s.hopSet[1] = 161; s.hopCount = 2;
    assert(s.presentEntries() == 15);                         // the run on 6 is the only entry outside the set
    assert(s.onHopSide(149) && s.onHopSide(161) && !s.onHopSide(6) && !s.onHopSide(0));
    s.reset();
    assert(s.hopCount == 2 && s.hopSet[0] == 149 && s.hopSet[1] == 161 && s.assumeZero);   // policy survives a reset

    // The choice for each entry. eaw 16 is entry 0 (149) from t = 1000 + 65536 to + 65536; leadUS 3000, minRun 40 ms.
    s.valid = true; s.heardUS = 1000; s.eawUS = 65536; s.anchorEaw = 15; s.anchorEndUS = 1000 + 65536; memcpy(s.channels, plan, 16); s.ourChannel = 161;
    const uint64_t eaw16 = 1000 + 65536;
    auto at = [&](unsigned entry, uint64_t into) { return eaw16 + uint64_t(entry) * 65536 + into; };   // a time inside entry `entry` (eaw 16 + entry)
    SlotHopChoice c = chooseSlotHop(s, at(0, 10000), pair, 2, 161, 3000, 40000);
    assert(c.follows && c.channel == 149 && c.holdUS == 4 * 65536 - 13000);        // entry 0 to 3 are 149: 4 entries less 13 ms in
    c = chooseSlotHop(s, at(4, 10000), pair, 2, 149, 3000, 40000);
    assert(c.follows && c.channel == 161 && c.holdUS == 4 * 65536 - 13000);        // 4 to 7
    // Entry 8 is channel 6: outside the set, so it stays where it is.
    c = chooseSlotHop(s, at(8, 10000), pair, 2, 161, 3000, 40000);
    assert(!c.follows && c.channel == 161);
    c = chooseSlotHop(s, at(8, 10000), pair, 2, 149, 3000, 40000);
    assert(!c.follows && c.channel == 149);
    // A run too short to pay for a hop: 3 ms before the end of the 149 run, landing in 161's. The choice is for at + lead.
    c = chooseSlotHop(s, at(3, 65536 - 2000), pair, 2, 149, 3000, 40000);
    assert(c.channel == 161 && c.follows && c.holdUS > 40000);                       // lands in entry 4: a long run, hop
    c = chooseSlotHop(s, at(7, 65536 - 2000), pair, 2, 161, 3000, 40000);
    assert(!c.follows && c.channel == 161);                                       // lands in entry 8, channel 6
    c = chooseSlotHop(s, at(11, 65536 - 2000), pair, 2, 149, 3000, 40000);
    assert(c.channel == 161 && c.follows);
    c = chooseSlotHop(s, at(9, 10000), pair, 2, 161, 3000, 300000);               // minRun longer than the 3 entries: stay
    assert(c.channel == 161 && c.follows && c.holdUS < 300000);
    // Wraps: entry 15 is 161 and entry 0 of the next sequence is 149.
    c = chooseSlotHop(s, at(15, 10000), pair, 2, 161, 3000, 40000);
    assert(c.channel == 161 && c.holdUS == 65536 - 13000);   // entry 15 to its end: entry 16 is 149 again
    // Nothing known: stay. Stale: stay. Empty set: stay.
    PeerSchedule none{}; assert(chooseSlotHop(none, 0, pair, 2, 161, 3000, 40000).channel == 161);
    assert(chooseSlotHop(s, at(0, 0) + 5000000, pair, 2, 161, 3000, 40000).follows == false);
    assert(chooseSlotHop(s, at(0, 10000), pair, 0, 161, 3000, 40000).channel == 161);


    // The announced sequence follows the plan: an election TLV in a template, the peer TLVs added, then the sixteen entries rewritten.
    {
        uint8_t tlvs[256] = {};
        size_t used = 0;
        tlvs[0] = 24; tlvs[1] = 28; tlvs[2] = 0; used = 3 + 28;                       // the template's election v2 TLV
        const uint8_t me[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
        assert(extendWithPeerTLVs(tlvs, &used, sizeof(tlvs), me, 161, 128, false));
        const size_t before = used;
        uint8_t slots[16], classes[16];
        for (unsigned i = 0; i < 16; ++i) { slots[i] = plan[i] == 6 ? 0 : plan[i]; classes[i] = 128; }   // channel 6 is not ours to claim
        assert(applySlotChannels(tlvs, used, slots, classes) && used == before);
        // TLV 5 is 24 bytes, then TLV 18: type, length 41, header of 6, then entries (channel, class).
        assert(tlvs[24] == 18 && tlvs[25] == 41 && tlvs[26] == 0);
        const uint8_t *e = tlvs + 24 + 3 + 6;
        for (unsigned i = 0; i < 16; ++i) {
            const uint8_t want = plan[i] == 6 ? 161 : plan[i];                        // kept as extend wrote it (161) where 0
            assert(e[2 * i] == want && e[2 * i + 1] == 128);
        }
        // Not a TLV 18 of the expected shape: nothing written.
        uint8_t odd[3 + 10] = {18, 10, 0};
        assert(!applySlotChannels(odd, sizeof(odd), slots, classes));
        assert(!applySlotChannels(nullptr, 0, slots, classes) && !applySlotChannels(tlvs, 5, slots, classes));
    }

    // Stats: hops are counted, and slow ones stop the hopping once most of 20 are slow; two failures stop it too.
    SlotHopStats st;
    for (unsigned i = 0; i < 19; ++i) st.noteHop(3000);
    assert(!st.disabled && st.averageUS() == 3000 && st.latencyMinUS == 3000 && st.latencyMaxUS == 3000);
    SlotHopStats slow;
    for (unsigned i = 0; i < 19; ++i) slow.noteHop(40000);
    assert(!slow.disabled);                                  // fewer than 20 to judge
    slow.noteHop(40000);
    assert(slow.disabled && slow.reason == 2 && slow.slow == 20);
    SlotHopStats bad; bad.noteFailure(); assert(!bad.disabled); bad.noteFailure(); assert(bad.disabled && bad.reason == 1);
    // Hop mode judges presence by the set, not by the channel the radio is on right now: a phone on the other hop channel is not "away"
    // (no pause, no gate, no skipped ask), a phone on 6 still is, and without a hop set the old fixed-channel rule stands.
    {
        PeerSchedule h{};
        h.valid = true; h.heardUS = 1000; h.eawUS = 65536; h.anchorEaw = 15; h.anchorEndUS = 1000 + 65536;
        memcpy(h.channels, plan, 16);
        h.ourChannel = 149; h.assumeZero = true;
        const uint64_t base = 1000 + 65536;
        auto mid = [&](unsigned entry) { return base + uint64_t(entry) * 65536 + 32768; };
        assert(h.shouldPause(mid(4)));                        // fixed channel: the phone is on 161, we are on 149
        assert(h.sendGate(mid(4), false, 8000) == PeerSchedule::SendGate::Away);
        h.hopSet[0] = 149; h.hopSet[1] = 161; h.hopCount = 2;
        assert(!h.shouldPause(mid(4)) && !h.shouldPause(mid(0)));
        assert(h.sendGate(mid(4), false, 8000) == PeerSchedule::SendGate::Open);
        assert(h.shouldPause(mid(8)));                        // entry 8 is channel 6
        assert(h.usUntilPresentAw(mid(8), true) > 0);         // and the next AW to send in is the first one after it
    }
    // Calibration in hop mode: the radio follows the plan (hops at each boundary) and hears the phone every entry it shares; the offset
    // the phone really uses (here 5) must come out. Judged against the whole set it could only use the single entry on 6.
    {
        PeerSchedule c{};
        const uint64_t eaw = 65536, base = 1000 + 65536;
        c.valid = true; c.heardUS = base; c.eawUS = eaw; c.anchorEaw = 15; c.anchorEndUS = base;
        memcpy(c.channels, plan, 16);
        c.hopSet[0] = 149; c.hopSet[1] = 161; c.hopCount = 2;
        const unsigned trueOffset = 5;
        for (unsigned n = 0; n < 1600 && !c.calibrated; ++n) {
            const uint64_t t = base + uint64_t(n) * eaw + 20000;                       // mid-entry
            c.heardUS = t;
            const unsigned eawNo = 16 + n;
            const uint8_t phoneOn = plan[(eawNo + trueOffset) % 16];
            if (phoneOn == 6) continue;                                                // the radio is not on 6: nothing heard
            c.ourChannel = phoneOn;                                                    // it hopped there
            c.notePeerFrame(t);
        }
        assert(c.calibrated && c.offset == trueOffset);
        // And a frame heard on 161 in an entry that names 149 refutes it (strict check), while one on 149 in a 149 entry does not.
        const uint32_t before = c.awayFrames;
        const uint64_t t = base + 40ULL * eaw + 20000;
        c.heardUS = t;
        const uint8_t on = plan[(16 + 40 + trueOffset) % 16];
        c.ourChannel = on == 149 ? 161 : 149;
        c.notePeerFrame(t);
        assert(c.awayFrames == before + 1);
    }
    puts("slot hop planner: passed");
    return 0;
}
