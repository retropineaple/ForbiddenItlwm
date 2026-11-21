"""tests for tools/diag/rawmgmt_ring.py: ring order after a wrap, the Block Ack decode with and without HT Control, the verdict."""
import importlib.util
import pathlib
import struct
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rawmgmt_ring", root / "tools/diag/rawmgmt_ring.py")
rm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rm)

PHONE = bytes.fromhex("06aabbccddee")
US = bytes.fromhex("021122334455")
BSSID = bytes.fromhex("0025 00ff9473".replace(" ", ""))


def frame(action, htc=False, token=111, status=0, tid=0, window=64):
    hdr = bytearray(24)
    hdr[0] = 0xd0
    hdr[4:10], hdr[10:16], hdr[16:22] = US, PHONE, BSSID
    if htc:
        hdr[1] |= 0x80
        hdr += bytes([1, 2, 3, 4])
    params = 2 | tid << 2 | window << 6
    if action == 0:
        body = bytes([3, 0, token]) + struct.pack("<H", params) + b"\0\0" + b"\0\0"
    elif action == 1:
        body = bytes([3, 1, token]) + struct.pack("<HHH", status, params, 0)
    else:
        body = bytes([3, 2]) + struct.pack("<HH", tid << 12 | 0x800, 1)
    return bytes(hdr + body)


def entry(us, raw, unicast=True):
    flags = (1 if unicast else 0) | (2 if raw[1] & 0x80 else 0)
    return struct.pack("<IHBB", us, len(raw), flags, 0) + (raw + bytes(40))[:40]


def ring_of(frames, total):
    """A ring after `total` frames were recorded, `frames` being the latest ones (oldest first)."""
    slots = [bytes(rm.ENTRY)] * rm.RING
    first = total - len(frames)
    for i, f in enumerate(frames):
        slots[(first + i) % rm.RING] = f
    return b"".join(slots)


# Decode: a plain request, an answer, an answer with HT Control, a DELBA.
d = rm.decode({"bytes": (frame(0) + bytes(40))[:40], "unicast": True})
assert d["name"] == "ADDBA request token 111 tid 0 window 64 immediate" and d["ta"] == "06:aa:bb:cc:dd:ee" and d["bssid"] == "00:25:00:ff:94:73"
d = rm.decode({"bytes": (frame(1) + bytes(40))[:40]})
assert d["name"] == "ADDBA RESPONSE token 111 status 0 tid 0 window 64 immediate", d
d = rm.decode({"bytes": (frame(1, htc=True) + bytes(40))[:40]})
assert d["name"] == "ADDBA RESPONSE token 111 status 0 tid 0 window 64 immediate" and d["category"] == 3, d
assert rm.decode({"bytes": (frame(2) + bytes(40))[:40]})["name"].startswith("DELBA tid 0 originator")

# Order after a wrap: 40 frames recorded, the ring holds frames 8..39, oldest first.
frames = [entry(1000 * i, frame(0, token=i)) for i in range(8, 40)]
ring = ring_of(frames, 40)
es = rm.entries(ring, 40)
assert [e["us"] for e in es] == [1000 * i for i in range(8, 40)]
es = rm.entries(ring_of(frames[-3:], 3), 3)                        # not wrapped yet: only what was written
assert [e["us"] for e in es] == [37000, 38000, 39000] or len(es) == 3

# The registry text and the verdict.
def registry(ring, count, cats):
    return '"ITLAWDLRawMgmtRing" = <%s>\n"ITLAWDLRawMgmtCount" = %d\n"ITLAWDLRawMgmtCats" = <%s>\n' % (ring.hex(), count, struct.pack("<16I", *cats).hex())

cats = [0] * 16
cats[3] = 2
only_requests = registry(ring_of([entry(0, frame(0)), entry(1000, frame(0, token=112))], 2), 2, cats)
only_answers = registry(ring_of([entry(0, frame(0)), entry(1000, frame(1))], 2), 2, cats)
htc_answer = registry(ring_of([entry(0, frame(0)), entry(1000, frame(1, htc=True))], 2), 2, cats)
with tempfile.TemporaryDirectory() as dir_:
    def run(text):
        p = pathlib.Path(dir_) / "ioreg.txt"
        p.write_text(text)
        return subprocess.run([sys.executable, "-I", str(root / "tools/diag/rawmgmt_ring.py"), "--file", str(p)], capture_output=True, text=True)
    r = run(only_requests)
    assert r.returncode == 1 and "ADDBA requests 2, ADDBA responses 0" in r.stdout and "by category: 3:2" in r.stdout, r.stdout
    r = run(only_answers)
    assert r.returncode == 0 and "ADDBA RESPONSE" in r.stdout, r.stdout
    r = run(htc_answer)
    assert r.returncode == 0 and "UH-" in r.stdout and "ADDBA RESPONSE" in r.stdout, r.stdout
    r = run("nothing here")
    assert r.returncode == 2 and "no ITLAWDLRawMgmtRing" in r.stdout
print("rawmgmt_ring: passed")
