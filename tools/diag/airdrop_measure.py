#!/usr/bin/env python3
"""Measure an AirDrop transfer end to end: per-interface throughput, the driver's counters and sharingd's own result.

  python3 -I tools/diag/airdrop_measure.py --seconds 120        start it, then send the file within the window
  python3 -I tools/diag/airdrop_measure.py --seconds 120 --json run.json --min-mbit 60
  python3 -I tools/diag/airdrop_measure.py --replay run.json    judge a saved run again (no sampling)

While it runs it samples `netstat -I <if> -b` for each interface once per --interval seconds, reads the driver's
registry counters (read-only ioreg) before and after, and afterwards classifies sharingd's log for the window:
the send-finish and receive-finish results (size, bytesPerSec, transport, duration) and the failure signatures.

It reports, and turns into PASS / FAIL lines:
  where the bytes went     awdl0 + llw0 (through itlwm) against en0 / en1; en0 above 1 % fails (the 2026-11-20 captures
                           had the Mac -> iPhone bulk data leaving on the wired en0)
  how fast                 mean, median, 95th percentile and peak Mbit/s over the active seconds, against --min-mbit
  stalls                   runs of idle seconds inside the active period longer than --max-stall
  the MTUs                 awdl0 and llw0 at Apple's 1500; a wired interface above awdl0's MTU fails
  the driver's counters    unicast failure ratio, frames dropped, frames parked (and the ring's size), command ring errors,
                           block ack frames sent and acknowledged, availability windows posted
  sharingd's verdict       each finished send or receive, and the signatures (EMSGSIZE, not connected, timeout, ...)

Exit status 1 when anything FAILs, so it can sit in a check script. The tests are tests/airdrop_measure_test.py.
"""
import argparse
import json
import re
import statistics
import subprocess
import sys
import time

IFACES = ["awdl0", "llw0", "en0", "en1"]

REG_KEYS = [
    "ITLAWDLCoexDataTx", "ITLAWDLCoexDataRxUcast", "ITLAWDLCoexUcastOK", "ITLAWDLCoexUcastFailed",
    "ITLAWDLCoexBaTxFrames", "ITLAWDLCoexBaTxAcked", "ITLAWDLCoexGateHolds", "ITLAWDLCoexTxAcVo",
    "ItlwmAWDLTxPacketsOffered", "ItlwmAWDLTxDropped", "ItlwmAWDLTxParked", "ItlwmAWDLTxPassedParked",
    "ItlwmAWDLTxParkedPeak", "ItlwmAWDLTxParkFrames", "ItlwmAWDLAwStarts", "ItlwmAWDLAwEnds",
    "ItlwmLLWRxDelivered", "ItlwmLLWRingQueued", "ItlwmLLWRingDropped",
    "ITLAWDLCmdTimeouts", "ITLAWDLCmdEnospcRing", "ITLAWDLCmdEnospcSlot",
    "ITLAWDLCoexCountryPolls", "ITLAWDLCoexCountryLearned", "ITLAWDLCoexChannel",
]
# Read once per sample (ioreg -l -d1 takes about 40 ms) so a stall can be explained by what the driver was doing in it.
TIMELINE_KEYS = [
    "ITLAWDLCoexGateAwayTicks", "ITLAWDLCoexGateHolds", "ITLAWDLCoexGateForced", "ITLAWDLCoexGateDozeTicks",
    "ItlwmAWDLTxPacketsOffered", "ItlwmAWDLTxParked", "ItlwmAWDLTxPassedParked", "ITLAWDLCoexDataTx", "ITLAWDLCoexUcastOK",
    "ITLAWDLCoexUcastFailed", "ITLAWDLCoexOrderHeld", "ITLAWDLCoexPeerPauses", "ITLAWDLCoexPeerSequenceChanges",
    "ITLAWDLCoexPeer5GHzChanges", "ITLAWDLCoexPeerDecalibrations", "ITLAWDLCoexChannel", "ITLAWDLCoexPeer5GHz",
]

# Counters that only grow within a run; the others are levels and are reported as they are at the end.
LEVEL_KEYS = {"ItlwmAWDLTxParkedPeak", "ItlwmAWDLTxParkFrames", "ITLAWDLCoexCountryLearned", "ITLAWDLCoexChannel"}

SIGNATURES = [
    ("emsgsize", re.compile(r"Message too long|unexpected-40|EMSGSIZE")),
    ("not-connected", re.compile(r"unexpected-57|ENOTCONN|Socket is not connected")),
    ("missingEndpoint", re.compile(r"failureSubType\"?\s*:\s*\"?missingEndpoint")),
    ("timeout", re.compile(r"failureSubType\"?\s*:\s*\"?(?:timeout|timedOut|connectionTimeout)")),
    ("cancelled", re.compile(r"failureSubType\"?\s*:\s*\"?cancelled")),
    ("companion", re.compile(r"Failed to create the companion low latency interface|Timed out while waiting for the low latency")),
]
FAIL_SIGNATURES = ("emsgsize", "not-connected", "missingEndpoint", "timeout", "companion")


def run(cmd, timeout=30):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.stdout
    except Exception as e:  # noqa: BLE001
        return ""


# ---- parsing (pure) -------------------------------------------------------------------------------------------------

def parse_netstat(text, name):
    """(bytes in, bytes out, packets in, packets out) from the link row of `netstat -I name -b`, or None."""
    for line in text.splitlines():
        f = line.split()
        if not f or f[0].rstrip("*") != name or "<Link#" not in line:
            continue
        nums = [x for x in f if x.isdigit()]
        if len(nums) >= 6:
            # Ipkts Ierrs Ibytes Opkts Oerrs Obytes Coll are the last seven numeric columns on the link row.
            return int(nums[-5]), int(nums[-2]), int(nums[-7]), int(nums[-4])
    return None


def parse_registry(text, keys=REG_KEYS):
    out = {}
    for key in keys:
        m = re.search(r'"%s" = (-?\d+|Yes|No)\b' % re.escape(key), text)
        if m:
            v = m.group(1)
            out[key] = {"Yes": 1, "No": 0}[v] if v in ("Yes", "No") else int(v)
    return out


def parse_log(text):
    """Finished transfers and failure signature counts from `log show` text of sharingd."""
    counts, transfers = {}, []
    for line in text.splitlines():
        for name, rx in SIGNATURES:
            if rx.search(line):
                counts[name] = counts.get(name, 0) + 1
        kind = "send" if "airdrop.send-finish" in line else "receive" if "airdrop.receive-finish" in line else None
        if kind:
            f = {"kind": kind, "stamp": line[:23]}
            for key in ("result", "failureSubType", "size", "bytesPerSec", "transport", "duration"):
                m = re.search(r'\\?"%s\\?"\s*:\s*\\?"?([^,"\\}\s]+)' % key, line)
                if m:
                    f[key] = m.group(1)
            transfers.append(f)
    return counts, transfers


def parse_mtus(text):
    return {m.group(1): int(m.group(2)) for m in re.finditer(r"(?m)^(\S+): flags=\w+<[^>]*> mtu (\d+)", text)}


def check_mtu(mtus, apple=1500):
    """awdl0 and llw0 should be at Apple's 1500 (IO80211VirtualInterface::peerToPeerConfigureIfnet gives AppleBCMWLANCore
    1500 and every other controller 1484). A wired interface with a bigger MTU than awdl0 makes QUIC size packets awdl0 refuses."""
    out = []
    for n in ("awdl0", "llw0"):
        if n in mtus:
            out.append(("PASS" if mtus[n] == apple else "FAIL", "%s MTU %d (Apple's is %d)" % (n, mtus[n], apple)))
    if "awdl0" in mtus:
        wider = ["%s=%d" % (n, m) for n, m in sorted(mtus.items()) if n.startswith("en") and m > mtus["awdl0"]]
        if wider:
            out.append(("FAIL", "interface(s) with a bigger MTU than awdl0: " + ", ".join(wider)))
    return out


def stall_hints(samples, start, length):
    """What the driver's counters did between start and start + length, in words: the likely reason for the idle time."""
    inside = [s for s in samples if start <= s["t"] <= start + length + 1e-6 and s.get("reg")]
    if len(inside) < 2:
        return []
    a, b = inside[0]["reg"], inside[-1]["reg"]

    def d(key):
        return b.get(key, 0) - a.get(key, 0) if key in a and key in b else None

    hints = []
    away, holds, parked = d("ITLAWDLCoexGateAwayTicks"), d("ITLAWDLCoexGateHolds"), d("ItlwmAWDLTxParked")
    offered, tx, ok = d("ItlwmAWDLTxPacketsOffered"), d("ITLAWDLCoexDataTx"), d("ITLAWDLCoexUcastOK")
    if offered == 0 and (tx or 0) == 0:
        hints.append("nothing was offered to the driver: the sender (QUIC in sharingd) was not sending, which is what a path "
                     "switch, a retry or a wait for acknowledgements looks like")
    if parked and not ok:
        hints.append("%d frame(s) were parked for the peer and none was acknowledged: the gate held the peer's frames" % parked)
    if away:
        hints.append("the gate counted the peer away for %d ticks (%d holds): the iPhone was not on the listener's channel" % (away, holds or 0))
    if offered and not tx and not parked:
        hints.append("%d frame(s) were offered and none sent: they waited in the ring" % offered)
    seq, ch5 = d("ITLAWDLCoexPeerSequenceChanges"), d("ITLAWDLCoexPeer5GHzChanges")
    if seq:
        hints.append("the iPhone's channel sequence changed %d time(s)%s" % (seq, ", its 5 GHz channel %d time(s)" % ch5 if ch5 else ""))
    if d("ITLAWDLCoexPeerDecalibrations"):
        hints.append("the peer was decalibrated %d time(s)" % d("ITLAWDLCoexPeerDecalibrations"))
    if d("ITLAWDLCoexUcastFailed"):
        hints.append("%d unicast frame(s) failed" % d("ITLAWDLCoexUcastFailed"))
    if b.get("ITLAWDLCoexChannel") != a.get("ITLAWDLCoexChannel"):
        hints.append("the listener moved from %s to %s" % (a.get("ITLAWDLCoexChannel"), b.get("ITLAWDLCoexChannel")))
    return hints


# ---- analysis (pure) -------------------------------------------------------------------------------------------------

def rates(samples, iface):
    """[(t, Mbit/s)] of one interface's in + out bytes between consecutive samples."""
    out, prev = [], None
    for s in samples:
        cur = s["if"].get(iface)
        if cur is not None and prev is not None and prev[1] is not None and s["t"] > prev[0]:
            out.append((s["t"], ((cur[0] - prev[1][0]) + (cur[1] - prev[1][1])) * 8 / (s["t"] - prev[0]) / 1e6))
        prev = (s["t"], cur)
    return out


def percentile(values, p):
    if not values:
        return 0.0
    v = sorted(values)
    return v[min(len(v) - 1, int(round(p / 100.0 * (len(v) - 1))))]


def stalls(series, active_floor, min_len):
    """Runs of idle samples (below active_floor) strictly inside the active period, at least min_len seconds long.
    Each is (start time, length in seconds); a run's length is its samples times the sampling step."""
    act = [i for i, (_, r) in enumerate(series) if r >= active_floor]
    if len(act) < 2 or len(series) < 2:
        return []
    step = series[1][0] - series[0][0]
    out, start = [], None
    for i in range(act[0], act[-1] + 1):
        if series[i][1] < active_floor:
            if start is None:
                start = i
        elif start is not None:
            length = (i - start) * step
            if length >= min_len:
                out.append((series[start][0] - step, length))   # a rate's time is its interval's end
            start = None
    return out


def analyze(samples, reg0, reg1, log_text, opts):
    res = {"interfaces": {}, "registry": {}, "transfers": [], "signatures": {}, "lines": [], "failed": False}

    def add(level, text):
        res["lines"].append((level, text))
        if level == "FAIL":
            res["failed"] = True

    first, last = samples[0], samples[-1]
    total = 0
    for n in opts["ifaces"]:
        a, b = first["if"].get(n), last["if"].get(n)
        moved = ((b[0] - a[0]) + (b[1] - a[1])) if a and b else 0
        series = [r for _, r in rates(samples, n)]
        res["interfaces"][n] = {"bytes_in": (b[0] - a[0]) if a and b else 0, "bytes_out": (b[1] - a[1]) if a and b else 0,
                                "bytes": moved, "peak_mbit": max(series) if series else 0.0}
        total += moved
    res["total_bytes"] = total
    for n, i in res["interfaces"].items():
        i["share"] = 100.0 * i["bytes"] / total if total else 0.0

    if total < opts["min_bytes"]:
        add("INFO", "only %d bytes moved on %s: no transfer ran in this window" % (total, ",".join(opts["ifaces"])))
    else:
        wired = res["interfaces"].get("en0", {"share": 0.0})["share"]
        add("FAIL" if wired > 1.0 else "PASS", "en0 (wired, not itlwm) carried %.1f %% of the bytes; awdl0 %.1f %%, llw0 %.1f %%, en1 %.1f %%" % (
            wired, res["interfaces"].get("awdl0", {"share": 0.0})["share"], res["interfaces"].get("llw0", {"share": 0.0})["share"],
            res["interfaces"].get("en1", {"share": 0.0})["share"]))
        # Speed on the interface that carried most of the AWDL bytes.
        main = max(("awdl0", "llw0"), key=lambda n: res["interfaces"].get(n, {"bytes": 0})["bytes"])
        series = rates(samples, main)
        active = [r for _, r in series if r >= opts["active_floor"]]
        sp = {"interface": main, "active_seconds": len(active) * opts["interval"],
              "mean_mbit": statistics.mean(active) if active else 0.0, "median_mbit": statistics.median(active) if active else 0.0,
              "p95_mbit": percentile(active, 95), "peak_mbit": max(active) if active else 0.0}
        sp["stalls"] = stalls(series, opts["active_floor"], opts["max_stall"])
        res["speed"] = sp
        add("INFO", "%s active %.0f s: mean %.1f, median %.1f, p95 %.1f, peak %.1f Mbit/s" % (
            main, sp["active_seconds"], sp["mean_mbit"], sp["median_mbit"], sp["p95_mbit"], sp["peak_mbit"]))
        if opts["min_mbit"]:
            add("PASS" if sp["mean_mbit"] >= opts["min_mbit"] else "FAIL", "mean %.1f Mbit/s against the %.1f wanted" % (sp["mean_mbit"], opts["min_mbit"]))
        add("FAIL" if sp["stalls"] else "PASS", "%d stall(s) of %.0f s or more inside the transfer%s" % (
            len(sp["stalls"]), opts["max_stall"], ": " + ", ".join("at +%.0fs for %.0fs" % (t - first["t"], l) for t, l in sp["stalls"][:6]) if sp["stalls"] else ""))
        for t, l in sp["stalls"][:8]:
            for h in stall_hints(samples, t, l):
                add("INFO", "  stall at +%.0fs: %s" % (t - first["t"], h))

    # Driver counters.
    d = {}
    for key in REG_KEYS:
        if key in reg1:
            d[key] = reg1[key] if key in LEVEL_KEYS else reg1[key] - reg0.get(key, 0)
    res["registry"] = d
    if d:
        ok, bad = d.get("ITLAWDLCoexUcastOK", 0), d.get("ITLAWDLCoexUcastFailed", 0)
        if ok + bad >= 200:
            ratio = 100.0 * bad / (ok + bad)
            add("FAIL" if ratio > opts["max_fail_pct"] else "PASS", "unicast data: %d acknowledged, %d failed (%.1f %%)" % (ok, bad, ratio))
        elif ok + bad:
            # With a Block Ack agreement most frames go out aggregated and are counted below, not here: a handful says nothing.
            add("INFO", "single unicast frames: %d acknowledged, %d failed (too few to judge)" % (ok, bad))
        sent, acked = d.get("ITLAWDLCoexBaTxFrames", 0), d.get("ITLAWDLCoexBaTxAcked", 0)
        if sent >= 1000:
            pct = 100.0 * acked / sent
            add("FAIL" if pct < 95.0 else "PASS", "aggregated frames: %d sent, %d acknowledged (%.2f %%)" % (sent, acked, pct))
        for key, what in (("ItlwmAWDLTxDropped", "frames dropped by awdl0's ring"), ("ItlwmLLWRingDropped", "llw0 frames the ring refused"),
                          ("ITLAWDLCmdTimeouts", "firmware command timeouts"), ("ITLAWDLCmdEnospcRing", "command ring ENOSPC"),
                          ("ITLAWDLCmdEnospcSlot", "command slot ENOSPC")):
            if key in d:
                add("FAIL" if d[key] > 0 else "PASS", "%s: %d" % (what, d[key]))
        if "ItlwmAWDLTxParked" in d:
            ring = d.get("ItlwmAWDLTxParkFrames", 0)
            peak = d.get("ItlwmAWDLTxParkedPeak", 0)
            sat = ring and peak >= ring
            add("FAIL" if sat else "INFO", "parked %d frame(s), %d passed them, ring peak %d of %d%s" % (
                d["ItlwmAWDLTxParked"], d.get("ItlwmAWDLTxPassedParked", 0), peak, ring, " (the ring filled: the held peer is not coming back)" if sat else ""))
        if d.get("ITLAWDLCoexBaTxFrames") is not None:
            add("INFO", "block ack: %d aggregated frames sent, %d acknowledged; VO-queue frames %d" % (
                d.get("ITLAWDLCoexBaTxFrames", 0), d.get("ITLAWDLCoexBaTxAcked", 0), d.get("ITLAWDLCoexTxAcVo", 0)))
        if "ItlwmAWDLAwStarts" in d:
            add("INFO", "availability windows posted: %d starts, %d ends" % (d["ItlwmAWDLAwStarts"], d.get("ItlwmAWDLAwEnds", 0)))

    for level, text in check_mtu(opts.get("mtus", {})):
        add(level, text)
    counts, transfers = parse_log(log_text) if log_text else ({}, [])
    res["signatures"], res["transfers"] = counts, transfers
    for t in transfers:
        bps = t.get("bytesPerSec")
        mbit = float(bps) * 8 / 1e6 if bps and re.fullmatch(r"[0-9.]+", bps) else None
        add("PASS" if t.get("result") == "0" else "FAIL", "%s %s: result=%s size=%s transport=%s duration=%s speed=%s" % (
            t["stamp"], t["kind"], t.get("result", "?"), t.get("size", "-"), t.get("transport", "-"), t.get("duration", "-"),
            "%.1f Mbit/s" % mbit if mbit is not None else "-"))
    for name, _ in SIGNATURES:
        if counts.get(name):
            add("FAIL" if name in FAIL_SIGNATURES else "INFO", "log: %s x%d" % (name, counts[name]))
    return res


# ---- live sampling ---------------------------------------------------------------------------------------------------

def sample_once(ifaces, t):
    return {"t": t, "if": {n: parse_netstat(run(["netstat", "-I", n, "-b"], 10), n) for n in ifaces},
            "reg": parse_registry(run(["ioreg", "-l", "-w0", "-d1"], 10), TIMELINE_KEYS)}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--seconds", type=float, default=120.0)
    ap.add_argument("--interval", type=float, default=1.0)
    ap.add_argument("--ifaces", default=",".join(IFACES))
    ap.add_argument("--min-mbit", type=float, default=0.0, help="FAIL when the mean over the active seconds is lower")
    ap.add_argument("--max-stall", type=float, default=3.0, help="seconds of idle inside the transfer that count as a stall")
    ap.add_argument("--active-floor", type=float, default=1.0, help="Mbit/s above which a second counts as part of the transfer")
    ap.add_argument("--max-fail-pct", type=float, default=5.0, help="unicast failure percentage that FAILs")
    ap.add_argument("--min-bytes", type=int, default=1000000)
    ap.add_argument("--json", help="write the samples and the result here")
    ap.add_argument("--replay", help="judge a saved --json run instead of sampling")
    ap.add_argument("--no-log", action="store_true", help="skip reading the sharingd log")
    a = ap.parse_args(argv)
    opts = {"ifaces": a.ifaces.split(","), "interval": a.interval, "min_mbit": a.min_mbit, "max_stall": a.max_stall,
            "active_floor": a.active_floor, "max_fail_pct": a.max_fail_pct, "min_bytes": a.min_bytes}
    if a.replay:
        saved = json.load(open(a.replay))
        samples = [{"t": s["t"], "if": {k: (tuple(v) if v else None) for k, v in s["if"].items()}} for s in saved["samples"]]
        reg0, reg1, log_text = saved["reg0"], saved["reg1"], saved.get("log", "")
        opts["mtus"] = saved.get("mtus", {})
        opts["ifaces"] = saved.get("ifaces", opts["ifaces"])
        opts["interval"] = saved.get("interval", opts["interval"])
    else:
        opts["mtus"] = parse_mtus(run(["ifconfig", "-a"], 20))
        reg0 = parse_registry(run(["ioreg", "-l", "-w0"], 90))
        samples, start = [], time.time()
        while True:
            now = time.time()
            samples.append(sample_once(opts["ifaces"], now))
            if now - start >= a.seconds:
                break
            time.sleep(max(0.0, a.interval - (time.time() - now)))
        reg1 = parse_registry(run(["ioreg", "-l", "-w0"], 90))
        log_text = "" if a.no_log else run(["/usr/bin/log", "show", "--last", "%ds" % int(a.seconds + 30), "--style", "compact", "--predicate",
                                            'process == "sharingd" OR process == "wifip2pd" OR process == "rapportd"'], 240)
    res = analyze(samples, reg0, reg1, log_text, opts)
    for level, text in res["lines"]:
        print("%-4s %s" % (level, text))
    if a.json and not a.replay:
        json.dump({"samples": samples, "reg0": reg0, "reg1": reg1, "log": log_text, "ifaces": opts["ifaces"], "interval": opts["interval"],
                   "mtus": opts["mtus"]}, open(a.json, "w"))
    return 1 if res["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
