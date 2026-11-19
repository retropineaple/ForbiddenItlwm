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
