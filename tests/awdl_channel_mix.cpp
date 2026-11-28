// Host checks for the #45 diagnostics (AWDLChannelMix.hpp): macOS's plan with one or two 5 GHz AirDrop
// channels, and nearby devices on 44 and on 149. No firmware or radio.
#include <AWDLChannelMix.hpp>
#include <cassert>
#include <cstdio>
using namespace ItlAWDL;

int main() {
    unsigned cases = 0;
    // The live state-29 plan: Wi-Fi 36, AirDrop 149 in slots 2 and 10, channel 6 in slot 8.
    const uint8_t one[16] = {36, 36, 149, 0, 0, 0, 0, 36, 6, 36, 149, 36, 0, 0, 0, 36};
    PlanFiveGHz p = planFiveGHz(one, 149, 36);
    assert(p.home == 149 && p.homeSlots == 2 && !p.second && !p.secondMask), ++cases;
    // State 8 with a 44-only device nearby: home 149 in 0-5 and 10, the common channel 44 in 9 and 11-13.
    const uint8_t two[16] = {149, 149, 149, 149, 149, 149, 36, 36, 6, 44, 149, 44, 44, 44, 36, 36};
    p = planFiveGHz(two, 149, 36);
    assert(p.home == 149 && p.homeSlots == 7 && p.second == 44 && p.secondSlots == 4), ++cases;
    assert(p.secondMask == ((1u << 9) | (1u << 11) | (1u << 12) | (1u << 13))), ++cases;
    // The Mac's Wi-Fi on 44 is never a second AirDrop channel.
    const uint8_t wifi44[16] = {44, 44, 149, 0, 0, 0, 0, 44, 6, 44, 149, 44, 0, 0, 0, 44};
    p = planFiveGHz(wifi44, 149, 44);
    assert(p.home == 149 && !p.second), ++cases;
    // Wi-Fi and AirDrop sharing 149: home stays 149, nothing second.
    const uint8_t shared[16] = {149, 149, 149, 0, 0, 0, 0, 149, 6, 149, 149, 149, 0, 0, 0, 149};
    p = planFiveGHz(shared, 149, 149);
    assert(p.home == 149 && p.homeSlots == 8 && !p.second), ++cases;
    // No secondary master yet: the most named 5 GHz channel other than Wi-Fi's is home.
    p = planFiveGHz(two, 0, 36);
    assert(p.home == 149 && p.second == 44), ++cases;
    p = planFiveGHz(two, 6, 36);                         // 6 isn't a 5 GHz home
    assert(p.home == 149 && p.second == 44), ++cases;
    // Wi-Fi on 2.4 GHz, AirDrop only on 6: no 5 GHz channel at all.
    const uint8_t none[16] = {6, 6, 6, 0, 0, 0, 0, 6, 6, 6, 6, 6, 0, 0, 0, 6};
    p = planFiveGHz(none, 0, 6);
    assert(!p.home && !p.second), ++cases;

    // PlanMix: counts plans with a second channel and keeps the last one.
    {
        PlanMix m;
        assert(m.note(one, 149, 36, 1000) && m.plans == 1 && !m.withSecond);
        assert(!m.note(one, 149, 36, 2000));                                  // unchanged
        assert(m.note(two, 149, 36, 3000) && m.withSecond == 1 && m.last.second == 44 && m.lastUS == 3000);
        assert(m.note(one, 149, 36, 4000) && m.now.second == 0 && m.last.second == 44 && m.lastPlan[9] == 44);
        assert(m.plans == 4 && m.withSecond == 1);
        (void)m.note(wifi44, 149, 44, 5000);
        assert(m.station == 44 && !m.now.second);                                  // shown as "can't be seen"
        cases += 6;
    }

    // PeerChannelMix: devices on 44 and 149 at once, expiry, replacement, no double counting.
    {
        PeerChannelMix x;
        const uint64_t life = 120000000ULL;
        uint8_t a[6] = {2, 0, 0, 0, 0, 1}, b[6] = {2, 0, 0, 0, 0, 2};
        assert(x.note(a, 149, 1, life));
        PeerChannelMix::Counts c = x.counts(1, life);
        assert(c.on149 == 1 && !c.on44 && !x.mixed && !x.mixedSeen);
        assert(!x.note(a, 149, 2, life));                                     // same device, same channel
        assert(x.note(b, 44, 3, life) && x.mixed && x.mixedSeen == 1 && x.lastMixedUS == 3);
        assert(!x.note(b, 44, 4, life) && x.mixedSeen == 1);                  // still mixed, counted once
        c = x.counts(4, life);
        assert(c.on149 == 1 && c.on44 == 1);
        // a goes quiet; after its two minutes only b counts and the mix ends.
        const uint64_t later = 3 + life + 1;
        assert(x.note(b, 44, later, life) && !x.mixed);
        c = x.counts(later, life);
        assert(c.on44 == 1 && !c.on149);
        // a device that moves from 149 to 44 is one device, not two.
        assert(x.note(a, 149, later + 1, life) && x.mixed && x.mixedSeen == 2);
        assert(x.note(a, 44, later + 2, life) && !x.mixed);
        c = x.counts(later + 2, life);
        assert(c.on44 == 2 && !c.on149 && !c.other);
        cases += 11;
        // nine devices in eight entries: the oldest is replaced, nothing overflows.
        PeerChannelMix y;
        for (uint8_t i = 0; i < 9; ++i) {
            uint8_t d[6] = {2, 0, 0, 0, 1, i};
            y.note(d, i == 8 ? 0 : 149, 10 + i, life);
        }
        c = y.counts(20, life);
        assert(c.on149 == 7 && c.other == 1);
        uint8_t first[6] = {2, 0, 0, 0, 1, 0};
        bool found = false;
        for (const auto &e : y.entries)
            found |= e.used && !memcmp(e.addr, first, 6);
        assert(!found);
        cases += 2;
    }
    // -itlschedsecond: the second channel's runs. A mixed plan (state 8's types on 149 home, 44 common).
    {
        uint8_t plan[16] = {149, 149, 149, 149, 149, 149, 6, 6, 36, 44, 149, 44, 44, 44, 6, 6}, sec[16];
        uint8_t copy[16];
        memcpy(copy, plan, 16);
        assert(secondChannelPlan(plan, 149, 36, 44, 0, 0, sec) == 44);
        assert(!memcmp(plan, copy, 16));                                       // real: the plan isn't changed
        for (unsigned i = 0; i < 16; ++i)
            assert(sec[i] == (plan[i] == 44 ? 44 : 0));
        // never the home or the Wi-Fi channel, never 2.4 GHz; none named: 0 and an empty plan
        assert(!secondChannelPlan(plan, 149, 36, 149, 0, 0, sec) && !secondChannelPlan(plan, 149, 44, 44, 0, 0, sec));
        assert(!secondChannelPlan(plan, 149, 36, 6, 0, 0, sec) && !secondChannelPlan(plan, 149, 36, 0, 0, 0, sec));
        for (unsigned i = 0; i < 16; ++i)
            assert(!sec[i]);
        uint8_t no44[16] = {149, 0, 0, 0, 0, 0, 0, 0, 6, 0, 149, 0, 0, 0, 0, 0};
        assert(!secondChannelPlan(no44, 149, 36, 44, 0, 0, sec));              // named but no entry on it
        cases += 6;
    }
    {
        // simulation: masked entries on the home channel move; others (Wi-Fi, 6, 0) stay.
        uint8_t idle[16] = {36, 36, 149, 0, 0, 0, 0, 36, 6, 36, 149, 36, 0, 0, 0, 36}, sec[16];
        assert(secondChannelPlan(idle, 149, 36, 0, 44, 0x0c00, sec) == 44);
        assert(idle[10] == 44 && idle[11] == 36 && idle[2] == 149 && sec[10] == 44 && !sec[11] && !sec[2]);
        uint8_t busy[16];
        memset(busy, 149, 16);
        busy[8] = 6;
        assert(secondChannelPlan(busy, 149, 36, 0, 44, 0x0c00, sec) == 44);
        unsigned on149 = 0, on44 = 0;
        for (unsigned i = 0; i < 16; ++i)
            on149 += busy[i] == 149, on44 += sec[i] == 44;
        assert(on149 == 13 && on44 == 2 && busy[8] == 6);
        // the simulated channel overrides what the plan names; refused when it is home or Wi-Fi
        uint8_t again[16];
        memset(again, 149, 16);
        assert(!secondChannelPlan(again, 149, 36, 44, 149, 0x0c00, sec) && again[10] == 149);
        assert(!secondChannelPlan(again, 149, 36, 44, 36, 0x0c00, sec) && again[10] == 149);
        assert(!secondChannelPlan(again, 0, 36, 44, 44, 0x0c00, sec) && again[10] == 149);   // no listener yet
        assert(secondChannelPlan(again, 149, 36, 0, 44, 0x0001, sec) == 44 && again[0] == 44 && sec[0] == 44);
        cases += 8;
    }
    {
        // step 2c: the advertised plan for macOS's mixed plan (home 149, second 44, Wi-Fi 36, channel 6).
        uint8_t plan[16] = {149, 149, 44, 0, 0, 0, 0, 36, 6, 149, 149, 44, 44, 0, 0, 36}, sec[16];
        const unsigned second = secondChannelPlan(plan, 149, 36, 44, 0, 0, sec);
        assert(second == 44);
        uint8_t on[16], off[16];
        for (unsigned i = 0; i < 16; ++i) {
            on[i] = advertisedEntry(plan[i], sec[i], 149, second, plan[i] == 6, 6, 36);
            off[i] = advertisedEntry(plan[i], sec[i], 149, 0, plan[i] == 6, 6, 36);   // without -itlschedsecondsync
        }
        const uint8_t wantOn[16] = {149, 149, 44, 36, 36, 36, 36, 36, 6, 149, 149, 44, 44, 36, 36, 36};
        const uint8_t wantOff[16] = {149, 149, 36, 36, 36, 36, 36, 36, 6, 149, 149, 36, 36, 36, 36, 36};
        assert(!memcmp(on, wantOn, 16) && !memcmp(off, wantOff, 16));
        // channel 6 quiet (not advertised): the station's channel there
        assert(advertisedEntry(6, 0, 149, 44, false, 6, 36) == 36);
        // simulation: the moved entries are advertised as the simulated channel, the others unchanged
        uint8_t busy[16], ssec[16];
        memset(busy, 149, 16);
        const unsigned sim = secondChannelPlan(busy, 149, 36, 0, 44, 0x3000, ssec);
        assert(sim == 44 && advertisedEntry(busy[12], ssec[12], 149, sim, false, 6, 36) == 44 &&
               advertisedEntry(busy[13], ssec[13], 149, sim, false, 6, 36) == 44 &&
               advertisedEntry(busy[11], ssec[11], 149, sim, false, 6, 36) == 149);
        // a home entry is never replaced, even if it matched
        assert(advertisedEntry(149, 149, 149, 149, false, 6, 36) == 149);
        cases += 6;
    }
    {
        // #122: macOS wants 149, which can't be used (CN, nothing heard), listener on 44, which the iPhone names.
        const uint64_t hold = 60000000ULL;
        PlanSubstitution sub;
        // off: nothing, ever
        assert(!substitutionUpdate(sub, false, 149, false, 44, true, 1000000, hold) && !sub.from);
        // macOS's channel is followed, or the listener is already on it, or on 2.4 GHz: nothing
        assert(!substitutionUpdate(sub, true, 149, true, 44, true, 1000000, hold) && !sub.from);
        assert(!substitutionUpdate(sub, true, 149, false, 149, true, 1000000, hold) && !sub.from);
        assert(!substitutionUpdate(sub, true, 149, false, 6, true, 1000000, hold) && !sub.from);
        assert(!substitutionUpdate(sub, true, 0, false, 44, true, 1000000, hold) && !sub.from);
        // the iPhone doesn't name 44: no start, even with an old sighting from another case
        PlanSubstitution quiet;
        assert(!substitutionUpdate(quiet, true, 149, false, 44, false, 1000000, hold) && !quiet.from);
        // starts on a fresh sighting
        assert(substitutionUpdate(sub, true, 149, false, 44, true, 2000000, hold));
        assert(sub.from == 149 && sub.to == 44 && sub.starts == 1);
        // held while the sequence is stale, up to the hold time
        assert(!substitutionUpdate(sub, true, 149, false, 44, false, 2000000 + hold - 1, hold) && sub.from == 149);
        assert(substitutionUpdate(sub, true, 149, false, 44, false, 2000000 + hold, hold) && !sub.from);
        // a stale sighting doesn't restart it
        assert(!substitutionUpdate(sub, true, 149, false, 44, false, 2000000 + hold + 10, hold) && !sub.from);
        assert(substitutionUpdate(sub, true, 149, false, 44, true, 90000000, hold) && sub.starts == 2);
        // macOS's channel becomes usable: ends at once
        assert(substitutionUpdate(sub, true, 149, true, 149, true, 90000001, hold) && !sub.from);
        // the plan: macOS's 149 entries move to 44, others stay; not for another listener channel
        assert(substitutionUpdate(sub, true, 149, false, 44, true, 91000000, hold));
        uint8_t plan[16] = {36, 36, 149, 0, 0, 0, 0, 36, 6, 36, 149, 36, 0, 0, 0, 36};
        uint8_t copy[16];
        memcpy(copy, plan, 16);
        assert(substitutePlan(copy, sub, 149) == 0 && !memcmp(copy, plan, 16));
        assert(substitutePlan(copy, sub, 44) == 2 && copy[2] == 44 && copy[10] == 44 && copy[8] == 6 && copy[0] == 36);
        // advertised as the listener's home channel once moved
        assert(advertisedEntry(copy[2], 0, 44, 0, false, 6, 36) == 44);
        cases += 17;
    }
    printf("AWDL channel mix: %u cases passed (host only)\n", cases);
}
