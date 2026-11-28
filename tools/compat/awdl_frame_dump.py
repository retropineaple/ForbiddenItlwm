#!/usr/bin/env python3
"""Decode the AWDL sync frames ForbiddenItlwm keeps with -itlawdlframedump.

Usage: python3 awdl_frame_dump.py [ioreg.plist]
Without an argument it reads the live registry (ioreg -a -l -w0 -d1). With a file it reads that plist,
for example one saved with `ioreg -a -l -w0 -d1 > ioreg.plist` during a stall.

For each kept frame it prints the time (uptime ms), built (TX) or heard (RX), the channel, the sender
(last two address bytes only), PSF or MI, and the fields IO80211 checks before it qualifies a peer
for its master election:
- sync parameters (TLV 4): AW sequence number, presence mode (IO80211 only counts a sync as heard
  when it isn't 0), AW period / extension length / common length (its safety check wants 16 each),
  the master address, channels and timing values;
- election (TLV 5) and election v2 (TLV 24): election ID, hop count, master, metrics, counters;
- version (TLV 21).
Other TLVs are listed by type and length only: the service TLVs hold device names.
"""
import plistlib
import struct
import subprocess
import sys

KEYS = ("ITLAWDLFrameDumpTx", "ITLAWDLFrameDumpRx", "ITLAWDLFrameDump")


def find(node, found):
    if isinstance(node, dict):
        for key in KEYS:
            if key in node and key not in found:
                found[key] = node[key]
        for value in node.values():
            find(value, found)
    elif isinstance(node, list):
        for value in node:
            find(value, found)


def short(addr):
    return "..:%02x:%02x" % (addr[4], addr[5])


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def sync_params(v):
    if len(v) < 31:
        return "short (%d bytes)" % len(v)
    out = ("aw seq %u, presence %u, aw period %u, ext len %u, common len %u, remaining %u, master %s, "
           "next ch %u, master ch %u, tx counter %u, guard %u, af period %u, flags 0x%04x, ext min %u mc %u uc %u af %u"
           % (u16(v, 29), v[27], u16(v, 5), u16(v, 11), u16(v, 13), u16(v, 15), short(v[21:27]), v[0], v[3],
              u16(v, 1), v[4], u16(v, 7), u16(v, 9), v[17], v[18], v[19], v[20]))
    if len(v) >= 33:
        out += ", beacon delta %u" % u16(v, 31)
    return out


def election(v):
    if len(v) < 19:
        return "short (%d bytes)" % len(v)
    return ("flags 0x%02x, id %u, hops %u, master %s, master metric %u, self metric %u"
            % (v[0], u16(v, 1), v[3], short(v[5:11]), u32(v, 11), u32(v, 15)))


def election2(v):
    if len(v) < 40:
        return "short (%d bytes): %s" % (len(v), v.hex())
    return ("master %s, other %s, master counter %u, hops %u, master metric %u, self metric %u, unknown %u, "
            "reserved %u, self counter %u"
            % (short(v[0:6]), short(v[6:12]), u32(v, 12), u32(v, 16), u32(v, 20), u32(v, 24), u32(v, 28),
               u32(v, 32), u32(v, 36)))


def frame(f):
    if len(f) < 40:
        return ["  (frame of %d bytes, too short)" % len(f)]
    lines = ["  from %s, %s, version 0x%02x, phy tx %u, target tx %u"
             % (short(f[10:16]), {0: "PSF", 3: "MI"}.get(f[30], "subtype %u" % f[30]), f[29], u32(f, 32), u32(f, 36))]
    others = []
    off = 40
    while off + 3 <= len(f):
        t, n = f[off], u16(f, off + 1)
        v = f[off + 3:off + 3 + n]
        if t == 4:
            lines.append("  sync params: " + sync_params(v))
        elif t == 5:
            lines.append("  election: " + election(v))
        elif t == 24:
            lines.append("  election v2: " + election2(v))
        elif t == 21 and len(v) >= 2:
            lines.append("  version: %u.%u, device class %u" % (v[0] >> 4, v[0] & 15, v[1]))
        else:
            others.append("%u:%u" % (t, n))
        if off + 3 + n > len(f):
            lines.append("  (TLV %u runs past the kept bytes)" % t)
            break
        off += 3 + n
    if others:
        lines.append("  other TLVs (type:length): " + " ".join(others))
    return lines


def entries(blob):
    off = 0
    while off + 8 <= len(blob):
        ms, length, channel, cut = struct.unpack_from("<IHBB", blob, off)
        yield ms, channel, cut, bytes(blob[off + 8:off + 8 + length])
        off += 8 + length


def main():
    if len(sys.argv) > 1:
        with open(sys.argv[1], "rb") as handle:
            tree = plistlib.load(handle)
    else:
        tree = plistlib.loads(subprocess.run(["ioreg", "-a", "-l", "-w0", "-d1"], check=True,
                                             capture_output=True).stdout)
    found = {}
    find(tree, found)
    if "ITLAWDLFrameDump" not in found:
        print("No frame dump in the registry: boot with -itlawdlframedump (and AirDrop in use).")
        return 1
    print("ITLAWDLFrameDump:", found["ITLAWDLFrameDump"])
    rows = []
    for key, label in (("ITLAWDLFrameDumpTx", "built"), ("ITLAWDLFrameDumpRx", "heard")):
        for ms, channel, cut, data in entries(found.get(key, b"")):
            rows.append((ms, label, channel, cut, data))
    for ms, label, channel, cut, data in sorted(rows, key=lambda r: r[0]):
        print("%u ms %s%s%s" % (ms, label, " on %u" % channel if channel else "", " (cut)" if cut else ""))
        for line in frame(data):
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
