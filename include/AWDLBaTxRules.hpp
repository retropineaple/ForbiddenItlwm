// The rules for our send agreement (Block Ack for frames we send to a peer), shared by every peer slot. Until
// 2026-11-27 slot 0 and the further slots (-itlawdlmultipeer) each had their own copy, and the further slots' copy
// had drifted: it started only on a bulk send, kept asking while the peer was away, and had no reply-delivery probe.
// A peer in a later slot could then ask without end and send singly (#103). Both paths now call these.
#pragma once
#include <stdint.h>

namespace ItlAWDL {

// When to set up the agreement: once the peer has acknowledged our answer to its own request (it can receive from
// us), or once a bulk send to it has started.
inline bool baTxShouldStart(bool accepted, bool bulkToPeer) { return accepted || bulkToPeer; }

// One pass of the asking state: what to do now, in this order.
enum class BaAskStep : uint8_t {
    IdleHold,   // no bulk send and the peer quiet: clear the count and any rest, ask again on the next data
    Resting,    // between rounds
    TooSoon,    // our last request is under 200 ms old
    PeerAway,   // the peer's schedule says it isn't on our channel: don't spend a request
    RoundOver,  // the round's requests are used up: new dialog token, rest
    Paused,     // sends are held for a channel move or similar
    Ask,        // send a request
};

constexpr uint64_t kBaAskGapUS = 200000;

struct BaAskView {
    bool bulk;              // a bulk send to this peer is running
    uint64_t peerLastUS;    // last frame to or from the peer
    uint64_t restUntilUS;   // 0 = not resting
    uint64_t lastAskUS;     // 0 = none in this round
    bool peerAway;
    uint32_t asks;          // requests in this round
};

inline BaAskStep baAskStep(const BaAskView &v, uint64_t now, uint64_t idleUS, uint32_t asksMax,
                           uint64_t txPauseUntilUS)
{
    if (!v.bulk && now - v.peerLastUS > idleUS)
        return BaAskStep::IdleHold;
    if (v.restUntilUS && now < v.restUntilUS)
        return BaAskStep::Resting;
    if (v.lastAskUS && now - v.lastAskUS < kBaAskGapUS)
        return BaAskStep::TooSoon;
    if (v.peerAway)
        return BaAskStep::PeerAway;
    if (v.asks >= asksMax)
        return BaAskStep::RoundOver;
    if (now < txPauseUntilUS)
        return BaAskStep::Paused;
    return BaAskStep::Ask;
}

// The reply-delivery probe. With a peer's station set the listener filters what it receives, and some phones' ADDBA
// answers then don't get through. After three unanswered requests in a dialog the driver opens promiscuous receive
// for 250 ms so the answer can arrive. Until 2026-11-27 that was once per dialog, and a dialog's window could fall
// while another slot's bulk send held the air: a later slot then asked until the next round, 30 s on (#103, slot 3
// agreed only on the third round). Now a dialog gets up to three windows, 5 s and three more requests apart.
constexpr uint64_t kBaProbeUS = 250000;          // how long receive stays open
constexpr uint64_t kBaProbeAskGapUS = 200000;    // since our last request
constexpr uint64_t kBaProbeRecentRxUS = 300000;  // data from the peer this recently: replies already get through
constexpr uint32_t kBaProbeMinAsks = 3;          // unanswered requests before a window (and between windows)
constexpr uint8_t kBaProbeMaxPerDialog = 3;
constexpr uint64_t kBaProbeRetryUS = 5000000;    // between a dialog's windows

// One slot's state, as the probe reads it.
struct BaProbeView {
    bool receiveSession;      // the peer's station is set and it has a receive session open
    uint64_t lastRxPayloadUS; // last data heard from the peer (0 = none)
    bool asking;              // our send agreement is in its asking state
    uint64_t restUntilUS;     // nonzero while resting between rounds
    uint8_t probes;           // windows already opened in this dialog
    uint32_t asks;            // requests sent in this dialog
    uint64_t lastAskUS;
    uint32_t asksAtProbe = 0; // requests sent when the last window opened
    uint64_t lastProbeUS = 0; // when it opened
};

// Whether a probe could apply to this slot at all. When it can't, the slot's open window is closed.
inline bool baProbeAllowed(const BaProbeView &v, uint64_t now)
{
    const bool recentRx = v.lastRxPayloadUS && now - v.lastRxPayloadUS < kBaProbeRecentRxUS;
    return v.receiveSession && !recentRx && v.asking && !v.restUntilUS;
}

// Whether to open receive for this slot now. peerAway: the peer's schedule says it isn't on our channel.
inline bool baProbeDue(const BaProbeView &v, uint64_t now, bool filterOff, uint32_t asksMax, uint64_t txPauseUntilUS,
                       bool peerAway)
{
    const bool windowLeft = !v.probes || (v.probes < kBaProbeMaxPerDialog && now - v.lastProbeUS >= kBaProbeRetryUS &&
                                          // (a count that started again, after an idle hold, counts as new)
                                          (v.asks < v.asksAtProbe || v.asks - v.asksAtProbe >= kBaProbeMinAsks));
    return baProbeAllowed(v, now) && windowLeft && filterOff && v.asks >= kBaProbeMinAsks && v.asks < asksMax &&
        now >= txPauseUntilUS && now - v.lastAskUS >= kBaProbeAskGapUS && !peerAway;
}

// Whether any slot's probe window is still open (receive must stay unfiltered).
template <typename Slot> inline bool baProbeOpen(const Slot *slots, unsigned count, uint64_t now)
{
    for (unsigned i = 0; i < count; ++i)
        if (now < slots[i].probeUntilUS)
            return true;
    return false;
}

}  // namespace ItlAWDL
