#!/usr/bin/env python3
"""Decode the driver's raw management-frame ring: the latest non-AWDL action frames the firmware handed up, before any parsing.

  python3 -I tools/diag/rawmgmt_ring.py                 read the live registry (ioreg)
  python3 -I tools/diag/rawmgmt_ring.py --file dump     decode a saved `ioreg -l -w0` text

For each frame: time, receiver (unicast to us or not), whether it carried an HT Control field, the transmitter and receiver
addresses, the category and action, and for Block Ack frames the decoded fields. It exists to answer one question: does the
iPhone's ADDBA answer (category 3, action 1) ever reach the host, and in what shape. Keys: ITLAWDLRawMgmtRing (32 entries of 48
bytes), ITLAWDLRawMgmtCount (frames recorded since boot), ITLAWDLRawMgmtCats (counts by category, 15 meaning 15 and above).
Exit status 1 when the ring holds ADDBA requests and no answer, so it can sit in a check script.
"""
import argparse
import re
import struct
import subprocess
import sys

ENTRY = 48
RING = 32


def parse_registry(text):
    def data(key):
        m = re.search(r'"%s" = <([0-9a-f]*)>' % key, text)
        return bytes.fromhex(m.group(1)) if m else None
    cm = re.search(r'"ITLAWDLRawMgmtCount" = (\d+)', text)
    return {"ring": data("ITLAWDLRawMgmtRing"), "cats": data("ITLAWDLRawMgmtCats"), "count": int(cm.group(1)) if cm else None}


def entries(ring, count):
    """Frames oldest first. The ring is written at count % 32, so once it has wrapped the oldest is at that index."""
    out = []
    n = min(count, RING)
    start = count % RING if count >= RING else 0
    for i in range(n):
        slot = (start + i) % RING
        us, length, flags, _rssi = struct.unpack_from("<IHBB", ring, slot * ENTRY)
        out.append({"us": us, "length": length, "unicast": bool(flags & 1), "htc": bool(flags & 2), "padded": bool(flags & 4),
                    "bytes": ring[slot * ENTRY + 8:(slot + 1) * ENTRY]})
    return out


def mac(b):
    return ":".join("%02x" % x for x in b)


def decode(e):
    b = e["bytes"]
    at = 28 if (b[1] & 0x80) else 24
    d = {"ra": mac(b[4:10]), "ta": mac(b[10:16]), "bssid": mac(b[16:22]), "fc": "%02x%02x" % (b[0], b[1]), "category": b[at], "action": b[at + 1],
         "name": "action %d/%d" % (b[at], b[at + 1])}
    if b[at] == 3 and at + 2 + 3 <= len(b):
        body = b[at + 2:]
        if b[at + 1] == 0:      # ADDBA request: token, params (policy bit 1, TID bits 2-5, buffer size bits 6-15), timeout, SSC
            p = body[1] | body[2] << 8
            d["name"] = "ADDBA request token %d tid %d window %d %s" % (body[0], (p >> 2) & 15, p >> 6, "immediate" if p & 2 else "delayed")
        elif b[at + 1] == 1:    # ADDBA response: token, status, params
            status = body[1] | body[2] << 8
            p = body[3] | body[4] << 8
            d["name"] = "ADDBA RESPONSE token %d status %d tid %d window %d %s" % (body[0], status, (p >> 2) & 15, p >> 6, "immediate" if p & 2 else "delayed")
        elif b[at + 1] == 2:
            p = body[0] | body[1] << 8
            d["name"] = "DELBA tid %d %s" % (p >> 12, "originator" if p & 0x800 else "recipient")
    return d


def report(info):
    if not info["ring"] or info["count"] is None:
        print("no ITLAWDLRawMgmtRing in the registry (the driver is older than this tool, or no frame was recorded yet)")
        return 2
    es = entries(info["ring"], info["count"])
    print("%d frame(s) recorded since boot; the ring holds the latest %d" % (info["count"], len(es)))
    if info["cats"]:
        cats = struct.unpack("<16I", info["cats"])
        print("by category: " + ", ".join("%s:%d" % ("15+" if i == 15 else i, c) for i, c in enumerate(cats) if c))
    requests = responses = 0
    t0 = es[0]["us"] if es else 0
    for e in es:
        d = decode(e)
        requests += d["category"] == 3 and d["action"] == 0
        responses += d["category"] == 3 and d["action"] == 1
        print("+%8.3f s  len %3d  %s%s%s  fc %s  ta %s -> ra %s  %s" % (
            ((e["us"] - t0) & 0xffffffff) / 1e6, e["length"], "U" if e["unicast"] else "-", "H" if e["htc"] else "-", "P" if e["padded"] else "-",
            d["fc"], d["ta"], d["ra"], d["name"]))
    print("ADDBA requests %d, ADDBA responses %d in the ring (U unicast to us, H carried an HT Control field, P firmware padded)" % (requests, responses))
    return 1 if requests and not responses else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--file", help="decode a saved `ioreg -l -w0` text instead of the live registry")
    a = ap.parse_args(argv)
    text = open(a.file, errors="ignore").read() if a.file else subprocess.run(["ioreg", "-l", "-w0", "-d1"], capture_output=True, text=True).stdout
    return report(parse_registry(text))


if __name__ == "__main__":
    sys.exit(main())
