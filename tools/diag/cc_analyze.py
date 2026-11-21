#!/usr/bin/env python3
"""Reliable analysis of a Continuity Camera / AWDL run from the driver's registry (ioreg), with checks that say
PASS / FAIL / INFO so a run can be judged the same way every time.

  python3 -I tools/diag/cc_analyze.py                    live ioreg
  python3 -I tools/diag/cc_analyze.py dump.txt           a saved `ioreg -l -w0`
  python3 -I tools/diag/cc_analyze.py before.txt after.txt   compare two dumps (counters as deltas)

What it reads: command ring health, Block Ack, the station's power table (what the firmware was sent and answered),
receive loss by 802.11 sequence number, where the gaps fall against the access point's beacons (and by beacon class),
the iPhone's receive rates, and what we advertise. Everything is a registry key the driver publishes; nothing is guessed.
"""
import re
import struct
import subprocess
import sys

W = [20, 40, 80, 160, 320]
MOD = {0: "CCK", 1: "LEG", 2: "HT", 3: "VHT", 4: "HE"}


class Reg:
    def __init__(self, text):
        self.t = text

    def num(self, key, default=None):
        m = re.search(r'"%s" = (-?\d+|Yes|No)\b' % re.escape(key), self.t)
        if not m:
            return default
        v = m.group(1)
        return {"Yes": 1, "No": 0}.get(v, None) if v in ("Yes", "No") else int(v) & 0xFFFFFFFF if int(v) < 0 else int(v)

    def text(self, key):
        m = re.search(r'"%s" = "([^"]*)"' % re.escape(key), self.t)
        return m.group(1) if m else None

    def data(self, key):
        m = re.search(r'"%s" = <([0-9a-f]+)>' % re.escape(key), self.t)
        return bytes.fromhex(m.group(1)) if m else None

    def u32s(self, key, n=None):
        d = self.data(key)
        if d is None:
            return None
        v = list(struct.unpack("<%dI" % (len(d) // 4), d))
        return v[:n] if n else v


def chi_square(counts):
    tot = sum(counts)
    if not tot:
        return 0.0, 0.0
    exp = tot / len(counts)
    return sum((c - exp) ** 2 / exp for c in counts), exp


def gap_periods(ring, count, lo=20.0, hi=1200.0, step=0.05):
    """Do the receive gaps repeat on a schedule? ring is the driver's gap ring (12-byte events: firmware time in microseconds,
    frames missing, ...), count how many it has recorded. For each candidate period the gaps' phases are averaged as unit
    vectors: the length R is 0 when gaps fall at random phases and 1 when they all fall at the same phase of the period.
    Returns (events, span in seconds, [(R, period in ms)] for the best distinct periods, {name: R} for the AWDL periods)."""
    import math
    n = len(ring) // 12
    events = [struct.unpack("<IHHHBB", ring[i * 12:i * 12 + 12]) for i in range(n)]
    have = min(count, n)
    order = [(count - have + i) % n for i in range(have)] if count >= n else list(range(have))
    stamps = sorted(((events[i][0] - events[order[0]][0]) & 0xFFFFFFFF) / 1000.0 for i in order)
    if len(stamps) < 16:
        return len(stamps), 0.0, [], {}

    def resultant(period):
        c = s = 0.0
        for t in stamps:
            a = 2 * math.pi * ((t % period) / period)
            c += math.cos(a)
            s += math.sin(a)
        return math.hypot(c, s) / len(stamps)

    scan = []
    p = lo
    while p <= hi:
        scan.append((resultant(p), p))
        p += step
    peaks = []
    for r, p in sorted(scan, reverse=True):
        # A period that fits shows up again at its sub-multiples (p / 2, p / 3, ...): those are not separate sources.
        if all(abs(p - q / k) > 0.03 * p for _, q in peaks for k in range(1, 9)):
            peaks.append((round(r, 3), round(p, 2)))
        if len(peaks) == 3:
            break
    named = {"16 TU (an availability window)": 16.384, "64 TU (an AWDL slot)": 65.536, "100 TU (the beacon interval)": 102.4,
             "110 TU (AWDL sync)": 112.64, "512 TU (8 slots)": 524.288}
    return len(stamps), stamps[-1] / 1000.0, peaks, {k: round(resultant(v), 3) for k, v in named.items()}


def gap_context(ring, ring_count, host, timeline, timeline_count, per_frame_ms=0.8, margin_ms=20.0):
    """What did the driver and firmware do just before each gap? Gaps (ring + host-clock stamps of when each ended, 12-byte and
    4-byte entries) are matched with the timeline (8-byte entries: host microseconds, kind, group, command). A gap's window is
    the margin plus the time its missing frames would have taken. Returns (gaps, [(signature, gaps with it in the window,
    expected by chance, events seen)]) for every signature; kinds are 1 scan start, 2 scan done, 3 firmware notification."""
    import math
    n = len(ring) // 12
    have = min(ring_count, n)
    order = [(ring_count - have + i) % n for i in range(have)] if ring_count >= n else list(range(have))
    gaps = []
    for i in order:
        missing = struct.unpack_from("<H", ring, i * 12 + 4)[0]
        end = struct.unpack_from("<I", host, i * 4)[0]
        gaps.append((end, missing))
    m = len(timeline) // 8
    thave = min(timeline_count, m)
    torder = [(timeline_count - thave + i) % m for i in range(thave)] if timeline_count >= m else list(range(thave))
    events = [struct.unpack_from("<IBBH", timeline, i * 8) for i in torder]
    if not gaps or not events:
        return len(gaps), []
    t0 = events[0][0]
    span_s = max(((events[-1][0] - t0) & 0xFFFFFFFF) / 1e6, 1e-3)
    sigs = {}
    for us, kind, group, cmd in events:
        sigs.setdefault((kind, group, cmd), []).append(us)
    out = []
    for sig, times in sigs.items():
        hits = 0
        expected = 0.0
        for end, missing in gaps:
            window = (margin_ms + missing * per_frame_ms) / 1000.0
            lo = (end - int(window * 1e6)) & 0xFFFFFFFF
            if any(((t - lo) & 0xFFFFFFFF) <= int(window * 1e6) for t in times):
                hits += 1
            expected += 1 - math.exp(-(len(times) / span_s) * window)
        out.append((sig, hits, expected, len(times)))
    out.sort(key=lambda r: -(r[1] - r[2]))
    return len(gaps), out


def verdict(ok, text):
    return ("PASS  " if ok else "FAIL  ") + text


def analyze(r, base=None):
    out = []
    def line(s=""):
        out.append(s)

    def n(key, d=0):
        v = r.num(key, d)
        if base is not None and v is not None and base.num(key) is not None:
            v = (v - base.num(key)) & 0xFFFFFFFF
        return v

    line("== command ring (a wedged ring made every later command fail with ENOSPC)")
    to, er, es, ep, rc = n("ITLAWDLCmdTimeouts"), n("ITLAWDLCmdEnospcRing"), n("ITLAWDLCmdEnospcSlot"), n("ITLAWDLCmdEnospcResp"), n("ITLAWDLCmdReclaimed")
    line(verdict(not (to or er or es or ep), f"timeouts {to}, ENOSPC ring/slot/resp {er}/{es}/{ep}, reclaimed {rc}, peak queue {r.num('ITLAWDLCmdQueuedPeak')}"))
    if to:
        line(f"      last timed-out command id 0x{r.num('ITLAWDLCmdLastTimeoutId') or 0:x}")

    line("== Block Ack and station")
    peer = r.num("ITLAWDLCoexBaPeerAdded"); tids = r.num("ITLAWDLCoexBaTids", 0)
    line(verdict(bool(peer), f"BaPeerAdded {peer}, rx TIDs 0x{tids:x} (TID 7 {'yes' if tids & 0x80 else 'NO'})"))

    line("== station power table (Apple's AWDL recipe: power save on, beacon abort on, DTIM skipped)")
    sent, fl, sk, er2 = r.num("ITLAWDLPmSent"), r.num("ITLAWDLPmFlags"), r.num("ITLAWDLPmSkip"), r.num("ITLAWDLPmErr")
    if sent is None:
        line("INFO  this driver build has no ITLAWDLPm* keys")
    else:
        bits = [name for bit, name in ((0, "power-save"), (1, "mgmt/beacon-abort"), (2, "skip-over-DTIM")) if fl & (1 << bit)]
        line(verdict(er2 == 0, f"{sent} power tables sent; last flags 0x{fl:x} [{', '.join(bits) or 'none'}], skip_dtim_periods {sk}, firmware answered {er2}"))
        line(verdict(bool(fl & 1), "power save is ON in the last table sent (a station that never sleeps is an always-active binding)"))
        rs, rf, re_, flips = r.num("ITLAWDLPmRecipeSent"), r.num("ITLAWDLPmRecipeFlags"), r.num("ITLAWDLPmRecipeErr"), r.num("ITLAWDLPmFlips")
        if rs is not None:
            line(verdict(rs == 0 or re_ == 0, f"{rs} recipe tables (AWDL data moving); last flags 0x{rf:x}, firmware answered {re_}"))
            line(verdict(flips is not None and flips <= 6, f"state flips {flips} (each power-save transition sends a null frame on the station's channel; more than a few means flapping)"))
        line(f"INFO  configured skip {r.num('ITLAWDLCoexSkipDtim')}, continuityPower {r.num('ITLAWDLContinuityPower')}")

    line("== receive loss by 802.11 sequence number (frames the iPhone sent that we never heard)")
    seen, miss = n("ITLAWDLCoexSeqSeen"), n("ITLAWDLCoexSeqMissing")
    if seen is not None and seen + miss:
        pct = 100.0 * miss / (seen + miss)
        line(f"{'PASS' if pct < 2 else 'FAIL'}  {miss} missing of {seen + miss} ({pct:.2f}%), {n('ITLAWDLCoexSeqGapRuns')} gaps, longest {r.num('ITLAWDLCoexSeqLongestGap')}, late {n('ITLAWDLCoexSeqLate')}")
        ts, tm = r.u32s("ITLAWDLCoexSeqTidSeen"), r.u32s("ITLAWDLCoexSeqTidMissing")
        if ts and tm:
            line("      by TID: " + ", ".join(f"{i}: {tm[i]}/{ts[i] + tm[i]}" for i in range(8) if ts[i] + tm[i]))
    else:
        line("INFO  no sequence data yet (camera not streaming?)")
    line(f"INFO  bad CRC {n('ITLAWDLRxMpduBadCrc')}, overrun {n('ITLAWDLRxMpduBadOverrun')}, duplicates {n('ITLAWDLRxDupDropped')}, llw0 delivered {n('ItlwmLLWRxDelivered')} failed {n('ItlwmLLWRxFailed')}")

    line("== gaps against the access point's beacons (the radio leaving the AWDL channel for the station)")
    bins, mis = r.u32s("ITLAWDLGapPhaseBins", 16), r.u32s("ITLAWDLGapPhaseMissing", 16)
    seenb = r.num("ITLAWDLBeaconsSeen")
    iv = r.num("ITLAWDLBeaconIntervalUS") or 102400
    if bins and sum(bins) >= 50 and seenb:
        chi, exp = chi_square(bins)
        top = sorted(range(16), key=lambda i: -bins[i])[:4]
        share = 100.0 * sum(bins[i] for i in top) / sum(bins)
        line(verdict(chi < 30.6, f"{sum(bins)} gaps over {seenb} beacons: chi-square {chi:.0f} (uniform if < 30.6); top 4 bins hold {share:.0f}% (uniform would be 25%)"))
        for i in range(16):
            line(f"      {i * iv / 16000:5.1f}-{(i + 1) * iv / 16000:5.1f} ms {bins[i]:5d} {'#' * int(36 * bins[i] / max(bins))}  missing {mis[i] if mis else '?'}")
        cl, cm, cc = r.data("ITLAWDLGapPhaseByClass"), r.data("ITLAWDLGapMissingByClass"), r.u32s("ITLAWDLBeaconClassCount")
        if cl and cc:
            c = struct.unpack("<48I", cl)
            names = ["ordinary beacon", "DTIM, nothing buffered", "DTIM, group traffic buffered"]
            line(f"      beacons by class {cc[:3]}, DTIM period {r.num('ITLAWDLBeaconDtimPeriod')}")
            for k in range(3):
                g = sum(c[16 * k:16 * k + 16])
                if g:
                    ck, _ = chi_square(list(c[16 * k:16 * k + 16]))
                    line(f"      after {names[k]:28s}: {g:5d} gaps ({100 * g / sum(bins):4.0f}%), chi-square {ck:.0f}")
    else:
        line("INFO  not enough gaps or beacons yet")

    line("== what the iPhone sends us (TID 7): modulation / width / streams / MCS")
    rates = r.u32s("ITLAWDLRxRatesTid7")
    if rates and sum(rates):
        tot = sum(rates)
        rows = []
        for i, cnt in enumerate(rates):
            if cnt:
                rows.append((cnt, MOD.get(i // 160, i // 160), W[(i // 32) % 5], ((i // 16) % 2) + 1, i % 16))
        rows.sort(reverse=True)
        wide = sum(c for c, m, w, s, mcs in rows if w > 20)
        two = sum(c for c, m, w, s, mcs in rows if s == 2)
        line(f"INFO  {tot} frames; wider than 20 MHz {100 * wide / tot:.1f}%; two streams {100 * two / tot:.1f}%")
        for cnt, m, w, s, mcs in rows[:6]:
            line(f"      {m:4s} {w:3d} MHz nss{s} mcs{mcs:2d}  {100 * cnt / tot:5.1f}%")

    line("== do the gaps repeat on a schedule? (phase concentration R of the gap times at each period: 0 random, 1 always the same phase)")
    ring, ring_count = r.data("ITLAWDLCoexSeqGapRing"), r.num("ITLAWDLCoexSeqGapRingCount")
    if ring and ring_count:
        n_ev, span, peaks, named = gap_periods(ring, ring_count)
        if peaks:
            import math
            # Many periods are tried, so the bar is the chance that the best of them reaches R: about span x (1/lo - 1/hi)
            # independent periods (frequency resolution 1 / span), each with P(R > x) = exp(-n x^2).
            independent = max(span * (1000 / 20.0 - 1000 / 1200.0), 1.0)
            bar = math.sqrt(math.log(independent / 0.001) / n_ev)
            line(f"INFO  {n_ev} gaps over {span:.1f} s; with this many gaps the best of all the periods tried reaches R {bar:.2f} by chance about once in a thousand runs")
            for rr, pp in peaks:
                line(f"      strongest period {pp:8.2f} ms: R {rr:.2f}  ({pp / 1.024:.1f} TU)")
            line("      AWDL periods: " + ", ".join(f"{k} R {v:.2f}" for k, v in named.items()))
        else:
            line("INFO  too few gaps in the ring yet")
    else:
        line("INFO  no gap ring published")
    line("== what happened just before each gap? (scan starts and ends, firmware notifications; window = 20 ms + the time the missing frames take)")
    host, tl, tl_count = r.data("ITLAWDLCoexSeqGapHostUS"), r.data("ITLAWDLTimeline"), r.num("ITLAWDLTimelineCount")
    if ring and ring_count and host and tl and tl_count:
        n_gaps, rows = gap_context(ring, ring_count, host, tl, tl_count)
        kinds = {1: "scan start", 2: "scan done", 3: "firmware notification"}
        line(f"INFO  {n_gaps} gaps, {tl_count} timeline events recorded; the signatures that came before gaps more often than chance:")
        shown = 0
        for (kind, group, cmd), hits, expected, seen in rows:
            if hits >= 3 and hits >= 2 * expected and shown < 6:
                line(f"      {kinds.get(kind, kind)} group {group} cmd 0x{cmd:02x}: before {hits} of {n_gaps} gaps, chance would give {expected:.1f} ({seen} events)")
                shown += 1
        if not shown:
            line("      none: no signature precedes gaps (3 or more, at least twice the chance rate)")
    else:
        line("INFO  no timeline published (an older build)")
    line("== where did each gap fall in the iPhone's 16-slot schedule? (a slot the iPhone announced on another channel cannot deliver to us)")
    slots, sched = r.data("ITLAWDLCoexSeqGapSlot"), r.data("ITLAWDLPeerSchedule")
    if slots and len(slots) >= 8:
        n = min(len(slots) // 8, ring_count or 0, 400)
        rows = [struct.unpack_from("<HBBBBBB", slots, 8 * i) for i in range(n)]
        known = [x for x in rows if x[5] == 1]
        if sched and len(sched) >= 28:
            line("INFO  the iPhone's announced channels per slot (own slot 0..15): " + " ".join(str(c) for c in sched[:16])
                 + f"; slot length {struct.unpack_from('<I', sched, 16)[0] / 1000:.1f} ms")
        if known:
            classes = ["fully explained by the iPhone being away (awayPct >= 90)", "partly (10-89)",
                       "not explained: it should have been on our channel (< 10)"]
            line(f"INFO  {len(known)} of {n} gaps have a known schedule; the rest: schedule unknown or older than the cache allows")
            line(f"      duration (ms) median {sorted(x[0] for x in known)[len(known) // 2]}, slots covered median {sorted(x[3] for x in known)[len(known) // 2]}")
            for label, test in ((classes[0], lambda x: x[4] >= 90), (classes[1], lambda x: 10 <= x[4] < 90), (classes[2], lambda x: x[4] < 10)):
                line(f"      {label}: {sum(1 for x in known if test(x))} gaps")
            starts = [0] * 16
            for x in known:
                starts[x[1] % 16] += 1
            line("      gaps starting in slot 0..15: " + " ".join(str(c) for c in starts))
            # How far into its slot each gap began (1/256 of a slot; 8 bins of 8.2 ms): a cluster in bin 0 is the iPhone's channel switch.
            for first_slot, label in ((None, "all slots"), (4, "slot 4"), (12, "slot 12")):
                bins = [0] * 8
                for x in known:
                    if first_slot is None or x[1] == first_slot:
                        bins[x[6] // 32] += 1
                line(f"      gap start offset in {label}, 8 bins of {1.024 * 64 / 8:.1f} ms: " + " ".join(str(c) for c in bins))
        else:
            line("INFO  no gap with a known schedule yet")
    else:
        line("INFO  no per-gap slot data published (an older build)")
    if slots and len(slots) >= 8 and ring and ring_count:
        # Sequence numbers the iPhone used while it was on another channel show up as missing frames in a gap that spans its away
        # slots (the 2026-11-20 run: 23 gaps of ~270 ms starting in the last slot of a 161 run held 587 of the 605 missing frames
        # that "started" in the tail of the run, with retries at their lowest there). They are not loss on our channel.
        ng = min(len(slots) // 8, len(ring) // 12, 400)
        tot = {"inside": 0, "partly": 0, "away": 0}
        cnt = {"inside": 0, "partly": 0, "away": 0}
        for i in range(ng):
            miss_n, tid = struct.unpack_from("<H", ring, 12 * i + 4)[0], ring[12 * i + 10]
            dur, st, en, span, away_pct, kn, _ = struct.unpack_from("<HBBBBBB", slots, 8 * i)
            if tid != 7 or kn != 1:
                continue
            k = "away" if away_pct >= 90 else "partly" if away_pct >= 10 else "inside"
            tot[k] += miss_n
            cnt[k] += 1
        total_missing = sum(tot.values())
        if total_missing:
            line("== how much of the loss is on our channel? (the last gaps by the share of each gap the iPhone had announced another channel)")
            line(f"INFO  missing TID 7 frames in the last {sum(cnt.values())} gaps: inside the iPhone's present windows {tot['inside']} ({100 * tot['inside'] / total_missing:.0f}%, {cnt['inside']} gaps), "
                 f"partly across its away slots {tot['partly']} ({100 * tot['partly'] / total_missing:.0f}%, {cnt['partly']} gaps), "
                 f"fully across them {tot['away']} ({100 * tot['away'] / total_missing:.0f}%, {cnt['away']} gaps)")
            seen_n, miss_total = r.num("ITLAWDLCoexSeqSeen"), r.num("ITLAWDLCoexSeqMissing")
            if seen_n and miss_total is not None:
                line(f"INFO  so about {100 * miss_total / (seen_n + miss_total) * tot['inside'] / total_missing:.1f}% of the frames sent were lost inside the present windows "
                     f"(of the {100 * miss_total / (seen_n + miss_total):.2f}% headline; the rest spans the iPhone's own away time)")
    heard, miss = r.u32s("ITLAWDLCellHeard"), r.u32s("ITLAWDLCellMissing")
    if heard and miss and len(heard) >= 128 and len(miss) >= 128 and sum(heard):
        pm = r.num("ITLAWDLPmRxTimeoutMs") or 0
        line(f"INFO  loss per 1000 frames sent, by slot and by 8th of the slot (missing / (heard + missing)); data timeout in the power table: {pm} ms")
        for s in range(16):
            h, m = heard[s * 8:s * 8 + 8], miss[s * 8:s * 8 + 8]
            if sum(h) + sum(m) < 200:
                continue
            cells = " ".join(f"{1000 * m[b] / (h[b] + m[b]):5.0f}" if h[b] + m[b] >= 50 else "    -" for b in range(8))
            line(f"      slot {s:2d}: {1000 * sum(m) / (sum(h) + sum(m)):6.1f} overall | {cells}  (frames heard {sum(h)}, missing {sum(m)})")
        none_m, none_h = r.num("ITLAWDLCellMissingNone"), r.num("ITLAWDLCellHeardNone")
        if none_m is not None:
            line(f"INFO  not placed in a cell (the schedule was not current): {none_m} missing frames of {sum(miss) + none_m}, {none_h} frames heard of {sum(heard) + (none_h or 0)}")
        retry, rate, mrate, rssi = (r.u32s("ITLAWDLCellRetry"), r.u32s("ITLAWDLCellRate"), r.u32s("ITLAWDLCellMissingRate"),
                                    r.u32s("ITLAWDLCellRssi"))
        if retry and rate and mrate and rssi and len(retry) >= 128 and len(rate) >= 384 and len(mrate) >= 384 and len(rssi) >= 128:
            def signed(v):
                return v - (1 << 32) if v >= 1 << 31 else v
            line("INFO  per cell, to tell on-air errors from frames never sent: retries (flagged frames heard per 1000 frames heard), MCS 10-11 share of frames heard (%), mean signal (dBm)")
            for s in range(16):
                h, m = heard[s * 8:s * 8 + 8], miss[s * 8:s * 8 + 8]
                if sum(h) + sum(m) < 200:
                    continue
                rt = " ".join(f"{1000 * retry[s * 8 + b] / h[b]:5.0f}" if h[b] >= 50 else "    -" for b in range(8))
                hi = " ".join(f"{100 * rate[(s * 8 + b) * 3 + 2] / h[b]:5.0f}" if h[b] >= 50 else "    -" for b in range(8))
                sg = " ".join(f"{signed(rssi[s * 8 + b]) / h[b]:5.0f}" if h[b] >= 50 else "    -" for b in range(8))
                line(f"      slot {s:2d} retries {rt}")
                line(f"              MCS10+ % {hi}")
                line(f"              signal   {sg}")
                classes = []
                for c, name in enumerate(("MCS0-7", "MCS8-9", "MCS10-11")):
                    hc = sum(rate[(s * 8 + b) * 3 + c] for b in range(8))
                    mc = sum(mrate[(s * 8 + b) * 3 + c] for b in range(8))
                    classes.append(f"{name} {1000 * mc / (hc + mc):.0f}" if hc + mc >= 50 else f"{name} -")
                line("              loss per 1000 by the rate before the gap: " + ", ".join(classes))
    elif r.num("ITLAWDLPmRxTimeoutMs") is None:
        line("INFO  no per-slot loss denominator published (an older build)")
    seqs = [r.data("ItlwmAWDLRawSeq%d" % i) for i in range(4)]
    for i, raw in enumerate([s for s in seqs if s and len(s) >= 16]):
        ents = []
        for j in range(min(raw[5] + 1, (len(raw) - 16) // 12)):
            ch, fl, third = struct.unpack_from("<III", raw, 16 + 12 * j)
            ents.append(f"{ch}/0x{fl:x}" + ("" if third == 1 else f"/{third}"))
        line(f"INFO  requested channel sequence #{i} (distinct, in the order asked): header {raw[:16].hex()} entries {' '.join(ents)}")
    line("== what macOS asked for (the requests as the driver stored them)")
    for name, label in (("AWDL_MASTER_CHANNEL", "master channel"), ("AWDL_SECONDARY_MASTER_CHANNEL", "secondary master channel")):
        raw = r.data("ItlwmAWDLRaw_" + name)
        if raw and len(raw) >= 8:
            line(f"INFO  {label}: version {struct.unpack_from('<I', raw, 0)[0]}, channel {struct.unpack_from('<I', raw, 4)[0]}")
    raw = r.data("ItlwmAWDLRaw_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE")
    if raw and len(raw) >= 16:
        # A 16-byte header, then 12-byte entries (read off a live request: channel, width/band flags, and a 1): the 3-byte
        # apple80211_channel_sequence in the repo's header does not describe them. The flags are apple80211_channel_flag.
        def flags_text(f):
            names = [(0x2, "20MHz"), (0x4, "40MHz"), (0x400, "80MHz"), (0x800, "160MHz"), (0x8, "2GHz"), (0x10, "5GHz"), (0x100, "DFS")]
            return "|".join(n for bit, n in names if f & bit) or "none"
        entries = []
        for i in range(min(raw[5] + 1, (len(raw) - 16) // 12)):
            ch, fl, third = struct.unpack_from("<III", raw, 16 + 12 * i)
            entries.append(f"{ch}[{flags_text(fl)}]" + ("" if third == 1 else f"/{third}"))
        line(f"INFO  channel sequence: header length byte {raw[5]}, encoding {raw[6]}, step {raw[7]}, duplicates {raw[8]}, fill {raw[9]}; slots: {' '.join(entries)}")
    elif raw is None:
        line("INFO  no requested channel data published (an older build)")
    line("== is the loss packet error at the iPhone's high rates? (TID 7: gaps by the rate of the frame heard just before each gap)")
    prev, nxt = r.u32s("ITLAWDLLossByPrevRate"), r.u32s("ITLAWDLLossByNextRate")
    frames = [0] * 32
    for i, cnt in enumerate(rates or []):
        frames[((i // 16) % 2) * 16 + (i % 16)] += cnt
    if prev and len(prev) >= 64 and sum(frames):
        total_frames, total_gaps = sum(frames), sum(prev[:32])
        line(f"INFO  {total_gaps} gaps after a TID 7 frame, {sum(prev[32:64])} frames missing, over {total_frames} TID 7 frames heard")
        low = [0, 0]
        high = [0, 0]
        for idx in range(32):
            if frames[idx] < 200:
                continue
            gaps, miss = prev[idx], prev[32 + idx]
            bucket = high if idx & 0xf >= 8 else low
            bucket[0] += frames[idx]
            bucket[1] += gaps
            line(f"      nss{2 if idx & 16 else 1} mcs{idx & 0xf:2d}: {frames[idx]:7d} frames ({100 * frames[idx] / total_frames:4.1f}%), "
                 f"{gaps:5d} gaps = {1000 * gaps / frames[idx]:5.1f} per 1000 frames, {miss:6d} missing")
        if low[0] >= 200 and high[0] >= 200:
            lo, hi = 1000 * low[1] / low[0], 1000 * high[1] / high[0]
            line(f"INFO  gaps per 1000 frames: MCS 0-7 {lo:.1f}, MCS 8-11 {hi:.1f} (MCS 8 and up are the high rates; a ratio far above 1 points at packet error, near 1 at something rate-independent)")
        if nxt and len(nxt) >= 64:
            line("      the frame after a gap is at: " + ", ".join(f"nss{2 if i & 16 else 1} mcs{i & 0xf}={nxt[i]}" for i in sorted(range(32), key=lambda k: -nxt[k])[:4] if nxt[i]))
    else:
        line("INFO  not published (an older build, or no gaps yet)")
    hist = r.u32s("ITLAWDLRxRssiTid7")
    if hist and sum(hist):
        count = sum(hist)
        mean = -r.num("ITLAWDLRxRssiSumTid7", 0) / count
        line(f"INFO  signal of the iPhone's TID 7 frames: mean {mean:.1f} dBm over {count} frames")
        line("      " + ", ".join(f"{'>=-30' if b == 0 else '<=-90' if b == 15 else str(-30 - 4 * (b + 1)) + '..' + str(-30 - 4 * b)}: {100 * hist[b] / count:.0f}%" for b in range(16) if hist[b]))
    resyncs = r.num("ITLAWDLCoexSeqResyncs")
    if resyncs is not None:
        line(f"INFO  sequence tracker resyncs {resyncs} (a stall of more than 2,048 frames; late frames are counted until the tracker re-syncs after 8)")

    line("== do both receive chains hear the iPhone? (TID 7 frames, chain A minus chain B in dB; the signal above is the stronger chain)")
    cdiff, ccount, cnone, csum = r.u32s("ITLAWDLChainDiff"), r.u32s("ITLAWDLChainCount"), r.u32s("ITLAWDLChainNone"), r.u32s("ITLAWDLChainSum")
    if cdiff and ccount and cnone and csum and len(cdiff) >= 42 and len(ccount) >= 2 and len(cnone) >= 4 and len(csum) >= 4:
        def signed(v):
            return v - (1 << 32) if v >= 1 << 31 else v
        for s, name in ((0, "one stream"), (1, "two streams")):
            frames = ccount[s]
            if frames < 100:
                line(f"INFO  {name}: {frames} frames, too few")
                continue
            both = sum(cdiff[s * 21:(s + 1) * 21])
            if not both:
                line(f"INFO  {name}: {frames} frames, a chain reported no energy in all of them (A none {cnone[s * 2]}, B none {cnone[s * 2 + 1]})")
                continue
            mean_a, mean_b = signed(csum[s * 2]) / both, signed(csum[s * 2 + 1]) / both
            bins = cdiff[s * 21:(s + 1) * 21]
            weak = sum(bins[:5]) + sum(bins[16:])      # more than 10 dB apart
            line(f"INFO  {name}: {frames} frames; chain A mean {mean_a:.1f} dBm, chain B mean {mean_b:.1f} dBm (A-B {mean_a - mean_b:+.1f} dB); "
                 f"a chain reported nothing: A {cnone[s * 2]}, B {cnone[s * 2 + 1]}; {100 * weak / both:.1f}% of frames more than 10 dB apart")
            line("      A-B in 2 dB bins from -20 to +20: " + " ".join(str(v) for v in bins))
    else:
        line("INFO  not published (an older build)")
    line("== what we advertise to the iPhone (TLV 18 operating classes: 124/125 = 20 MHz, 128 = 80)")
    for key, label in (("ITLAWDLCapOwnTlv18", "ours as macOS built it"), ("ITLAWDLCapOwnSentTlv18", "ours as sent"), ("ITLAWDLCapPeerTlv18", "the iPhone's")):
        d = r.data(key)
        if d and len(d) > 7:
            pairs = [(d[6 + 2 * i], d[7 + 2 * i]) for i in range(d[0] + 1) if 7 + 2 * i < len(d)]
            line(f"INFO  {label}: {sorted(set(pairs))}")
    line(f"INFO  listener width {r.num('ITLAWDLCoexOwnWidth')} MHz on channel {r.num('ITLAWDLCoexChannel')}, overlap with station channel {r.num('ITLAWDLCoexOverlap')}, firmware PHY info {r.data('ITLAWDLPhyCmdInfo').hex(' ') if r.data('ITLAWDLPhyCmdInfo') else 'n/a'}")
    return "\n".join(out)


def load(path):
    if path:
        return Reg(open(path, errors="replace").read())
    return Reg(subprocess.run(["ioreg", "-l", "-w0"], capture_output=True, text=True).stdout)


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) == 2:
        print(analyze(load(args[1]), load(args[0])))
    else:
        print(analyze(load(args[0] if args else None)))
