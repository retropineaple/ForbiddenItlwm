// Host checks for the send-agreement rules every peer slot shares (AWDLBaTxRules.hpp, #103).
#include <AWDLBaTxRules.hpp>
#include <cassert>
#include <cstdio>
using namespace ItlAWDL;

struct Slot { uint64_t probeUntilUS; };

int main() {
    unsigned cases = 0;
    const uint64_t now = 10000000;
    const uint32_t asksMax = 40;
    // A later slot's iPhone: station and receive session set, asking, three requests unanswered, the last 250 ms ago.
    const BaProbeView due{true, 0, true, 0, false, 3, now - 250000};
    assert(baProbeAllowed(due, now));
    assert(baProbeDue(due, now, true, asksMax, 0, false));
    ++cases;
    // Not before three requests, not at the round's limit, not right after a request.
    BaProbeView v = due;
    v.asks = 2;
    assert(!baProbeDue(v, now, true, asksMax, 0, false));
    v.asks = asksMax;
    assert(!baProbeDue(v, now, true, asksMax, 0, false));
    v = due;
    v.lastAskUS = now - 100000;
    assert(!baProbeDue(v, now, true, asksMax, 0, false));
    cases += 3;
    // A second window in the dialog only 5 s and three requests after the first; at most three; only while receive
    // is filtered; not while sends are paused or the peer is away.
    v = due;
    v.probes = 1;
    v.asksAtProbe = 3;
    v.lastProbeUS = now - 1000000;
    v.asks = 9;
    assert(!baProbeDue(v, now, true, asksMax, 0, false) && baProbeAllowed(v, now));   // 1 s after the first
    v.lastProbeUS = now - kBaProbeRetryUS;
    assert(baProbeDue(v, now, true, asksMax, 0, false));                             // 5 s and 6 requests after
    v.asks = 5;
    assert(!baProbeDue(v, now, true, asksMax, 0, false));                            // only 2 requests after
    v.asks = 6;
    assert(baProbeDue(v, now, true, asksMax, 0, false));
    v.asks = 4;   // the count started again (idle hold): counts as new requests
    v.asksAtProbe = 30;
    assert(baProbeDue(v, now, true, asksMax, 0, false));
    v = due;
    v.probes = kBaProbeMaxPerDialog;
    v.asksAtProbe = 3;
    v.lastProbeUS = now - 30000000;
    v.asks = 20;
    assert(!baProbeDue(v, now, true, asksMax, 0, false) && baProbeAllowed(v, now));   // three used
    v.probes = kBaProbeMaxPerDialog - 1;
    assert(baProbeDue(v, now, true, asksMax, 0, false));
    cases += 7;
    // #103's slot 3: a window every 5 s while it keeps asking, three in the round, not one per 30 s round.
    {
        BaProbeView s = due;
        s.asks = 0;
        unsigned windows = 0;
        uint64_t t = now;
        for (unsigned ask = 0; ask < asksMax; ++ask, t += 400000) {
            s.asks = ask + 1;
            s.lastAskUS = t;
            const uint64_t check = t + kBaProbeAskGapUS;
            if (baProbeDue(s, check, true, asksMax, 0, false)) {
                ++windows;
                s.probes = uint8_t(s.probes + 1);
                s.asksAtProbe = s.asks;
                s.lastProbeUS = check;
            }
        }
        assert(windows == kBaProbeMaxPerDialog);
        ++cases;
    }
    v = due;
    assert(!baProbeDue(due, now, false, asksMax, 0, false));
    assert(!baProbeDue(due, now, true, asksMax, now + 1, false));
    assert(!baProbeDue(due, now, true, asksMax, 0, true));
    cases += 4;
    // Not allowed at all (the slot's window closes): no receive session, recent data from the peer, resting, agreed.
    v = due;
    v.receiveSession = false;
    assert(!baProbeAllowed(v, now));
    v = due;
    v.lastRxPayloadUS = now - 100000;
    assert(!baProbeAllowed(v, now));
    v.lastRxPayloadUS = now - 400000;
    assert(baProbeAllowed(v, now));
    v = due;
    v.restUntilUS = now + 1;
    assert(!baProbeAllowed(v, now));
    v = due;
    v.asking = false;
    assert(!baProbeAllowed(v, now));
    cases += 5;
    // The filter stays open while any slot's window is open, slot 1's included.
    Slot slots[3] = {{0}, {now + kBaProbeUS}, {0}};
    assert(baProbeOpen(slots, 3, now) && baProbeOpen(slots, 3, now + kBaProbeUS - 1));
    assert(!baProbeOpen(slots, 3, now + kBaProbeUS));
    assert(!baProbeOpen(slots, 1, now));   // without -itlawdlmultipeer only slot 0 counts, as before
    cases += 3;
    // Start rule: an accepted answer or a bulk send, either one.
    assert(baTxShouldStart(true, false) && baTxShouldStart(false, true) && !baTxShouldStart(false, false));
    ++cases;
    // Asking, in slot 0's order. A busy peer, nothing pending: ask.
    const uint64_t idle = 30000000;
    const BaAskView ask{true, now - 1000, 0, now - 300000, false, 5};
    assert(baAskStep(ask, now, idle, asksMax, 0) == BaAskStep::Ask);
    BaAskView a = ask;
    a.bulk = false;
    a.peerLastUS = now - idle - 1;
    assert(baAskStep(a, now, idle, asksMax, 0) == BaAskStep::IdleHold);
    a.bulk = true;   // a bulk send keeps asking however quiet the peer was
    assert(baAskStep(a, now, idle, asksMax, 0) == BaAskStep::Ask);
    cases += 3;
    a = ask;
    a.restUntilUS = now + 1;
    assert(baAskStep(a, now, idle, asksMax, 0) == BaAskStep::Resting);
    a.restUntilUS = now;   // rest over
    assert(baAskStep(a, now, idle, asksMax, 0) == BaAskStep::Ask);
    a = ask;
    a.lastAskUS = now - kBaAskGapUS + 1;
    assert(baAskStep(a, now, idle, asksMax, 0) == BaAskStep::TooSoon);
    cases += 3;
    // Away is checked before the round's limit, so a request isn't spent (or a round closed) while the peer is away.
    a = ask;
    a.peerAway = true;
    a.asks = asksMax;
    assert(baAskStep(a, now, idle, asksMax, 0) == BaAskStep::PeerAway);
    a.peerAway = false;
    assert(baAskStep(a, now, idle, asksMax, 0) == BaAskStep::RoundOver);
    a.asks = 5;
    assert(baAskStep(a, now, idle, asksMax, now + 1) == BaAskStep::Paused);
    cases += 3;
    printf("awdl_ba_tx_rules: %u cases passed\n", cases);
    return 0;
}
