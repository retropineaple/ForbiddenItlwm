"""Run the driver's real per-peer receive Block Ack step (awdlCoexBaRxPeer, awdlCoexBaAddPeer, awdlCoexBaRemovePeer)
for several peers against mocked firmware commands.

Checks that each peer gets its own station and session and its own answer, that an idle further slot gives its
station back, and that one slot's failure stays in that slot. Bookkeeping and command order only: no firmware,
radio or timing.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "itlwm/hal_iwx/ItlIwx.cpp").read_text()


def method(signature):
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


def static(name):
    start = source.index(name)
    return source[start:source.index("\n}\n", start) + 3]


bodies = "\n".join([
    static("static void awdlPeerHoldFree(unsigned slot)"),
    "static uint8_t awdlPeerIndex(const ItlAWDL::PeerSlot &slot) { return uint8_t(&slot - awdlPeers); }",
    source[source.index("struct AwdlSendingPeer {"):source.index("\n};\n", source.index("struct AwdlSendingPeer {")) + 4],
    static("static bool awdlPeerAnyAdded()"),
    static("static void awdlCoexBaFail(ItlAWDL::PeerSlot &slot, int gen)"),
    method("int ItlIwx::\nawdlCoexBaRemovePeer("),
    method("int ItlIwx::\nawdlCoexBaAddPeer("),
    method("bool ItlIwx::\nawdlCoexBaRxPeer("),
    static("static bool awdlCoexBaQueueCurrent(int generation, uint32_t listener, uint32_t peer, uint8_t slot)\n"),
    static("static bool awdlPeerScheduleCurrent(const ItlAWDL::PeerSlot &p)"),
    static("static bool awdlBaPeerAway(const ItlAWDL::PeerSlot &p, uint64_t now)"),
    static("static uint32_t &awdlBaAsks(ItlAWDL::PeerSlot &p)"),
    method("bool ItlIwx::\nawdlBaAskPass("),
    method("bool ItlIwx::\nawdlCoexBaTxPeer("),
    static("static bool awdlPeerGateClosed(ItlAWDL::PeerSlot &p, uint64_t now)"),
    method("void ItlIwx::\nawdlCoexPeerPauseStep("),
])

program = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include <AWDLObservation.hpp>
#include <AWDLBlockAck.hpp>
#include <AWDLPeerSlot.hpp>
#include <AWDLBaTxRules.hpp>
#define htole16(x) (x)
#define htole32(x) (x)
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define nitems(a) (sizeof(a) / sizeof((a)[0]))
#define IEEE80211_ADDR_COPY(d, s) memcpy(d, s, 6)
#define IWX_FW_CMD_ID_AND_COLOR(id, c) ((id) | ((c) << 8))
enum { IWX_ADD_STA = 0x18, IWX_REMOVE_STA = 0x19, IWX_STA_LINK = 0, IWX_STA_MODE_MODIFY = 1,
    IWX_STA_MODIFY_ADD_BA_TID = 0x8, IWX_STA_MODIFY_REMOVE_BA_TID = 0x10, IWX_ADD_STA_SUCCESS = 1,
    IWX_ADD_STA_STATUS_MASK = 0xff, IWX_ADD_STA_BAID_VALID_MASK = 0x8000, IWX_FW_CTXT_ACTION_MODIFY = 2,
    IWX_ADD_STA_IMMEDIATE_BA_FAILURE = 0x4,
    IWX_STA_FLG_FAT_EN_20MHZ = 0, IWX_STA_FLG_FAT_EN_80MHZ = 1 << 26, IWX_STA_FLG_MIMO_EN_MIMO2 = 1 << 28,
    IWX_STA_FLG_MAX_AGG_SIZE_64K = 3 << 19, IWX_STA_FLG_MAX_AGG_SIZE_1024K = 6 << 19, IWX_STA_FLG_FAT_EN_MSK = 3 << 26,
    IWX_STA_FLG_MIMO_EN_MSK = 3 << 28, IWX_STA_FLG_MAX_AGG_SIZE_MSK = 15 << 19, IWX_STA_FLG_AGG_MPDU_DENS_MSK = 7 << 23,
    kAwdlCoexMacId = 2, kCoexSlotBaAnswer = 3, kCoexSlotBaAsk, kCoexSlotDelba,
    IWX_STA_MODIFY_TID_DISABLE_TX = 0x2, IWX_DEFAULT_QUEUE_SIZE = 256, IWX_STA_FLG_DISABLE_TX = 1 << 4,
    IWX_CMD_ASYNC = 1 };
enum { kCoexBaTxOff, kCoexBaTxAsking, kCoexBaTxAgreed, kCoexBaTxFailed, kCoexBaTxEnabling };
constexpr uint8_t kAwdlBaStationId = 3, kAwdlBaTxTid = 0;
constexpr uint32_t kAwdlBaTxAsksMax = 40;
constexpr uint64_t kAwdlBaTxRestUS = 20000000, kAwdlCoexBaIdleAskUS = 30000000;
static bool awdlBaTx80Active, awdlBaTlc;
static uint8_t awdlBaTlcStation = kAwdlBaStationId;
static int awdlCoexGeneration = 7;
struct iwx_node {};
struct ieee80211_node_stub {};
struct iwx_add_sta_cmd { uint8_t add_modify; uint16_t tid_disable_tx; uint32_t mac_id_n_color; uint8_t addr[6];
    uint8_t sta_id, modify_mask; uint8_t station_type; uint32_t station_flags, station_flags_msk;
    uint8_t add_immediate_ba_tid, remove_immediate_ba_tid; uint16_t add_immediate_ba_ssn, rx_ba_window; };
struct iwx_rm_sta_cmd { uint8_t sta_id; };
struct iwx_tx_ring { int queued, cur; };
struct ieee80211com { void *ic_bss = nullptr; };
struct iwx_softc { int sc_generation = 7; iwx_tx_ring txq[8] = {}; ieee80211com sc_ic; };
static ItlAWDL::PeerSlot awdlPeers[ItlAWDL::kMaxPeerSlots];
static ItlAWDL::PeerSlot &awdlPeer = awdlPeers[0];
static unsigned awdlPeerSlotsUsable = 3;
using mbuf_t = void *;
static void mbuf_freem(mbuf_t) {}
static ItlAWDL::BoundedFifo<mbuf_t, 128> awdlPeerHold[ItlAWDL::kMaxPeerSlots];
static uint8_t awdlCoexSendingPeer;
static uint8_t sentFor[64];   // the slot each sent management frame was credited to
static uint32_t awdlPeerSlotAdds, awdlPeerSlotRemoves, awdlPeerSlotFailures;
// -itlslotdiag is off in this harness; station changes are only noted when it is on.
static bool awdlSlotDiag = false;
static struct { void noteEvent(uint64_t, char, uint8_t) {} } awdlSlotDiagState;
static uint32_t awdlPeerTxQueues, awdlPeerGateHolds, awdlPeerGateForced, awdlPeerPauses, awdlCoexPauseResumeFailures,
    awdlCoexResumeRetirements;
static bool awdlCoexBaPeerPauseEnabled = true, awdlCoexMovePending, awdlCoexTxHalted, awdlCoexGate = true;
// Step 2b (-itlschedsecondtx) is off here: no peer is ever on a visited second channel.
static uint32_t awdlSchedSecondPeerHere;
static bool awdlSchedSecondPeerThere(ieee80211com *, const ItlAWDL::PeerSchedule &, uint64_t) { return false; }
static uint32_t awdlSchedSecondAnswersHeld;
static bool awdlSchedSecondHoldAtHome(const ItlAWDL::PeerSlot &, uint64_t) { return false; }   // step 2d off
static int awdlCoexBaGen = 7;
static unsigned awdlCoexChannel = 149;
constexpr uint64_t kAwdlGateAwWindowUS = 4000, kAwdlGateAwakeUS = 100000;
static int awdlCoexBaFailedGen = -1, awdlCoexBaTxResetting = -1;
static bool awdlCoexAdded = true, awdlCoexBaFilterOff, awdlCoexBaTxEnabled = true;
static uint32_t awdlCoexToken = 5, awdlCoexBaStatus, awdlCoexBaSessions, awdlCoexBaAnswers, awdlCoexBaAnswerErrors,
    awdlCoexBaOtherPeer, awdlCoexBaRxIdleTeardowns, awdlCoexBaDelbasSent, awdlCoexDelbaForced, awdlCoexDelbaConfirmed,
    awdlCoexDelbaRxTidRemoved, awdlCoexRateWindowOK, awdlCoexRateWindowFailed, awdlCoexBaRxDeclined, awdlCoexBaRxCancelled,
    awdlPeerTxRetries;
constexpr uint64_t kAwdlPeerTxRetryUS = 30000000ULL;
static unsigned awdlCoexRateStep;
static uint16_t awdlCoexBaSequence, awdlCoexLastMgmtEnqueueId;
static bool awdlAmsduRx;
static uint32_t awdlAmsduRxAsked, awdlAmsduRxNotAsked, awdlAmsduRxAllowed;
static bool awdlAmsduTx;   // -itlawdlamsdutx off here: requests and agreements as before
static uint32_t awdlAmsduTxOffers, awdlAmsduTxAgreed, awdlAmsduTxRefused;
static uint64_t awdlCoexTxPauseUntilUS;
static uint8_t awdlCoexAddr[6] = {2, 9, 9, 9, 9, 9};
static ItlAWDL::BaFilterState awdlCoexBaFilter;
static struct { bool applied, restorePending; } awdlCoexSameChannelProbe;
static uint64_t clockUS = 100000000ULL;
static uint64_t awdlUptimeUS() { return clockUS; }
static bool known = true;
static bool awdlCoexPeerKnown(const uint8_t *, uint64_t, bool = false) { return known; }
static void awdlCoexBaEvent(char, unsigned, unsigned, unsigned, unsigned = 0, unsigned = 0) {}
// Slot 0's asking state and schedule (slot 0 isn't exercised here) and the further slots' asking counters.
static ItlAWDL::PeerSchedule awdlCoexPeerSchedule;
static bool awdlCoexScheduleCurrent() { return false; }
static uint32_t awdlCoexBaTxAsks, awdlCoexBaTxIdleHolds, awdlCoexBaAsksSkippedAway, awdlCoexAskSkipsSinceLast,
    awdlCoexBaTxAskRounds, awdlPeerBaAsksSent, awdlPeerBaAsksSkippedAway, awdlPeerBaAskRounds, awdlPeerBaIdleHolds;
static int awdlCoexBaWidth(ieee80211com *) { return 80; }
struct Command { int id; uint8_t sta, mask, tid; uint8_t addr[6]; uint32_t flags; };
static std::vector<Command> commands;
static std::vector<std::vector<uint8_t>> sent;
static int failAddFor = -1;     // ADD_STA (new station) fails for this station number
static int nextQueue = 5, queueFail;
static int declineBaFor = -1;   // ADD_BA_TID on this station answers "no room for another session"
static void (*duringAdd)(int station) = nullptr;   // runs while ADD_STA (new station) waits, as the gate lets RX in
static unsigned rateInits, rateStation;
class ItlIwx {
public:
    struct CoexBaQueueOwner { int generation; uint32_t listener, peer; uint8_t slot; };
    int iwx_tvqm_enable_txq(iwx_softc *, int tid, int, uint32_t, int station, const CoexBaQueueOwner *owner) {
        assert(!tid && owner && owner->slot < ItlAWDL::kMaxPeerSlots &&
               awdlPeers[owner->slot].station == station && awdlPeers[owner->slot].epoch == owner->peer);
        commands.push_back(Command{0x40, uint8_t(station), 0, 0, {}});
        return queueFail ? -5 : nextQueue++;
    }
    int iwx_rs_init(iwx_softc *, iwx_node *, bool) {
        assert(awdlBaTlc);
        ++rateInits;
        rateStation = awdlBaTlcStation;
        return 0;
    }
    int iwx_send_cmd_pdu_status(iwx_softc *, int id, size_t, const void *data, uint32_t *status) {
        const auto *c = static_cast<const iwx_add_sta_cmd *>(data);
        Command k{id, c->sta_id, c->modify_mask, c->modify_mask == IWX_STA_MODIFY_ADD_BA_TID ? c->add_immediate_ba_tid :
                  c->modify_mask == IWX_STA_MODIFY_TID_DISABLE_TX ? uint8_t(c->tid_disable_tx & 1) :
                  c->remove_immediate_ba_tid, {}};
        memcpy(k.addr, c->addr, 6);
        k.flags = c->station_flags_msk == IWX_STA_FLG_DISABLE_TX ? 0x100 | c->station_flags : 0;
        commands.push_back(k);
        if (!c->add_modify && duringAdd)
            duringAdd(c->sta_id);
        if (!c->add_modify && c->sta_id == failAddFor) {
            *status = 0;
            return 0;
        }
        if (c->modify_mask == IWX_STA_MODIFY_ADD_BA_TID && c->sta_id == declineBaFor) {
            *status = IWX_ADD_STA_IMMEDIATE_BA_FAILURE;
            return 0;
        }
        *status = IWX_ADD_STA_SUCCESS | (c->modify_mask == IWX_STA_MODIFY_ADD_BA_TID ? IWX_ADD_STA_BAID_VALID_MASK : 0);
        return 0;
    }
    int iwx_send_cmd_pdu(iwx_softc *, int id, int, size_t, const void *data) {
        if (id == IWX_ADD_STA) {   // the asynchronous pause
            const auto *c = static_cast<const iwx_add_sta_cmd *>(data);
            commands.push_back(Command{id, c->sta_id, 0xfe, 0, {}, c->station_flags});
            return 0;
        }
        const auto *c = static_cast<const iwx_rm_sta_cmd *>(data);
        commands.push_back(Command{id, c->sta_id, 0, 0, {}, 0});
        return 0;
    }
    int iwx_flush_sta_tids(iwx_softc *, int, int) { return 0; }
    void iwx_reset_tx_ring(iwx_softc *, iwx_tx_ring *) {}
    int awdlCoexMacSend(iwx_softc *, uint32_t) { return 0; }
    bool awdlCoexSendMgmt(iwx_softc *, const uint8_t *frame, size_t length, uint8_t) {
        if (sent.size() < sizeof(sentFor))
            sentFor[sent.size()] = awdlCoexSendingPeer;
        sent.push_back(std::vector<uint8_t>(frame, frame + length));
        return true;
    }
    int awdlCoexBaRemovePeer(iwx_softc *, ItlAWDL::PeerSlot &);
    int awdlCoexBaAddPeer(iwx_softc *, ItlAWDL::PeerSlot &, const uint8_t *);
    bool awdlCoexBaRxPeer(iwx_softc *, ItlAWDL::PeerSlot &, int, uint32_t, uint64_t);
    bool awdlCoexBaTxPeer(iwx_softc *, ItlAWDL::PeerSlot &, int, uint32_t, uint64_t);
    bool awdlBaAskPass(iwx_softc *, ItlAWDL::PeerSlot &, unsigned, uint64_t);
    void awdlCoexPeerPauseStep(iwx_softc *, ItlAWDL::PeerSlot &);
};
''' + bodies + r'''
static ItlIwx driver;
static iwx_softc hardware;
static ItlAWDL::AddbaRequest request(uint8_t last, uint8_t tid, uint8_t token) {
    ItlAWDL::AddbaRequest r{};
    r.immediate = true;
    r.peer[0] = 2;
    r.peer[5] = last;
    r.tid = tid;
    r.token = token;
    r.window = 64;
    return r;
}
// What the receive path does with an ADDBA request (iwx_rx_frame).
static bool deliver(const ItlAWDL::AddbaRequest &r) {
    ItlAWDL::PeerSlot *slot = ItlAWDL::slotForRequest(awdlPeers, awdlPeerSlotsUsable, r.peer, hardware.sc_generation);
    return slot && slot->requests.note(r);
}
// The task's receive part: slot 0, then the others; a pass ends at the first slot that sent a command.
static void pass() {
    const int gen = hardware.sc_generation;
    if (!driver.awdlCoexBaRxPeer(&hardware, awdlPeer, gen, awdlCoexToken, clockUS))
        return;
    for (unsigned i = 1; i < awdlPeerSlotsUsable; ++i)
        if (!driver.awdlCoexBaRxPeer(&hardware, awdlPeers[i], gen, awdlCoexToken, clockUS))
            return;
}
static void settle() { for (int i = 0; i < 40; ++i) pass(); }
static unsigned count(int id, uint8_t sta, uint8_t mask = 0xff) {
    unsigned n = 0;
    for (const auto &c : commands)
        n += c.id == id && c.sta == sta && (mask == 0xff || c.mask == mask);
    return n;
}
static void reset() {
    // Every slot starts with the default station number (3), as after a reset that doesn't renumber: the
    // driver must still use its own number per slot (3, then 5 and up; the stick test on 2026-11-24 showed every slot
    // at 3).
    for (unsigned i = 0; i < ItlAWDL::kMaxPeerSlots; ++i)
        awdlPeers[i] = ItlAWDL::PeerSlot{};
    commands.clear();
    sent.clear();
    failAddFor = -1;
    declineBaFor = -1;
    duringAdd = nullptr;
    awdlCoexBaFailedGen = -1;
    awdlPeerSlotsUsable = 3;
    hardware = iwx_softc{};
    clockUS = 100000000ULL;
    known = true;
}
int main() {
    unsigned cases = 0;
    // Two peers asking at once: each gets its own station, session and answer.
    reset();
    assert(deliver(request(10, 0, 1)) && deliver(request(11, 0, 2)) && deliver(request(11, 5, 3)));
    settle();
    assert(awdlPeers[0].added && awdlPeers[1].added && !awdlPeers[2].added);
    assert(ItlAWDL::sameAddress(awdlPeers[0].addr, request(10, 0, 0).peer));
    assert(ItlAWDL::sameAddress(awdlPeers[1].addr, request(11, 0, 0).peer));
    assert(count(0x18, 3, 0) == 1 && count(0x18, 5, 0) == 1);            // one new station each
    assert(awdlPeers[0].station == 3 && awdlPeers[1].station == 5);
    for (const auto &c : commands)                                         // channel 6's station 4 is never touched
        assert(c.sta != 4);
    for (const auto &c : commands)                                         // and each command names its own
        if (c.id == 0x18 && !c.mask)
            assert(c.sta == 3 ? ItlAWDL::sameAddress(c.addr, request(10, 0, 0).peer) :
                   ItlAWDL::sameAddress(c.addr, request(11, 0, 0).peer));
    assert(count(0x18, 3, 0x8) == 1 && count(0x18, 5, 0x8) == 2);        // A: TID 0; B: TIDs 0 and 5
    assert(awdlPeers[0].rxTids == 0x01 && awdlPeers[1].rxTids == 0x21);
    unsigned toA = 0, toB = 0;
    for (const auto &f : sent) {
        toA += ItlAWDL::sameAddress(f.data() + 4, request(10, 0, 0).peer);
        toB += ItlAWDL::sameAddress(f.data() + 4, request(11, 0, 0).peer);
    }
    assert(toA == 1 && toB == 2 && awdlCoexBaAnswers == 0 + sent.size());
    for (size_t i = 0; i < sent.size() && i < sizeof(sentFor); ++i)      // each answer credited to its own slot
        assert(sentFor[i] == (ItlAWDL::sameAddress(sent[i].data() + 4, request(10, 0, 0).peer) ? 0 : 1));
    assert(awdlCoexSendingPeer == 0);
    ++cases;
    // A third peer with a third slot; a fourth finds none.
    assert(deliver(request(12, 0, 4)));
    settle();
    assert(awdlPeers[2].added && count(0x18, 6, 0) == 1);
    assert(!deliver(request(13, 0, 5)));
    ++cases;
    // A further slot gives its station back once idle: receive sessions first (DELBA, forced after 3 s), then the
    // station after 60 s without data. Slot 0 keeps its station for ten minutes.
    clockUS += 61000000ULL;
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned t = 0; t < 8; ++t)
            if (awdlPeers[i].rxTids & (1U << t))
                awdlPeers[i].rxTidLastUS[t] = clockUS - 61000000ULL;
    for (unsigned i = 0; i < 3; ++i)
        awdlPeers[i].lastUS = clockUS - 61000000ULL;
    for (int round = 0; round < 12; ++round) {
        settle();
        clockUS += 1000000ULL;
    }
    assert(awdlPeers[0].added && !awdlPeers[1].added && !awdlPeers[2].added);
    assert(count(0x19, 5) == 1 && count(0x19, 6) == 1 && count(0x19, 3) == 0 && count(0x19, 4) == 0);
    assert(count(0x18, 5, 0x10) == 2 && count(0x18, 6, 0x10) == 1);    // its sessions went first
    ++cases;
    // A failed station add takes only that slot out of use; others go on, and the next peer gets the next slot.
    reset();
    failAddFor = 5;
    assert(deliver(request(10, 0, 1)) && deliver(request(11, 0, 2)));
    settle();
    assert(awdlPeers[0].added && !awdlPeers[1].added && awdlPeers[1].failedGen == hardware.sc_generation);
    assert(awdlCoexBaFailedGen == -1 && awdlPeerSlotFailures);
    assert(deliver(request(12, 0, 3)));
    settle();
    assert(awdlPeers[2].added && ItlAWDL::sameAddress(awdlPeers[2].addr, request(12, 0, 0).peer));
    assert(!deliver(request(11, 0, 4)));                                   // slot 1 stays out this generation
    ++cases;
    // Slot 0's failure still quarantines all Block Ack, as before.
    reset();
    failAddFor = 3;
    assert(deliver(request(10, 0, 1)));
    settle();
    assert(awdlCoexBaFailedGen == hardware.sc_generation && !awdlPeers[0].added);
    ++cases;
    // One slot (no -itlawdlmultipeer): a second peer is refused while the first has the station.
    reset();
    awdlPeerSlotsUsable = 1;
    assert(deliver(request(10, 0, 1)));
    settle();
    assert(awdlPeers[0].added && !deliver(request(11, 0, 2)) && count(0x18, 5) == 0);
    ++cases;
    // A peer that already has a station in slot 0 never gets a second one in another slot.
    reset();
    assert(deliver(request(10, 0, 1)));
    settle();
    assert(awdlPeers[1].requests.note(request(10, 3, 2)));
    settle();
    assert(!awdlPeers[1].added && awdlPeers[1].requests.empty() && count(0x18, 5) == 0);
    ++cases;
    // Sending to a further slot's peer: bulk data -> its own queue on its station with rate control for that
    // station -> ADDBA request to it, credited to its slot -> (its answer) -> aggregation enabled on its station.
    reset();
    nextQueue = 5;
    rateInits = 0;
    assert(deliver(request(10, 0, 1)) && deliver(request(11, 0, 2)));
    settle();
    ItlAWDL::PeerSlot &b = awdlPeers[1];
    assert(b.added && b.station == 5 && b.txState == kCoexBaTxOff);
    auto txPass = [&]() { return driver.awdlCoexBaTxPeer(&hardware, b, hardware.sc_generation, awdlCoexToken, clockUS); };
    assert(txPass() && b.txState == kCoexBaTxOff);                         // no bulk yet: nothing
    for (int i = 0; i < 4 && b.txState == kCoexBaTxOff; ++i) {           // 300 payload frames a second
        b.txFrames += 300;
        clockUS += 1000000ULL;
        b.lastUS = clockUS;
        (void)txPass();
    }
    assert(b.txBulk.bulk && b.txState == kCoexBaTxAsking && b.txQueue == 5 && rateInits == 1 && rateStation == 5);
    assert(awdlBaTlcStation == kAwdlBaStationId && !awdlBaTlc);           // restored for slot 0
    sent.clear();
    clockUS += 300000ULL;
    assert(txPass() && b.txAsks == 1 && sent.size() == 1 && sentFor[0] == 1);
    assert(ItlAWDL::sameAddress(sent[0].data() + 4, request(11, 0, 0).peer) && sent[0][24] == 3 && sent[0][25] == 0);
    assert(awdlPeers[0].txState == kCoexBaTxOff && awdlPeers[0].txQueue == -1);   // slot 0 untouched
    b.txState = kCoexBaTxEnabling;                                         // what its answer does (iwx_rx_frame)
    (void)txPass();
    assert(b.txState == kCoexBaTxAgreed && count(0x18, 5, 0x2) == 1 && count(0x18, 3, 0x2) == 0);
    ++cases;
    // Slot 0's start rule for a further slot too: a peer that acknowledged our answer gets a send agreement
    // without waiting for a bulk send.
    reset();
    nextQueue = 5;
    assert(deliver(request(10, 0, 1)) && deliver(request(11, 0, 2)));
    settle();
    awdlPeers[1].accepted = true;
    (void)driver.awdlCoexBaTxPeer(&hardware, awdlPeers[1], hardware.sc_generation, awdlCoexToken, clockUS);
    assert(!awdlPeers[1].txBulk.bulk && awdlPeers[1].txState == kCoexBaTxAsking && awdlPeers[1].txQueue == 5);
    ++cases;
    // A queue that can't be had marks only that slot's sending as failed.
    reset();
    queueFail = 1;
    assert(deliver(request(10, 0, 1)) && deliver(request(11, 0, 2)));
    settle();
    awdlPeers[1].txBulk.bulk = true;
    (void)driver.awdlCoexBaTxPeer(&hardware, awdlPeers[1], hardware.sc_generation, awdlCoexToken, clockUS);
    assert(awdlPeers[1].txState == kCoexBaTxFailed && awdlPeers[1].added && awdlCoexBaFailedGen == -1);
    queueFail = 0;
    ++cases;
    // Asking stops after kAwdlBaTxAsksMax requests and rests; an idle peer holds.
    reset();
    assert(deliver(request(10, 0, 1)) && deliver(request(11, 0, 2)));
    settle();
    ItlAWDL::PeerSlot &c = awdlPeers[1];
    c.txState = kCoexBaTxAsking;
    c.txBulk.bulk = true;
    c.accepted = true;   // keeps asking once the bulk detector cools (slot 0's start rule, which asking obeys too)
    c.txQueue = 6;
    for (unsigned i = 0; i < kAwdlBaTxAsksMax + 2; ++i) {
        clockUS += 250000ULL;
        c.lastUS = clockUS;
        (void)driver.awdlCoexBaTxPeer(&hardware, c, hardware.sc_generation, awdlCoexToken, clockUS);
    }
    assert(c.txRestUntilUS > clockUS && c.txAsks == 0);
    c.txRestUntilUS = 0;
    c.txBulk.bulk = false;
    c.lastUS = clockUS - kAwdlCoexBaIdleAskUS - 1;
    const size_t before = sent.size();
    (void)driver.awdlCoexBaTxPeer(&hardware, c, hardware.sc_generation, awdlCoexToken, clockUS + 1);
    assert(sent.size() == before);
    ++cases;
    // A further slot's own schedule: while it says the peer is away, the slot's aggregate queue pauses (its station
    // only) and resumes after, and a single frame for it waits behind its send gate, at most the gate's limit.
    reset();
    assert(deliver(request(10, 0, 1)) && deliver(request(11, 0, 2)));
    settle();
    {
        ItlAWDL::PeerSlot &d = awdlPeers[1];
        d.txState = kCoexBaTxAgreed;
        d.txQueue = 6;
        hardware.txq[6].queued = 3;                                         // frames waiting on its queue
        d.schedule.assumeZero = true;
        ItlAWDL::PeerSyncInfo info{};
        info.awCounter = 400;                                               // extended AW 100: entry 4
        info.downCounterTU = 20;
        info.awPeriodTU = 16;
        for (unsigned k = 0; k < 16; ++k)
            info.channels[k] = k < 4 ? 149 : 6;                             // on our channel in entries 0-3 only
        const uint64_t t0 = clockUS;
        d.schedule.note(info, t0, 149);
        d.scheduleToken = awdlCoexToken;
        assert(awdlPeerScheduleCurrent(d) && !awdlPeerScheduleCurrent(awdlPeers[2]));
        uint64_t away = 0;
        for (uint64_t t = t0; t < t0 + 2000000 && !away; t += 1000)
            if (d.schedule.shouldPause(t)) away = t;
        assert(away);
        clockUS = away;
        d.schedule.heardUS = away;
        commands.clear();
        driver.awdlCoexPeerPauseStep(&hardware, d);
        assert(d.pause.paused && commands.size() == 1 && commands[0].sta == 5 && commands[0].mask == 0xfe &&
               commands[0].flags == (1 << 4));                             // asynchronous DISABLE_TX
        assert(!awdlPeers[0].pause.paused);
        // Back on our channel: resumed (confirmed on station 5).
        uint64_t back = 0;
        for (uint64_t t = away; t < away + 2000000 && !back; t += 1000)
            if (!d.schedule.shouldPause(t)) back = t;
        clockUS = back + 2000;
        d.schedule.heardUS = clockUS;
        commands.clear();
        driver.awdlCoexPeerPauseStep(&hardware, d);
        assert(!d.pause.paused && commands.size() == 1 && commands[0].sta == 5 && commands[0].flags == 0x100);
        // The send gate (for frames sent singly): closed while away, open at the limit or when back.
        d.txState = kCoexBaTxAsking;
        clockUS = away + 16 * 65536;                                        // the same entry, one sequence later
        d.schedule.heardUS = clockUS;
        assert(d.schedule.shouldPause(clockUS));
        {
            // Asking follows slot 0's rule as well: no request is spent while the peer is away.
            const size_t sentBefore = sent.size();
            const uint32_t asksBefore = d.txAsks, skipsBefore = awdlPeerBaAsksSkippedAway;
            const bool bulkBefore = d.txBulk.bulk;
            d.txBulk.bulk = true;
            d.lastUS = clockUS;
            d.txLastAskUS = 0;
            assert(!driver.awdlBaAskPass(&hardware, d, 1, clockUS));
            assert(sent.size() == sentBefore && d.txAsks == asksBefore && awdlPeerBaAsksSkippedAway == skipsBefore + 1);
            d.txBulk.bulk = bulkBefore;
        }
        assert(awdlPeerGateClosed(d, clockUS));
        assert(!awdlPeerGateClosed(d, clockUS + 600000));                   // past the 450 ms ceiling: opens
        assert(awdlPeerGateForced == 1);
        awdlCoexGate = false;
        assert(!awdlPeerGateClosed(d, clockUS));                            // -itlawdlcoexnogate
        awdlCoexGate = true;
        d.scheduleToken = awdlCoexToken + 1;                                // a schedule from another listener
        assert(!awdlPeerGateClosed(d, clockUS));
        hardware.txq[6].queued = 0;
    }
    ++cases;
    // The firmware has no room for another receive session: that request is declined (status 37) and nothing is
    // quarantined, for slot 0 as for the others; the next request is handled as usual.
    reset();
    declineBaFor = 5;
    assert(deliver(request(10, 0, 1)) && deliver(request(11, 0, 2)));
    settle();
    assert(awdlPeers[0].rxTids == 1 && awdlPeers[1].added && !awdlPeers[1].rxTids);
    assert(awdlCoexBaFailedGen == -1 && awdlPeers[1].failedGen != hardware.sc_generation && awdlCoexBaRxDeclined == 1);
    {
        unsigned declined = 0;
        for (const auto &f : sent)
            declined += ItlAWDL::sameAddress(f.data() + 4, request(11, 0, 0).peer) && f[27] == 37;
        assert(declined == 1);
    }
    declineBaFor = 3;
    assert(deliver(request(10, 2, 3)));
    settle();
    assert(awdlCoexBaFailedGen == -1 && awdlPeers[0].rxTids == 1 && awdlCoexBaRxDeclined == 2);
    declineBaFor = -1;
    assert(deliver(request(10, 2, 4)));
    settle();
    assert(awdlPeers[0].rxTids == 0x5);
    ++cases;
    // The peer cancels (DELBA) while its station is being added: the station stays, no session is set up and no
    // answer goes. Before, the DELBA found no slot and the cancelled session was set up and answered.
    reset();
    duringAdd = [](int station) {
        if (station != 5)
            return;
        ItlAWDL::PeerSlot *slot = ItlAWDL::ownedSlotFor(awdlPeers, awdlPeerSlotsUsable, request(11, 0, 0).peer);
        assert(slot == &awdlPeers[1] && slot->adding);
        slot->rxTidTeardown |= 1;                                           // as iwx_rx_frame's DELBA branch does
    };
    assert(deliver(request(10, 0, 1)) && deliver(request(11, 0, 2)));
    settle();
    assert(awdlPeers[1].added && !awdlPeers[1].rxTids && count(0x18, 5, 0x8) == 0 && awdlCoexBaRxCancelled == 1);
    for (const auto &f : sent)
        assert(!ItlAWDL::sameAddress(f.data() + 4, request(11, 0, 0).peer));
    ++cases;
    // Another peer's request during the add goes to a free slot: the slot being added is taken. Before, it went to
    // that slot and was wiped once the add finished.
    reset();
    duringAdd = [](int station) {
        if (station == 3)
            assert(deliver(request(12, 0, 7)));
    };
    assert(deliver(request(10, 0, 1)));
    settle();
    assert(awdlPeers[0].added && ItlAWDL::sameAddress(awdlPeers[0].addr, request(10, 0, 0).peer));
    assert(awdlPeers[1].added && ItlAWDL::sameAddress(awdlPeers[1].addr, request(12, 0, 0).peer) && awdlPeers[1].rxTids == 1);
    ++cases;
    // A send agreement that failed before the firmware held a queue is tried again after 30 s.
    reset();
    queueFail = 1;
    assert(deliver(request(10, 0, 1)) && deliver(request(11, 0, 2)));
    settle();
    awdlPeers[1].txBulk.bulk = true;
    (void)driver.awdlCoexBaTxPeer(&hardware, awdlPeers[1], hardware.sc_generation, awdlCoexToken, clockUS);
    assert(awdlPeers[1].txState == kCoexBaTxFailed && awdlPeers[1].txQueue < 0);
    queueFail = 0;
    (void)driver.awdlCoexBaTxPeer(&hardware, awdlPeers[1], hardware.sc_generation, awdlCoexToken, clockUS + 1000000ULL);
    assert(awdlPeers[1].txState == kCoexBaTxFailed);                        // not yet
    const uint32_t retriesBefore = awdlPeerTxRetries;
    (void)driver.awdlCoexBaTxPeer(&hardware, awdlPeers[1], hardware.sc_generation, awdlCoexToken, clockUS + 31000000ULL);
    assert(awdlPeerTxRetries == retriesBefore + 1 && awdlPeers[1].txState != kCoexBaTxFailed);
    ++cases;
    printf("PASS %u multi-peer receive and send Block Ack scenarios on the real task steps (host mocks only)\n", cases);
}
'''

with tempfile.TemporaryDirectory(prefix="forbiddenitlwm-multipeer-") as directory:
    src = Path(directory) / "test.cpp"
    src.write_text(program)
    exe = Path(directory) / "test"
    subprocess.run(["xcrun", "clang++", "-std=c++14", "-I" + str(root / "include"),
                    "-fsanitize=address,undefined", str(src), "-o", str(exe)], check=True)
    print(subprocess.check_output([str(exe)], text=True), end="")
