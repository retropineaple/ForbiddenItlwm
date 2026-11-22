#pragma once
// -itlawdlslothop: which channel the listener should be on, from the phone's announced plan.
//
// The phone's channel sequence (TLV 18) has one entry per extended AW. On 2026-11-22 it announced, for a Mac to iPhone AirDrop,
//   149 149 149 149 161 161 161 161 6 149 149 149 161 161 161 161
// (the plan IO80211Family runs in its Data state, `kAwdlScheduleStateDataWithInfraData`: 161 is the infrastructure channel, 149 the
// AWDL channel), and a radio parked on 161 met it in 8 of 16 entries. 149 and 161 lie in one 80 MHz block (149 153 157 161, centre
// 155), so moving between them is a change of primary 20 MHz channel and not a retune. This picks, for the time a hop would land,
// the entry's channel when the radio can be on it, and says how long the choice holds.
#include <AWDLPeerSchedule.hpp>
#include <stdint.h>

namespace ItlAWDL {

// The 80 MHz block a 5 GHz channel belongs to, as its lowest channel, or 0 (2.4 GHz, 6 GHz, unknown, 165 and above alone).
inline unsigned block80Of(unsigned channel) {
    static const unsigned firsts[] = {36, 52, 100, 116, 132, 149};
    for (unsigned first : firsts)
        if (channel >= first && channel <= first + 12 && ((channel - first) % 4) == 0) return first;
    return 0;
}

struct SlotHopChoice {
    uint8_t channel;      // The channel to be on at the time asked about (the current one when there is nothing better).
    uint64_t holdUS;      // How long that entry run lasts from then, in microseconds (0: unknown).
    bool follows;         // The phone is on a channel of the set at that time.
};

// `set` lists the channels the radio may be on (all in one 80 MHz block). `current` is where it is now. `leadUS` is how long a
// hop takes, so the choice is made for `nowUS + leadUS`. A run shorter than `minRunUS` is not worth a hop: the radio stays.
inline SlotHopChoice chooseSlotHop(const PeerSchedule &s, uint64_t nowUS, const uint8_t *set, unsigned count, uint8_t current,
                                   uint64_t leadUS, uint64_t minRunUS) {
    SlotHopChoice out{current, 0, false};
    if (!count || !s.known() || !s.fresh(nowUS) || !s.eawUS) return out;
    uint64_t into = 0;
    const uint64_t at = nowUS + leadUS;
    const uint32_t eaw = s.eawAt(at, &into);
    const uint8_t ch = s.channels[(eaw + s.entryOffset()) % kPeerSeqEntries];
    bool inSet = false;
    for (unsigned k = 0; k < count; ++k) inSet |= set[k] == ch;
    if (!inSet) return out;
    // How long the run of entries on `ch` lasts from `at`.
    uint64_t hold = s.eawUS - into;
    for (unsigned k = 1; k < kPeerSeqEntries; ++k) {
        if (s.channels[(eaw + k + s.entryOffset()) % kPeerSeqEntries] != ch) break;
        hold += s.eawUS;
    }
    out.follows = true;
    out.holdUS = hold;
    if (ch != current && hold < minRunUS) return {current, hold, true};   // Too short to pay for the hop.
    out.channel = ch;
    return out;
}

// Whether the set is usable at all: two or more channels, all in one 80 MHz block.
inline bool slotHopSetValid(const uint8_t *set, unsigned count) {
    if (count < 2) return false;
    const unsigned block = block80Of(set[0]);
    if (!block) return false;
    for (unsigned k = 1; k < count; ++k) if (block80Of(set[k]) != block) return false;
    return true;
}

// Statistics of the hops, and the rule that stops them: too slow on average, or failing.
struct SlotHopStats {
    uint32_t hops = 0, failures = 0, slow = 0;
    uint64_t latencySumUS = 0, latencyMinUS = ~0ULL, latencyMaxUS = 0;
    bool disabled = false;
    uint8_t reason = 0;   // 0 running, 1 a hop failed, 2 too slow, 3 firmware refused
    static constexpr uint64_t kSlowUS = 25000;       // A hop longer than this is slow (the phone's entry is 65 ms).
    static constexpr uint32_t kMinHopsToJudge = 20;
    void noteHop(uint64_t us) {
        ++hops;
        latencySumUS += us;
        if (us < latencyMinUS) latencyMinUS = us;
        if (us > latencyMaxUS) latencyMaxUS = us;
        if (us > kSlowUS) ++slow;
        // Stop when most hops are slow: a switch that eats a third of an entry gains nothing.
        if (hops >= kMinHopsToJudge && slow * 2 > hops) { disabled = true; reason = 2; }
    }
    void noteFailure() {
        ++failures;
        if (failures >= 2) { disabled = true; reason = 1; }
    }
    uint64_t averageUS() const { return hops ? latencySumUS / hops : 0; }
};

}  // namespace ItlAWDL
