#include <AWDLPeerSchedule.hpp>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>


using namespace ItlAWDL;

// An action frame with a 33-byte sync TLV and a 38-byte channel sequence TLV.
static size_t buildFrame(uint8_t *f, uint16_t awCounter, uint16_t down, uint16_t period, const uint8_t *seq,
                         uint8_t seqCount = 15) {
    memset(f, 0, 200);
    f[0] = 0xd0;
    size_t i = 40;
    f[i] = 4; f[i + 1] = 33; f[i + 2] = 0;
    uint8_t *v = f + i + 3;
    v[0] = 149; v[1] = uint8_t(down); v[2] = uint8_t(down >> 8); v[3] = 149;
    v[5] = uint8_t(period); v[6] = uint8_t(period >> 8);
    v[27] = 4;
    v[29] = uint8_t(awCounter); v[30] = uint8_t(awCounter >> 8);
    i += 3 + 33;
    f[i] = 18; f[i + 1] = 38; f[i + 2] = 0;
    v = f + i + 3;
    v[0] = seqCount; v[1] = 3; v[3] = 3; v[4] = 0xff; v[5] = 0xff;
    for (unsigned k = 0; k < 16; ++k) {
        v[6 + 2 * k] = seq[k];
        v[7 + 2 * k] = seq[k] == 6 ? 81 : seq[k] ? 128 : 0;
    }
    i += 3 + 38;
    return i;
}

int main() {
    uint8_t seq[16];
    for (unsigned k = 0; k < 16; ++k) seq[k] = k < 11 ? 149 : k < 15 ? 0 : 6;
    uint8_t f[200];
    size_t n = buildFrame(f, 400, 20, 16, seq);  // AW 400 = extended AW 100, 20 TU left in it.

    PeerSyncInfo info{};
    assert(peerSyncFromAction(f, n, &info));
    assert(info.awCounter == 400 && info.downCounterTU == 20 && info.awPeriodTU == 16 && info.channels[11] == 0);
    // Refused: truncated, a sequence of another length, a counter longer than the extended AW.
    PeerSyncInfo untouched{}; untouched.awCounter = 7;
    assert(!peerSyncFromAction(f, n - 1, &untouched) && untouched.awCounter == 7);
    uint8_t g[200]; size_t m = buildFrame(g, 400, 20, 16, seq, 7);
    assert(!peerSyncFromAction(g, m, &untouched) && untouched.awCounter == 7);
    m = buildFrame(g, 400, 65, 16, seq);
    assert(!peerSyncFromAction(g, m, &untouched));
    for (unsigned field : {1U, 2U, 3U, 4U, 7U}) {
        m = buildFrame(g, 400, 20, 16, seq);
        g[40 + 36 + 3 + field] = 99;
        bool badSequence = false;
        assert(!peerSyncFromAction(g, m, &untouched, &badSequence) && badSequence);
    }

    static PeerSchedule s; s.reset();
    const uint64_t t0 = 10000000;
    s.note(info, t0, 149);
    const uint64_t eaw = 65536;
    assert(s.eawUS == eaw && s.anchorEaw == 100 && s.anchorEndUS == t0 + 20 * 1024);
    uint64_t into = 0;
    assert(s.eawAt(t0, &into) == 100 && into == eaw - 20 * 1024);
    assert(s.eawAt(t0 + 20 * 1024, &into) == 101 && into == 0);
    assert(s.eawAt(t0 + 20 * 1024 + 3 * eaw + 5, &into) == 104 && into == 5);
    assert(s.eawAt(t0 - 100000, &into) == 99 && into == 10592);

    // The peer is really on 149 when (eaw + 5) % 16 is below 11. Frames heard only then.
    const unsigned trueOffset = 5;
    assert(!s.shouldPause(t0));
    unsigned sent = 0;
    for (uint64_t t = t0; sent < 400; t += 7919) {
        uint64_t in = 0;
        const uint32_t e = s.eawAt(t, &in);
        s.heardUS = t;  // The peer keeps sending sync frames; the anchor itself stays exact.
        if (seq[(e + trueOffset) % 16] == 149) { s.notePeerFrame(t); ++sent; }
    }
    assert(s.calibrated && s.offset == trueOffset && s.calibrations == 1);

    // Pausing: through away entries, from 3 ms before to 1 ms after.
    uint32_t firstAway = 0;
    for (uint32_t e = 101; ; ++e) if (seq[(e + trueOffset) % 16] != 149 && seq[(e - 1 + trueOffset) % 16] == 149) { firstAway = e; break; }
    const uint64_t awayStart = s.anchorEndUS + uint64_t(firstAway - 101) * eaw;
    const uint64_t now = awayStart - 10000;
    s.heardUS = now;  // Keep it fresh for these checks.
    assert(!s.shouldPause(awayStart - 4000));
    assert(s.shouldPause(awayStart - 2000));
    assert(s.shouldPause(awayStart + 30000));
    uint32_t back = firstAway;
    while (seq[(back + trueOffset) % 16] != 149) ++back;
    const uint64_t backStart = s.anchorEndUS + uint64_t(back - 101) * eaw;
    s.heardUS = backStart - 1000;
    assert(s.shouldPause(backStart + 500));
    assert(!s.shouldPause(backStart + 1500));
    // A pause may last through the whole predicted away run, plus margins.
    unsigned runEntries = back - firstAway;
    s.heardUS = awayStart - 10000;
    {
        const uint64_t lim = s.pauseLimitUS(awayStart - 2000);
        const uint64_t expect = 2000 + uint64_t(runEntries) * eaw + kPeerPauseEarlyUS + kPeerResumeLateUS + 2000;
        assert(lim == (expect > kPeerPauseCeilingUS ? kPeerPauseCeilingUS : expect));
        const uint64_t mid = s.pauseLimitUS(awayStart + 10000);  // Already inside the run.
        const uint64_t expectMid = (eaw - 10000) + uint64_t(runEntries - 1) * eaw + kPeerResumeLateUS + 2000;
        assert(mid == (expectMid > kPeerPauseCeilingUS ? kPeerPauseCeilingUS : expectMid));
    }
    s.heardUS = backStart - 1000;
    // AW timing: the peer's AWs in present entries, and the wait until the next one.
    {
        const uint64_t aw = eaw / 4;
        s.heardUS = backStart - 1000;
        assert(s.awUS() == aw);
        assert(s.inPresentAwStart(backStart + 500, 2000));           // Start of the first AW back.
        assert(!s.inPresentAwStart(backStart + 2500, 2000));         // Past the window.
        assert(s.inPresentAwStart(backStart + aw + 100, 2000));      // The next AW in the same entry.
        assert(!s.inPresentAwStart(awayStart + 100, 2000));          // An away entry.
        s.heardUS = awayStart - 1000;
        assert(s.usUntilPresentAw(awayStart + 100) == backStart - (awayStart + 100));  // Waits out the away run.
        s.heardUS = awayStart - 3 * aw;
        assert(s.usUntilPresentAw(awayStart - 2 * aw - 100) == 100); // Next AW, still present.
    }
    // Stale schedule: no pause.
    assert(!s.shouldPause(s.heardUS + kPeerScheduleStaleUS + 1));

    // A changed sequence restarts the checks but keeps the calibration and the offset evidence.
    uint8_t seq2[16]; memcpy(seq2, seq, 16); seq2[0] = 0;
    n = buildFrame(f, uint16_t(back * 4), 64, 16, seq2);
    assert(peerSyncFromAction(f, n, &info));
    s.heardUS = backStart - 1000;
    s.checkedFrames = 399;
    s.note(info, backStart, 149);
    assert(s.sequenceChanges == 1 && s.checkedFrames == 0 && s.calibrated);
    s.note(info, backStart + kPeerPhaseJumpUS + 1, 149);
    assert(!s.calibrated && s.decalibrations == 1);

    PeerSchedule wrap{};
    PeerSyncInfo rollover = info;
    rollover.awCounter = 65532;
    wrap.note(rollover, t0, 149);
    wrap.calibrated = true;
    rollover.awCounter = 0;
    wrap.note(rollover, t0 + eaw, 149);
    assert(wrap.calibrated);

    // Frames in away entries undo the calibration.
    static PeerSchedule bad; bad.reset();
    n = buildFrame(f, 400, 20, 16, seq);
    assert(peerSyncFromAction(f, n, &info));
    bad.note(info, t0, 149);
    bad.calibrated = true; bad.offset = 0;
    for (uint64_t t = t0; bad.checkedFrames < 199 && bad.calibrated; t += 5003) bad.notePeerFrame(t);
    for (uint64_t t = t0 + 2000000; bad.calibrated && t < t0 + 3000000; t += 5003) { bad.heardUS = t; bad.notePeerFrame(t); }
    assert(!bad.calibrated && bad.decalibrations == 1);
    // gf75-ax201: a phone on a busy network changes its sequence every few seconds. Fewer than 300 frames are
    // heard under any one sequence, and the offset is still found from all of them together.
    {
        static PeerSchedule hop; hop.reset();
        const uint8_t seqA[16] = {149,149,149,149, 36,36,36,36, 6, 149,149,149, 36,36,36,36};
        const uint8_t seqB[16] = {149,149, 36,36,36,36,36,36, 149,149,149,149,149, 36,36,36};
        const uint8_t seqC[16] = {36,36,36, 149,149,149,149,149,149, 36,36, 6, 149,149,149,149};
        const uint8_t *seqs[3] = {seqA, seqB, seqC};
        const unsigned hopOffset = 3;
        uint8_t g[200]; PeerSyncInfo hi{};
        size_t gn = buildFrame(g, 400, 20, 16, seqA);
        assert(peerSyncFromAction(g, gn, &hi));
        hop.note(hi, t0, 149);
        unsigned heard = 0, which = 0, sinceChange = 0, maxUnderOne = 0;
        for (uint64_t t = t0; heard < 900 && !hop.calibrated; t += 7919) {
            uint64_t in = 0;
            const uint32_t e = hop.eawAt(t, &in);
            hop.heardUS = t;
            if (sinceChange >= 90) {          // A new sequence, announced with exact counters.
                which = (which + 1) % 3; sinceChange = 0;
                memcpy(hop.channels, seqs[which], 16); ++hop.sequenceChanges; hop.clearChecks();
            }
            if (seqs[which][(e + hopOffset) % 16] == 149) { hop.notePeerFrame(t); ++heard; ++sinceChange; }
            if (sinceChange > maxUnderOne) maxUnderOne = sinceChange;
        }
        assert(maxUnderOne < kPeerCalibrateFrames);
        assert(hop.calibrated && hop.offset == hopOffset && hop.sequenceChanges >= 3);
        // A stale gap (the phone unheard for 3 s) drops the calibration but not the evidence: the next
        // sync frame re-anchors the same counters and the first frame after it calibrates again.
        const uint32_t kept = hop.scoredFrames;
        const uint64_t later = hop.heardUS + 3000000;
        uint64_t in2 = 0;
        const uint32_t eLater = hop.eawAt(later, &in2);
        gn = buildFrame(g, uint16_t(eLater * 4), uint16_t((hop.eawUS - in2) / 1024), 16, seqs[which]);
        assert(peerSyncFromAction(g, gn, &hi));
        hop.note(hi, later, 149);
        assert(!hop.calibrated && hop.decalibrations == 1 && hop.scoredFrames == kept && kept >= kPeerCalibrateFrames);
        for (uint64_t t = later + 2000; !hop.calibrated && t < later + 1500000; t += 7919) {
            uint64_t in = 0;
            const uint32_t e = hop.eawAt(t, &in);
            if (seqs[which][(e + hopOffset) % 16] == 149) hop.notePeerFrame(t);
        }
        assert(hop.calibrated && hop.offset == hopOffset && hop.calibrations == 2);
    }
    // No away entry at all: nothing to calibrate against.
    static PeerSchedule all; all.reset();
    uint8_t seqAll[16]; memset(seqAll, 149, 16);
    n = buildFrame(f, 400, 20, 16, seqAll);
    assert(peerSyncFromAction(f, n, &info));
    all.note(info, t0, 149);
    for (uint64_t t = t0; t < t0 + 1900000; t += 3001) all.notePeerFrame(t);
    assert(!all.calibrated && all.binFrames >= kPeerCalibrateFrames);

    // Pause state: changes only, a cap, and a hold-off after it.
    PeerPauseState p{};
    assert(p.step(true, 1000) == 1 && p.paused && p.pauses == 1);
    assert(p.step(true, 2000) == 0);
    assert(p.step(false, 61000) == -1 && !p.paused && p.pausedTotalUS == 60000);
    assert(p.step(true, 70000) == 1);
    assert(p.step(true, 70000 + kPeerPauseMaxUS) == -1 && p.forcedResumes == 1);
    assert(p.step(true, 70000 + kPeerPauseMaxUS + 1000) == 0 && !p.paused);
    assert(p.step(true, 70000 + kPeerPauseMaxUS + kPeerPauseHoldOffUS) == 1);
    // A longer limit given at the start of a pause holds for that pause.
    PeerPauseState q{};
    assert(q.step(true, 1000, 330000) == 1);
    assert(q.step(true, 1000 + 200000) == 0 && q.paused);
    assert(q.step(true, 1000 + 330000) == -1 && q.forcedResumes == 1);
    // Pausing allowed only during sustained sending.
    PeerBulkState b{};
    assert(!b.note(1000000, 0));
    assert(!b.note(1100000, 500));            // Too soon to check.
    assert(!b.note(1250000, 100));            // 400 a second: replies, not a send.
    assert(b.note(1500000, 400) && b.changes == 1);   // 1,200 a second.
    assert(b.note(1750000, 450));             // One quiet check.
    assert(b.note(2000000, 1000));            // Busy again: the quiet count restarts.
    assert(b.note(2250000, 1010));            // Quiet once.
    assert(!b.note(2500000, 1020) && b.changes == 2);  // Quiet twice: off.
    assert(!b.note(2400000, 1020));           // A clock that went back restarts the window.
    // -itlawdlcoexawsync0: sync timing from an uncalibrated schedule takes offset 0, ignores a stale
    // offset left by an earlier calibration, and never makes pauses or holds usable.
    {
        PeerSchedule u{};
        u.valid = true;
        u.eawUS = 65536;
        u.anchorEaw = 15;                 // Extended AW 16 (entry 0) starts at anchorEndUS.
        u.anchorEndUS = 10000000;
        u.heardUS = u.anchorEndUS - 1000;
        u.ourChannel = 149;
        for (unsigned k = 0; k < 16; ++k) u.channels[k] = k < 4 ? 149 : 36;
        u.offset = 5;                     // Stale: must not be used while uncalibrated.
        const uint64_t e0 = u.anchorEndUS, aw = u.awUS();
        assert(!u.timingUsable(e0 + 100, false) && u.timingUsable(e0 + 100, true));
        assert(!u.inPresentAwStart(e0 + 100, 2000) && !u.usUntilPresentAw(e0 + 100));
        assert(u.inPresentAwStart(e0 + 100, 2000, true));               // Entry 0, start of its first AW.
        assert(!u.inPresentAwStart(e0 + 3000, 2000, true));             // Past the window.
        assert(u.inPresentAwStart(e0 + 3 * u.eawUS + aw + 50, 2000, true));   // Entry 3, second AW.
        assert(!u.inPresentAwStart(e0 + 4 * u.eawUS + 50, 2000, true)); // Entry 4: away.
        u.heardUS = e0 + 4 * u.eawUS;
        assert(u.usUntilPresentAw(e0 + 4 * u.eawUS + 50, true) == 12 * u.eawUS - 50);  // Next entry 0.
        assert(!u.shouldPause(e0 + 4 * u.eawUS + 50));                  // Pauses still need calibration.
        assert(u.pauseLimitUS(e0 + 4 * u.eawUS + 50) == kPeerPauseMaxUS);
        assert(!u.timingUsable(u.heardUS + kPeerScheduleStaleUS + 1, true));  // Stale: nothing.
        u.calibrated = true;              // Calibrated: its own offset applies again.
        assert(u.inPresentAwStart(e0 + 11 * u.eawUS + 50, 2000, true)); // (27 + 5) % 16 == 0.
    }
    // iface161: offset 0 assumed until refuted. Pauses and the send gate work uncalibrated; the same check
    // that revokes a calibration refutes it; a new anchor gives it a fresh check; reset keeps the policy.
    {
        PeerSchedule z{};
        z.assumeZero = true;
        z.valid = true;
        z.eawUS = 65536;
        z.anchorEaw = 15;
        z.anchorEndUS = 10000000;
        z.heardUS = z.anchorEndUS;
        z.ourChannel = 149;
        // The phone's "Data + Infra Data" sequence: 149 x4, 36 x4, 6, 149 x3, 36 x4 (7 of 16).
        const uint8_t data7[16] = {149, 149, 149, 149, 36, 36, 36, 36, 6, 149, 149, 149, 36, 36, 36, 36};
        memcpy(z.channels, data7, 16);
        z.offset = 5;                     // Ignored: not calibrated.
        const uint64_t e0 = z.anchorEndUS, E = z.eawUS, aw = z.awUS();
        assert(z.known() && !z.calibrated && z.presentEntries() == 7);
        assert(!z.shouldPause(e0 + 10000) && z.shouldPause(e0 + 4 * E + 10000));    // Entry 0 here, 4 away.
        assert(z.shouldPause(e0 + 4 * E - 1000));                                     // Early margin.
        assert(z.shouldPause(e0 + 9 * E + 500) && !z.shouldPause(e0 + 9 * E + 2000)); // Late margin.
        assert(z.pauseLimitUS(e0 + 4 * E) > 4 * E && z.pauseLimitUS(e0 + 4 * E) <= kPeerPauseCeilingUS);
        using G = PeerSchedule::SendGate;
        assert(z.sendGate(e0 + 10000, false, 4000) == G::Open);
        assert(z.sendGate(e0 + 5 * E, false, 4000) == G::Away);
        assert(z.sendGate(e0 + 10000, true, 4000) == G::Doze);           // Idle: past the AW start window.
        assert(z.sendGate(e0 + aw + 2000, true, 4000) == G::Open);       // Second AW of entry 0, its start.
        assert(z.sendGate(e0 + 5 * E, true, 4000) == G::Away);           // Away wins over doze.
        assert(z.sendGate(z.heardUS + kPeerScheduleStaleUS + 1, true, 4000) == G::Open);  // Stale: no gate.
        // Without the policy nothing changes from iface160: pauses need a calibration.
        PeerSchedule c = z;
        c.assumeZero = false;
        assert(!c.known() && !c.shouldPause(e0 + 5 * E) && c.sendGate(e0 + 5 * E, true, 4000) == G::Open);
        // Frames from the peer in away entries, across entries, refute the assumption.
        for (unsigned k = 0; k < 300; ++k) {
            const unsigned entry = k % 2 ? 4 + (k / 2) % 4 : (k / 2) % 4;   // Half in away entries 4 to 7.
            z.heardUS = e0 + entry * E;
            z.notePeerFrame(e0 + entry * E + 5000 + (k % 7) * 1000);
        }
        assert(z.refuted && !z.known() && z.refutations == 1 && !z.shouldPause(e0 + 5 * E));
        z.invalidate();                   // A new anchor clears it.
        assert(!z.refuted);
        z.valid = true;
        assert(z.known());
        z.reset();
        assert(z.assumeZero && !z.valid && !z.refuted);
        // Frames that agree with offset 0 keep it, and calibrate to offset 0.
        PeerSchedule k{};
        k.assumeZero = true;
        k.valid = true; k.eawUS = E; k.anchorEaw = 15; k.anchorEndUS = e0; k.heardUS = e0; k.ourChannel = 149;
        memcpy(k.channels, data7, 16);
        const unsigned present[7] = {0, 1, 2, 3, 9, 10, 11};
        for (unsigned n = 0; n < 400; ++n) {
            const unsigned entry = present[n % 7];
            k.heardUS = e0 + entry * E;
            k.notePeerFrame(e0 + entry * E + 3000 + (n % 50) * 1000);
        }
        assert(k.calibrated && k.offset == 0 && !k.refuted && k.refutations == 0);
    }
    printf("AWDL peer schedule and pause: passed\n");
    return 0;
}
