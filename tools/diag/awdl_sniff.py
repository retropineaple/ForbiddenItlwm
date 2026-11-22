#!/usr/bin/env python3
"""Read a monitor-mode capture of an AirDrop send (classic pcap, radiotap, one channel) and say what limits it.

  python3 -I tools/diag/awdl_sniff.py capture.pcap --us 26:b1:90:69:57:28 --phone b6:33:bd:88:71:8c

It reports, from the air alone:
  the phone's channel plan     the channel sequence (AWDL TLV 18) it announced during the send, and the share of its 16 slots on the
                               capture channel: that share is the most airtime a radio parked there can have
  our send                     distinct data frames to the phone (duplicates of one sequence number within 5 ms are one frame),
                               rate, retransmissions, PHY rate, how much of the time frames flow (10 ms bins) and the idle gaps
  who was on the channel       in the idle gaps, whether the phone transmitted anything: none means it was elsewhere
  Block Ack                    our ADDBA requests, the phone's ADDBA answers and whether we acknowledged them (the host never sees
                               them on the AX200: the firmware acknowledges and does not deliver)
The 2026-11-22 capture of a Mac -> iPhone send gave a plan of 149 x7, 161 x8, 6 x1 slots against a Mac parked on 161: 50 % airtime.
Exit status 1 when the plan shows the capture channel in fewer than 12 of 16 slots and our sending covers that share (the cap is the
plan, not the driver).
"""
import argparse
import bisect
import collections
import statistics
import struct
import sys

AWDL_OUI = bytes.fromhex("0017f2")


def mac(s):
    return bytes.fromhex(s.replace(":", ""))


def load(path):
    """[(time, 802.11 bytes, radiotap bytes)], classic pcap with radiotap (link type 127)."""
    out = []
    with open(path, "rb") as f:
        head = f.read(24)
        if struct.unpack("<I", head[:4])[0] not in (0xa1b2c3d4, 0xa1b23c4d) or struct.unpack("<I", head[20:24])[0] != 127:
            raise SystemExit("not a classic little-endian pcap with radiotap framing")
        nano = struct.unpack("<I", head[:4])[0] == 0xa1b23c4d
        while True:
            h = f.read(16)
            if len(h) < 16:
                break
            ts, tu, cl, _ = struct.unpack("<IIII", h)
            d = f.read(cl)
            if len(d) < 8:
                continue
            rl = struct.unpack_from("<H", d, 2)[0]
            if len(d) - rl >= 10:
                out.append((ts + tu / (1e9 if nano else 1e6), d[rl:], d[:rl]))
    return out


def awdl_tlvs(f):
    """(subtype, {tlv type: [bytes]}) of an AWDL action frame, or None."""
    b = f[24:]
    if len(b) < 16 or b[0] != 0x7f or b[1:4] != AWDL_OUI or b[4] != 8:
        return None
    tlvs, pos = {}, 16
    while pos + 3 <= len(b) - 4:
        n = struct.unpack_from("<H", b, pos + 1)[0]
        tlvs.setdefault(b[pos], []).append(b[pos + 3:pos + 3 + n])
        pos += 3 + n
    return b[6], tlvs


def channel_plan(tlv18):
    """The 16 channels of a channel sequence TLV (0 = none announced)."""
    body = tlv18[6:]
    return [body[i] for i in range(0, min(len(body) - 1, 32), 2)][:16]


def distinct_frames(rows):
    """[(time, seq, retry, length)] one per frame: the same sequence number again within 5 ms is a duplicate."""
    out, last = [], {}
    for t, s, r, n in rows:
        if s in last and t - last[s] < 0.005:
            last[s] = t
            continue
        last[s] = t
        out.append((t, s, r, n))
    return out


def analyze(frames, us, phone):
    res = {"lines": [], "cap": False}
    add = lambda level, text: res["lines"].append((level, text))
    t0 = frames[0][0]
    a1 = lambda f: f[4:10]
    a2 = lambda f: f[10:16] if len(f) >= 16 else b""
    # The phone's channel plans.
    plans = collections.OrderedDict()
    for t, f, _ in frames:
        if (f[0] >> 2) & 3 == 0 and (f[0] >> 4) in (0xd, 0xe) and a2(f) == phone:
            r = awdl_tlvs(f)
            if r and 18 in r[1]:
                p = tuple(channel_plan(r[1][18][0]))
                e = plans.setdefault(p, [0, t - t0, t - t0])
                e[0] += 1
                e[2] = t - t0
    if plans:
        main = max(plans.items(), key=lambda kv: kv[1][0])
        plan, (n, a, b) = main
        counts = collections.Counter(plan)
        add("INFO", "the phone's plan (%d announcements, %.0f to %.0f s): %s" % (
            n, a, b, " ".join("%s" % (c or ".") for c in plan)))
        add("INFO", "slots per channel: " + ", ".join("%s x%d" % (c or "none", k) for c, k in sorted(counts.items(), key=lambda kv: -kv[1])))
        res["plan"] = plan
    # Our data frames to the phone.
    rows = [(t - t0, struct.unpack_from("<H", f, 22)[0] >> 4, bool(f[1] & 8), len(f)) for t, f, _ in frames
            if (f[0] >> 2) & 3 == 2 and a2(f) == us and a1(f) == phone and len(f) > 200]
    if not rows:
        add("INFO", "no data frames from us to the phone in this capture")
        return res
    dist = distinct_frames(rows)
    ts = [x[0] for x in dist]
    dur = ts[-1] - ts[0]
    bins = collections.Counter(int(x * 100) for x in ts)
    nb = int(dur * 100) + 1
    active = [bins.get(i + int(ts[0] * 100), 0) for i in range(nb)]
    busy = [v for v in active if v]
    add("INFO", "our data: %d distinct frames (%d duplicates dropped) in %.0f s = %.0f frames/s, %.1f Mbit/s, %d retransmissions" % (
        len(dist), len(rows) - len(dist), dur, len(dist) / dur, sum(x[3] for x in dist) * 8 / dur / 1e6, sum(1 for x in dist if x[2])))
    duty = 100.0 * len(busy) / max(1, nb)
    add("INFO", "frames flow in %.0f %% of 10 ms bins, %.1f per busy bin (%.1f Mbit/s while sending)" % (
        duty, statistics.mean(busy), statistics.mean(busy) * 100 * statistics.mean(x[3] for x in dist) * 8 / 1e6))
    res["duty"] = duty
    gaps = [(a, b) for a, b in zip(ts, ts[1:]) if b - a > 0.010]
    present = sorted(t - t0 for t, f, _ in frames if a2(f) == phone or ((f[0] >> 2) & 3 == 1 and (f[0] >> 4) == 0xd and a1(f) == us))
    silent = sum(1 for a, b in gaps if bisect.bisect_left(present, b - 0.001) - bisect.bisect_right(present, a + 0.001) == 0)
    add("INFO", "idle gaps over 10 ms: %d, %.1f s in all; the phone transmitted nothing in %d of them (%.0f %%): it was on another channel" % (
        len(gaps), sum(b - a for a, b in gaps), silent, 100.0 * silent / max(1, len(gaps))))
    # Block Ack.
    req = resp = acked = 0
    for t, f, _ in frames:
        if (f[0] >> 2) & 3 == 0 and (f[0] >> 4) in (0xd, 0xe) and len(f) > 26 and f[24] == 3:
            if a2(f) == us and a1(f) == phone and f[25] == 0:
                req += 1
            elif a2(f) == phone and a1(f) == us and f[25] == 1:
                resp += 1
    acks = sum(1 for t, f, _ in frames if (f[0] >> 2) & 3 == 1 and (f[0] >> 4) == 0xd and a1(f) == phone)
    add("INFO", "Block Ack: %d ADDBA requests from us (as captured), %d ADDBA answers from the phone, %d ACKs from us to the phone" % (req, resp, acks))
    return res


def plan_share(plan, channel):
    return sum(1 for c in plan if c == channel)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("pcap")
    ap.add_argument("--us", required=True, help="our AWDL address")
    ap.add_argument("--phone", required=True, help="the phone's AWDL address")
    ap.add_argument("--channel", type=int, default=161, help="the channel the capture and our radio were on")
    a = ap.parse_args(argv)
    res = analyze(load(a.pcap), mac(a.us), mac(a.phone))
    for level, text in res["lines"]:
        print("%-4s %s" % (level, text))
    capped = False
    if "plan" in res:
        n = plan_share(res["plan"], a.channel)
        print("%-4s the plan has channel %d in %d of 16 slots (%.0f %% of the time): a radio parked there can have at most that" % ("INFO", a.channel, n, 100.0 * n / 16))
        capped = n < 12 and res.get("duty", 100) <= 100.0 * n / 16 + 10
        print("%-4s %s" % ("FAIL" if capped else "PASS", "our sending covers the plan's share on %d: the cap is the phone's schedule, not the driver" % a.channel if capped
                           else "our sending is not limited by the phone's schedule alone"))
    return 1 if capped else 0


if __name__ == "__main__":
    sys.exit(main())
