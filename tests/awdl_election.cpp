// AWDL election and the master's clock (include/AWDLElection.hpp), macOS's plan on that clock
// (AWDLSessionFollow.hpp), and the election our frames advertise (AWDLPeerProbe.hpp, AWDLLocalSchedule.hpp).
#include <AWDLElection.hpp>
#include <AWDLSessionFollow.hpp>
#include <AWDLPeerProbe.hpp>
#include <AWDLLocalSchedule.hpp>
#include <assert.h>
#include <stdio.h>
#include <string.h>

using namespace ItlAWDL;

static void put32(uint8_t *p, uint32_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24); }

// An election v2 TLV (type 24, 40 bytes of value) at `t`.
static size_t putElection(uint8_t *t, const uint8_t *master, const uint8_t *sync, uint32_t masterCounter,
                          uint32_t height, uint32_t masterMetric, uint32_t selfMetric, uint32_t selfCounter) {
    t[0] = 24; t[1] = 40; t[2] = 0;
    uint8_t *v = t + 3;
    memset(v, 0, 40);
    memcpy(v, master, 6);
    memcpy(v + 6, sync, 6);
    put32(v + 12, masterCounter);
    put32(v + 16, height);
    put32(v + 20, masterMetric);
    put32(v + 24, selfMetric);
    put32(v + 36, selfCounter);
    return 43;
}

static ElectionView view(const uint8_t *master, const uint8_t *sync, uint32_t counter, uint32_t height,
                         uint32_t masterMetric, uint32_t selfMetric) {
    ElectionView v{};
    memcpy(v.master, master, 6);
    memcpy(v.sync, sync, 6);
    v.masterCounter = counter;
    v.height = height;
    v.masterMetric = masterMetric;
    v.selfMetric = selfMetric;
    return v;
}

int main() {
    const uint8_t self[6] = {0x02, 0, 0, 0, 0x11, 0x11};
    const uint8_t phone[6] = {0x02, 0, 0, 0, 0x22, 0x22};
    const uint8_t pad[6] = {0x02, 0, 0, 0, 0x33, 0x33};
    const uint64_t t0 = 50000000;

    // Parsing: an action frame's TLV 24 at byte 40, after other TLVs.
    {
        uint8_t f[200] = {};
        f[0] = 0xd0;
        size_t i = 40;
        f[i] = 6; f[i + 1] = 2; f[i + 2] = 0; i += 5;   // some other TLV first
        i += putElection(f + i, phone, phone, 7, 0, 600, 600, 7);
        ElectionView v{};
        assert(electionFromAction(f, i, &v));
        assert(!memcmp(v.master, phone, 6) && !memcmp(v.sync, phone, 6) && v.masterCounter == 7 && v.height == 0 &&
               v.masterMetric == 600 && v.selfMetric == 600 && v.selfCounter == 7);
        // Truncated: refused, out untouched.
        ElectionView w{};
        w.height = 99;
        assert(!electionFromAction(f, i - 1, &w) && w.height == 99);
        f[40 + 5] = 24; f[40 + 6] = 30;   // a short TLV 24 first: skipped, not misread
        assert(!electionFromTLVs(f + 40 + 5, 33, &w));
    }

    // Election: alone, we're our own master on our own clock.
    Election e{};
    ElectionResult r = e.run(self, 100, 0, t0);
    assert(!r.following && !memcmp(r.master, self, 6) && r.height == 0 && !r.clockValid);
    assert(r.clock.anchorEndUS == ownClock().anchorEndUS);

    // A phone that is its own master with a higher metric: we follow it, height 1, and take its clock.
    PeerSyncInfo s{};
    s.awCounter = 4000;           // AW 4000: extended AW 1000
    s.downCounterTU = 40;         // 40 TU left in it
    s.awPeriodTU = 16;
    ElectionView pv = view(phone, phone, 0, 0, 500, 500);
    e.note(phone, &pv, &s, t0);
    r = e.run(self, 100, 0, t0 + 1000);
    assert(r.following && !memcmp(r.master, phone, 6) && !memcmp(r.sync, phone, 6) && r.height == 1 &&
           r.masterMetric == 500 && r.clockValid);
    assert(r.clock.anchorEaw == 1000 && r.clock.anchorEndUS == t0 + 40 * 1024 && r.clock.eawUS == 65536);

    // A lower metric doesn't win; a higher master counter beats a higher metric.
    {
        Election e2{};
        ElectionView low = view(phone, phone, 0, 0, 50, 50);
        e2.note(phone, &low, &s, t0);
        assert(!e2.run(self, 100, 0, t0).following);
        ElectionView counter = view(phone, phone, 3, 0, 50, 50);
        e2.note(phone, &counter, &s, t0);
        assert(e2.run(self, 100, 0, t0).following);
    }

    // A peer that syncs to us (or names us master) is never chosen: no cycles.
    {
        Election e2{};
        ElectionView child = view(self, self, 0, 1, 900, 900);
        e2.note(pad, &child, nullptr, t0);
        assert(!e2.run(self, 100, 0, t0).following);
    }

    // Same master through two peers: the closer one wins; on equal distance, the higher address.
    {
        Election e2{};
        ElectionView direct = view(phone, phone, 0, 0, 500, 500);
        ElectionView viaPhone = view(phone, phone, 0, 1, 500, 300);
        e2.note(pad, &viaPhone, nullptr, t0);
        e2.note(phone, &direct, &s, t0);
        ElectionResult q = e2.run(self, 100, 0, t0);
        assert(q.following && !memcmp(q.sync, phone, 6) && q.height == 1);
        Election e3{};
        const uint8_t low[6] = {0x02, 0, 0, 0, 0x01, 0x01};
        ElectionView a = view(phone, phone, 0, 1, 500, 300), b = a;
        e3.note(low, &a, nullptr, t0);
        e3.note(pad, &b, nullptr, t0);
        q = e3.run(self, 100, 0, t0);
        assert(q.following && !memcmp(q.sync, pad, 6) && q.height == 2);
    }

    // Too deep a tree is refused; stale peers drop out; a stale clock isn't used.
    {
        Election e2{};
        ElectionView deep = view(phone, pad, 0, kElectionMaxHeight, 900, 900);
        e2.note(pad, &deep, nullptr, t0);
        assert(!e2.run(self, 100, 0, t0).following);
        Election e3{};
        e3.note(phone, &pv, &s, t0);
        assert(e3.run(self, 100, 0, t0 + kElectionClockStaleUS - 1).clockValid);
        ElectionResult q = e3.run(self, 100, 0, t0 + kElectionClockStaleUS);
        assert(q.following && !q.clockValid);
        assert(!e3.run(self, 100, 0, t0 + kElectionPeerStaleUS).following);
        assert(e3.count(t0) == 1 && e3.count(t0 + kElectionPeerStaleUS) == 0);
    }

    // The table holds kElectionPeers; a ninth replaces the oldest.
    {
        Election e2{};
        for (unsigned i = 0; i < kElectionPeers + 1; ++i) {
            uint8_t a[6] = {0x02, 0, 0, 0, 0x40, uint8_t(i)};
            ElectionView v = view(a, a, 0, 0, 10, 10);
            e2.note(a, &v, nullptr, t0 + i);
        }
        assert(e2.count(t0 + 100) == kElectionPeers);
        bool firstGone = true;
        for (const auto &p : e2.peers) if (p.used && p.addr[5] == 0) firstGone = false;
        assert(firstGone);
    }

    // Clock arithmetic: our own clock is uptime / 64 TU; a peer clock as anchored.
    {
        const Clock own = ownClock();
        uint64_t into = 0;
        assert(clockEawAt(own, 0, &into) == 0 && into == 0);
        assert(clockEawAt(own, 65536 * 7 + 5, &into) == 7 && into == 5);
        assert(clockEawStart(own, 7) == 65536 * 7);
        const Clock pc = clockFromSync(s, t0);   // eaw 1000 ends at t0 + 40 TU
        assert(clockEawAt(pc, t0, &into) == 1000 && into == 24 * 1024);
        assert(clockEawAt(pc, t0 + 40 * 1024, &into) == 1001 && into == 0);
        assert(clockEawStart(pc, 1001) == t0 + 40 * 1024 && clockEawStart(pc, 999) == t0 + 40 * 1024 - 2 * 65536);
        // macOS's plan on the peer's clock: runs line up with the peer's entries (1000 % 16 == 8).
        const uint8_t plan[16] = {149, 149, 149, 149, 149, 149, 36, 36, 6, 149, 149, 149, 149, 149, 36, 36};
        FollowRun run{};
        // At t0 we're in entry 1000 (index 8: channel 6); next 149 run is entries 1001-1005 (index 9-13).
        assert(nextPlanRun(plan, 149, t0, pc, &run) && run.startUS == t0 + 40 * 1024 && run.entries == 5);
        // The own-clock overload is unchanged.
        FollowRun a{}, b{};
        assert(nextPlanRun(plan, 149, 1234567890, &a) && nextPlanRun(plan, 149, 1234567890, ownClock(), &b));
        assert(a.startUS == b.startUS && a.endUS == b.endUS && a.entries == b.entries);
    }

    // A clock re-anchored on a later frame from the same master is the same clock (within drift); another
    // numbering, a whole-window jump or another window length is not.
    {
        const Clock pc = clockFromSync(s, t0);
        Clock later = pc;   // the same master heard 3 windows on, 40 us of drift
        later.anchorEaw = pc.anchorEaw + 3;
        later.anchorEndUS = pc.anchorEndUS + 3 * pc.eawUS + 40;
        assert(clockShiftUS(pc, later) == 40 && clockShiftUS(later, pc) == -40);
        assert(clockSameWithin(pc, later, 1000) && clockSameWithin(later, pc, 1000));
        assert(clockSameWithin(pc, pc, 0));
        Clock early = later;
        early.anchorEndUS = later.anchorEndUS - 1100;   // 1060 us the other way
        assert(clockShiftUS(pc, early) == -1060 && !clockSameWithin(pc, early, 1000));
        Clock renumbered = pc;   // same instants, numbering one window off: every run lands on another entry
        renumbered.anchorEaw = pc.anchorEaw + 1;
        assert(!clockSameWithin(pc, renumbered, 1000));
        Clock period = pc;
        period.eawUS = pc.eawUS + 1024;
        assert(!clockSameWithin(pc, period, 1000000));
        // Wraps of the 32-bit window number are taken the short way.
        Clock a = pc, b = pc;
        a.anchorEaw = 0xfffffffeU;
        b.anchorEaw = 1;
        b.anchorEndUS = a.anchorEndUS + 3 * a.eawUS + 7;
        assert(clockShiftUS(a, b) == 7 && clockSameWithin(a, b, 1000));
    }

    // Our sync TLV carries the clock: epoch and AW counter reproduce the peer's counter and down counter.
    {
        const Clock pc = clockFromSync(s, t0);
        const uint64_t now = t0 + 3000;   // 37 TU and a bit left in entry 1000
        uint64_t epoch = 0;
        uint16_t aw = 0;
        clockEpoch(pc, now, &epoch, &aw);
        assert(epoch <= now && aw == 4000 && epoch == t0 + 40 * 1024 - 65536);
        SyncPrefix sp = {149, 149, 0, 0, 16, 110, 0x1000, 16, 16, 0, 3, 3, 3, 3, {2, 0, 0, 0, 0x22, 0x22}};
        uint8_t out[73];
        size_t n = 0;
        assert(buildLocalFixedSchedule(sp, epoch, now, aw, out, 73, &n) == BuildResult::Ready);
        const uint16_t down = uint16_t(out[1] | out[2] << 8), counter = uint16_t(out[29] | out[30] << 8);
        // The counter counts AWs (16 TU), as for our own clock: 26 TU into extended AW 1000 is AW 4001.
        assert(counter == 4000 + ((now - epoch) / 1024) / 16 && counter / 4 == 1000 && down == 64 - (now - epoch) / 1024);
        // Our own clock: epoch at the current entry's start, counter 4 per entry.
        clockEpoch(ownClock(), 65536 * 9 + 100, &epoch, &aw);
        assert(epoch == 65536 * 9 && aw == 36);
    }

    // Frames: TLV 5 and macOS's TLV 24 carry the election; our own metric and counter stay macOS's.
    {
        uint8_t base[400] = {};
        size_t len = 0;
        len += putElection(base, self, self, 0, 0, 100, 100, 9);   // macOS's template: itself as master
        uint32_t m = 0, c = 0;
        assert(templateSelfElection(base, len, &m, &c) && m == 100 && c == 9);
        ElectionAdvert adv{};
        memcpy(adv.master, phone, 6);
        memcpy(adv.sync, phone, 6);
        adv.height = 1;
        adv.masterMetric = 500;
        adv.masterCounter = 4;
        uint8_t tl[400];
        memcpy(tl, base, 400);
        size_t tlen = len;
        assert(extendWithPeerTLVs(tl, &tlen, sizeof(tl), self, 149, 128, false, nullptr, &adv));
        // TLV 5 first: master = phone, distance 1, master metric 500, self metric 100.
        assert(tl[0] == 5 && !memcmp(tl + 8, phone, 6) && tl[6] == 1);
        assert(electionRead32(tl + 14) == 500 && electionRead32(tl + 18) == 100);
        // TLV 24 (after TLVs 5 and 18): master, sync, counter, distance, master metric; self fields kept.
        ElectionView v{};
        assert(electionFromTLVs(tl, tlen, &v));
        assert(!memcmp(v.master, phone, 6) && !memcmp(v.sync, phone, 6) && v.masterCounter == 4 && v.height == 1 &&
               v.masterMetric == 500 && v.selfMetric == 100 && v.selfCounter == 9);
        // Without an advert: the old frame, byte for byte.
        uint8_t a[400], b[400];
        memcpy(a, base, 400);
        memcpy(b, base, 400);
        size_t alen = len, blen = len;
        assert(extendWithPeerTLVs(a, &alen, sizeof(a), self, 149, 128));
        assert(extendWithPeerTLVs(b, &blen, sizeof(b), self, 149, 128, false, nullptr, nullptr));
        assert(alen == blen && !memcmp(a, b, alen));
        assert(!memcmp(a + 8, self, 6) && a[6] == 0);
    }

    // the election RSSI rule.
    {
        const uint8_t far[6] = {0x02, 0, 0, 0, 0x44, 0x44};
        // A distant device with a far higher master counter, heard at -85 dBm.
        ElectionView fv = view(far, far, 46000, 0, 510, 510);
        Election e4{};
        e4.note(far, &fv, &s, t0, -85);
        assert(e4.run(self, 100, 0, t0).following);                       // rule off: it wins (the flipping seen in testing)
        ElectionResult q = e4.run(self, 100, 0, t0, kElectionDefaultMinRssi);
        assert(!q.following && q.weakSkipped == 1);                       // rule on: left out, we stay our own master
        // The near phone (-45 dBm) with a lower counter wins while the far one is skipped.
        ElectionView nv = view(phone, phone, 8000, 0, 520, 520);
        e4.note(phone, &nv, &s, t0, -45);
        q = e4.run(self, 100, 0, t0, kElectionDefaultMinRssi);
        assert(q.following && !memcmp(q.sync, phone, 6) && q.weakSkipped == 1);
        // Smoothing: one strong frame from the far device doesn't make it eligible at once.
        e4.note(far, &fv, &s, t0 + 100, -60);                              // (3 * -85 + -60) / 4 = -78.75 -> -78
        assert(e4.peers[0].rssiDbm == -78 || e4.peers[1].rssiDbm == -78);
        e4.note(far, &fv, &s, t0 + 200, -60);                              // -73: now eligible, and it wins on counter
        q = e4.run(self, 100, 0, t0 + 200, kElectionDefaultMinRssi);
        assert(q.following && !memcmp(q.sync, far, 6) && q.weakSkipped == 0);
        // A peer whose RSSI isn't known stays eligible; out-of-range values are ignored.
        Election e5{};
        e5.note(far, &fv, &s, t0);                                         // no RSSI
        e5.note(far, &fv, &s, t0, 12);                                     // not a dBm value: ignored
        assert(e5.run(self, 100, 0, t0, kElectionDefaultMinRssi).following);
    }

    // IO80211's own election rules (runApple).
    {
        const uint8_t far[6] = {0x02, 0, 0, 0, 0x44, 0x44};
        PeerSyncInfo good = s;
        good.awExtTU = 16;
        good.awCommonTU = 16;
        good.presenceMode = 4;
        AppleElectionInputs in{};
        in.selfMetric = 539;
        in.minRssi5Dbm = -65;
        in.hostEdgeDbm = -45;
        // The scene seen in testing: the phone (its own root, metric 520) near; a far device with a far higher counter
        // (metric 510), even heard strongly. OWL follows the far one; IO80211 compares root metrics across trees,
        // so the phone's tree beats it, and our 539 beats the phone's 520: we are root (macOS expected exactly that).
        Election a{};
        ElectionView phoneV = view(phone, phone, 8470, 0, 520, 520);
        ElectionView farV = view(far, far, 46213, 0, 510, 510);
        a.note(phone, &phoneV, &good, t0, -40, true, 0x3e);
        a.note(far, &farV, &good, t0, -50, true, 0x3e);
        assert(a.run(self, 539, 0, t0).following && !memcmp(a.run(self, 539, 0, t0).sync, far, 6));
        ElectionResult q = a.runApple(self, in, t0);
        assert(!q.following && !memcmp(q.master, self, 6));
        in.selfMetric = 500;                                   // a lower own metric: follow the phone's tree
        q = a.runApple(self, in, t0);
        assert(q.following && !memcmp(q.sync, phone, 6) && q.height == 1 && q.clockValid);
        in.selfMetric = 520;                                   // equal: follow (only a higher own metric is root)
        assert(a.runApple(self, in, t0).following);
        in.selfMetric = 500;
        // Same root R via two relays: lower hop count wins at equal RLFC; a fresher RLFC wins, and the relay with
        // the older RLFC for that root is refused.
        {
            const uint8_t root[6] = {0x02, 0, 0, 0, 0x55, 0x55};
            const uint8_t r1[6] = {0x02, 0, 0, 0, 0x61, 0x61}, r2[6] = {0x02, 0, 0, 0, 0x62, 0x62};
            Election b{};
            ElectionView v1 = view(root, root, 100, 1, 600, 300), v2 = view(root, root, 100, 2, 600, 300);
            b.note(r1, &v1, &good, t0, -40, true, 0x3e);
            b.note(r2, &v2, &good, t0, -40, true, 0x3e);
            q = b.runApple(self, in, t0);
            assert(q.following && !memcmp(q.sync, r1, 6) && q.height == 2);
            ElectionView v2b = view(root, root, 101, 2, 600, 300);
            b.note(r2, &v2b, &good, t0, -40, true, 0x3e);
            q = b.runApple(self, in, t0);
            assert(q.following && !memcmp(q.sync, r2, 6) && q.olderSkipped == 1);
        }
        // Each refusal rule, one peer at a time (the peer would otherwise win: metric 900).
        auto lone = [&](const ElectionView &v, const PeerSyncInfo *ps, int32_t rssi, bool on5, int ver) {
            Election c{};
            c.note(phone, &v, ps, t0, rssi, on5, ver);
            return c.runApple(self, in, t0);
        };
        ElectionView strong = view(phone, phone, 10, 0, 900, 900);
        assert(lone(strong, &good, -40, true, 0x3e).following);
        assert(lone(strong, &good, -40, true, -1).following);                       // version unknown: eligible
        q = lone(strong, &good, -40, true, 0x30);
        assert(!q.following && q.legacySkipped == 1);                              // legacy version
        ElectionView zero = view(phone, phone, 0, 0, 900, 900);
        q = lone(zero, &good, -40, true, 0x3e);
        assert(!q.following && q.legacySkipped == 1);                              // RLFC 0
        q = lone(strong, &good, -70, true, 0x3e);
        assert(!q.following && q.weakSkipped == 1);                                // 5 GHz below -65
        q = lone(strong, &good, -40, false, 0x3e);
        assert(!q.following && q.weakSkipped == 1);                                // 2.4 GHz only: -40-10 < -45
        assert(lone(strong, &good, -30, false, 0x3e).following);                   // -30-10 = -40 >= -45
        assert(lone(strong, nullptr, kElectionNoRssi, true, 0x3e).following);      // no RSSI reading: eligible
        PeerSyncInfo badAw = good;
        badAw.awExtTU = 32;
        q = lone(strong, &badAw, -40, true, 0x3e);
        assert(!q.following && q.badAwSkipped == 1);                               // AW params not 16 TU
        badAw = good;
        badAw.presenceMode = 6;
        assert(lone(strong, &badAw, -40, true, 0x3e).badAwSkipped == 1);           // presence mode not 1..4
        ElectionView prohibited = view(kElectionProhibitedRoot, phone, 10, 1, 900, 900);
        q = lone(prohibited, &good, -40, true, 0x3e);
        assert(!q.following && q.loopSkipped == 1);                                // prohibited root
        ElectionView viaUs = view(phone, self, 10, 1, 900, 900);
        assert(lone(viaUs, &good, -40, true, 0x3e).loopSkipped == 1);              // syncs to us
        in.haveElectionId = true;
        in.electionId = 0;
        ElectionView otherId = strong;
        otherId.electionId = 7;
        q = lone(otherId, &good, -40, true, 0x3e);
        assert(!q.following && q.idSkipped == 1);                                  // another election ID
        assert(lone(strong, &good, -40, true, 0x3e).following);                    // same ID (0)
        // The parsers: election ID at offset 28 of TLV 24, version from TLV 21.
        uint8_t tl[3 + 40 + 3 + 2] = {24, 40, 0};
        put32(tl + 3 + 28, 0x1234);
        tl[43] = 21; tl[44] = 2; tl[45] = 0; tl[46] = 0x3e; tl[47] = 8;
        ElectionView pv2{};
        uint8_t ver = 0;
        assert(electionFromTLVs(tl, sizeof(tl), &pv2) && pv2.electionId == 0x1234);
        assert(electionVersionFromTLVs(tl, sizeof(tl), &ver) && ver == 0x3e);
    }

    // macOS's own election (ELECTION_PARAMS 155, ADVERTISERS 229; layouts from IO80211 programChipWithNewMaster).
    {
        uint8_t params[0x38] = {};
        put32(params + 0x08, 1);
        params[0x12] = 0x34; params[0x13] = 0x12;     // election id 0x1234
        params[0x14] = 1;                              // hop count
        memcpy(params + 0x16, phone, 6);               // root
        put32(params + 0x1c, 524);
        put32(params + 0x20, 515);
        uint8_t adv[0x2c] = {};
        memcpy(adv + 0x26, phone, 6);
        MacOSElectionChoice c{};
        const uint64_t tm = t0 + 2000000;
        // parsing, and refusals: short buffer, wrong version, no or group parent address
        assert(!parseMacOSElectionParams(params, kMacOSElectionParamsLength - 1, &c, tm) && !c.valid);
        params[0x08] = 2;
        assert(!parseMacOSElectionParams(params, sizeof(params), &c, tm) && !c.valid);
        params[0x08] = 1;
        assert(parseMacOSAdvertisers(adv, sizeof(adv), &c, tm - 1000) && c.haveParent && !memcmp(c.parent, phone, 6));
        assert(parseMacOSElectionParams(params, sizeof(params), &c, tm));
        assert(c.valid && c.hopCount == 1 && c.electionId == 0x1234 && !memcmp(c.root, phone, 6) &&
               c.rootMetric == 524 && c.selfMetric == 515 && c.atUS == tm);
        MacOSElectionChoice bad{};
        uint8_t noAddr[0x2c] = {};
        assert(!parseMacOSAdvertisers(noAddr, sizeof(noAddr), &bad, tm) && !bad.haveParent);
        noAddr[0x26] = 0x01; noAddr[0x2b] = 5;
        assert(!parseMacOSAdvertisers(noAddr, sizeof(noAddr), &bad, tm) && !bad.haveParent);
        assert(!parseMacOSAdvertisers(adv, kMacOSAdvertisersLength - 1, &bad, tm));

        // macOS follows the phone (one hop): we sync to the phone's clock even where our own rules wouldn't.
        Election m{};
        PeerSyncInfo ms{};
        ms.awCounter = 8000; ms.downCounterTU = 20; ms.awPeriodTU = 16;
        ElectionView mv = view(phone, phone, 0, 0, 524, 524);
        m.note(phone, &mv, &ms, tm - 500000, -66);   // below IO80211's -65: runApple wouldn't follow
        AppleElectionInputs in{};
        in.selfMetric = 515; in.minRssi5Dbm = -65; in.hostEdgeDbm = -45;
        assert(!m.runApple(self, in, tm + 1000).following);
        bool ok = false;
        ElectionResult mr = m.runMacOS(self, c, tm + 1000, &ok);
        assert(ok && mr.following && !memcmp(mr.sync, phone, 6) && !memcmp(mr.master, phone, 6) && mr.height == 1 &&
               mr.masterMetric == 524 && mr.clockValid && mr.clock.anchorEaw == 2000);
        // the parent from ADVERTISERS wins over the root (two hops: root far away, parent the phone)
        MacOSElectionChoice two = c;
        two.hopCount = 2;
        memcpy(two.root, pad, 6);
        mr = m.runMacOS(self, two, tm + 1000, &ok);
        assert(ok && mr.following && !memcmp(mr.sync, phone, 6) && !memcmp(mr.master, pad, 6) && mr.height == 2);
        // two hops without a fresh parent: unresolved (the root isn't whom to sync to)
        two.haveParent = false;
        mr = m.runMacOS(self, two, tm + 1000, &ok);
        assert(!ok && !mr.following);
        two.haveParent = true; two.parentAtUS = two.atUS - kMacOSParentMatchUS;   // too old for this programming
        mr = m.runMacOS(self, two, tm + 1000, &ok);
        assert(!ok);
        // macOS is master (hop count 0): our own clock, resolved
        MacOSElectionChoice own = c;
        own.hopCount = 0;
        mr = m.runMacOS(self, own, tm + 1000, &ok);
        assert(ok && !mr.following && !memcmp(mr.master, self, 6) && !mr.clockValid &&
               mr.clock.anchorEndUS == ownClock().anchorEndUS);
        // stale choice, or before it arrived: unresolved
        mr = m.runMacOS(self, c, tm + kMacOSChoiceStaleUS, &ok);
        assert(!ok);
        mr = m.runMacOS(self, c, tm - 1, &ok);
        assert(!ok);
        // the parent isn't a peer heard here (or not recently): unresolved
        mr = m.runMacOS(self, c, tm - 500000 + kElectionPeerStaleUS, &ok);
        assert(!ok);
        MacOSElectionChoice other = c;
        memcpy(other.parent, pad, 6); memcpy(other.root, pad, 6);
        mr = m.runMacOS(self, other, tm + 1000, &ok);
        assert(!ok);
        // a parent that is us: unresolved, never a loop
        MacOSElectionChoice loop = c;
        memcpy(loop.parent, self, 6);
        mr = m.runMacOS(self, loop, tm + 1000, &ok);
        assert(!ok && !mr.following);
        // followed but the clock is old: following, own clock until a fresh sync TLV
        mr = m.runMacOS(self, c, tm - 500000 + kElectionClockStaleUS + 1, &ok);
        assert(ok && mr.following && !mr.clockValid);
        // no choice at all
        MacOSElectionChoice none{};
        mr = m.runMacOS(self, none, tm, &ok);
        assert(!ok);
    }

    printf("election: TLV 24 parsing, OWL rules, cycles, depth, staleness, table, clocks, sync TLV, frames: passed\n");
    printf("Apple election rules (root metric across trees, RLFC/HC in a tree, self root, all refusals): passed\n");
    printf("election RSSI rule (skip below the minimum, smoothing, unknown RSSI eligible): passed\n");
    printf("macOS's election (ELECTION_PARAMS/ADVERTISERS parsing, follow parent or root, own, stale, unknown, loop): passed\n");
    return 0;
}
