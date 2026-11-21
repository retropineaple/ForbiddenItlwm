#!/usr/bin/env python3
"""What the camera stream loses, seen above the radio: RTP sequence gaps in a pcap taken on llw0.

   sudo tcpdump -i llw0 -s 160 -nn -B 8192 -w /var/tmp/llw0.pcap -G 20 -W 1      (camera streaming)
   python3 -I tools/diag/llw0_rtp.py /var/tmp/llw0.pcap

Finds the biggest UDP flow, reads it as RTP (the iPhone's video: version 2, payload type 100, SRTP so the payload is opaque), and
reports: packets never received (by sequence number, counting late copies as received), the size of each loss run, where in
the iPhone's 524 ms schedule cycle each run starts and ends (the traffic itself fixes the cycle: the iPhone sends a heavy burst at the
start of its window on our channel, lighter traffic after it, nothing while it is away), duplicates and reordering, and the RTCP
packet types the Mac sends back (the payload is encrypted, so only the types are readable)."""
import collections
import math
import struct
import sys


def read_pcap(path):
    data = open(path, "rb").read()
    magic = struct.unpack_from("<I", data, 0)[0]
    if magic not in (0xa1b2c3d4, 0xa1b23c4d):
        sys.exit("not a little-endian classic pcap file")
    scale = 1e-6 if magic == 0xa1b2c3d4 else 1e-9
    if struct.unpack_from("<I", data, 20)[0] != 1:
        sys.exit("expected Ethernet frames (link type 1), as llw0 gives")
    off, out = 24, []
    while off + 16 <= len(data):
        sec, frac, caplen, origlen = struct.unpack_from("<IIII", data, off)
        off += 16
        out.append((sec + frac * scale, origlen, data[off:off + caplen]))
        off += caplen
    return out


def main(path):
    packets = read_pcap(path)
    flows = collections.Counter()
    for _, _, d in packets:
        if len(d) >= 62 and struct.unpack_from(">H", d, 12)[0] == 0x86dd and d[20] == 17:
            flows[(d[22:38], d[38:54], struct.unpack_from(">HH", d, 54))] += 1
    if not flows:
        sys.exit("no UDP over IPv6 in the capture")
    (src, dst, ports), count = flows.most_common(1)[0]
    print(f"{len(packets)} packets over {packets[-1][0] - packets[0][0]:.1f} s; biggest flow {src.hex()[-8:]} -> {dst.hex()[-8:]} "
          f"ports {ports[0]} -> {ports[1]}: {count} packets")
    t0 = packets[0][0]
    rtp, back = [], collections.Counter()
    for t, _, d in packets:
        if len(d) < 62 or d[20] != 17:
            continue
        pl = d[62:]
        if d[22:38] == src and d[38:54] == dst and len(pl) >= 12 and pl[0] >> 6 == 2:
            rtp.append((t - t0, struct.unpack_from(">H", pl, 2)[0], struct.unpack_from(">I", pl, 4)[0], pl[1] & 0x7f, pl))
        elif d[22:38] == dst and d[38:54] == src and len(pl) >= 8 and pl[0] >> 6 == 2:
            back[pl[1]] += 1
    pts = collections.Counter(r[3] for r in rtp)
    video_pt = pts.most_common(1)[0][0]
    video = [r for r in rtp if r[3] == video_pt]
    print(f"RTP payload types {dict(pts)}; reading PT {video_pt}: {len(video)} packets; RTCP types sent back {dict(back)} "
          f"(205 would be NACK, 206 PLI/FIR)")
    first, cycles, previous = {}, 0, video[0][1]
    for t, seq, _, _, _ in video:
        if seq < previous - 0x8000:
            cycles += 1
        previous = seq
        extended = seq + 0x10000 * cycles
        if extended in first:
            first[extended][1] += 1
        else:
            first[extended] = [t, 0]
    lo, hi = min(first), max(first)
    missing = [s for s in range(lo, hi + 1) if s not in first]
    expected = hi - lo + 1
    print(f"expected {expected}, never received {len(missing)} ({100 * len(missing) / expected:.2f}%), "
          f"duplicates {sum(v[1] for v in first.values())}")
    runs, i = [], 0
    while i < len(missing):
        j = i
        while j + 1 < len(missing) and missing[j + 1] == missing[j] + 1:
            j += 1
        runs.append((missing[i], j - i + 1))
        i = j + 1
    big = [r for r in runs if r[1] >= 20]
    print(f"{len(runs)} loss runs; {len(big)} of 20 packets or more hold {sum(r[1] for r in big)} of the {len(missing)} lost "
          f"({100 * sum(r[1] for r in big) / max(len(missing), 1):.0f}%); sizes {sorted(r[1] for r in runs)}")
    # The iPhone's cycle, from the traffic itself: the period that concentrates the arrival times most.
    best = (-1.0, 0.5235, 0.0)
    for k in range(0, 600):
        period = 0.5235 + k * 0.000005
        c = sum(math.cos(2 * math.pi * r[0] / period) for r in video)
        s = sum(math.sin(2 * math.pi * r[0] / period) for r in video)
        value = math.hypot(c, s) / len(video)
        if value > best[0]:
            best = (value, period, math.atan2(s, c))
    concentration, period, angle = best

    def bin_of(t):
        return ((t / period) - angle / (2 * math.pi)) % 1.0 * 64

    histogram = [0] * 64
    for r in video:
        histogram[int(bin_of(r[0])) % 64] += 1
    print(f"traffic period {period * 1000:.2f} ms (concentration {concentration:.2f}); packets per 8.2 ms bin of the cycle:")
    print("  " + " ".join(f"{v:5d}" for v in histogram[:32]))
    print("  " + " ".join(f"{v:5d}" for v in histogram[32:]))
    print("loss runs of 20 or more: packets, cycle bin of the last packet heard before the run, of the first heard after it, silence (ms)")
    for start, n in big:
        before, after = start - 1, start + n
        while before not in first and before > lo:
            before -= 1
        while after not in first and after < hi:
            after += 1
        if before in first and after in first:
            print(f"  {n:4d}  {bin_of(first[before][0]):5.1f}  {bin_of(first[after][0]):5.1f}  {(first[after][0] - first[before][0]) * 1000:6.1f}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
