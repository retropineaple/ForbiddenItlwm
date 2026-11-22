"""tests for tools/diag/awdl_sniff.py on a small made-up capture: a phone plan, our data with duplicates, idle gaps, Block Ack."""
import importlib.util
import pathlib
import struct
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("awdl_sniff", root / "tools/diag/awdl_sniff.py")
sn = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sn)

US = bytes.fromhex("26b190695728")
PH = bytes.fromhex("b633bd88718c")
BSSID = bytes.fromhex("0025 00ff9473".replace(" ", ""))
RTAP = bytes([0, 0, 8, 0, 0, 0, 0, 0])


def pcap(packets):
    out = bytearray(struct.pack("<IHHiIII", 0xa1b2c3d4, 2, 4, 0, 0, 65535, 127))
    for t, f in packets:
        d = RTAP + f
        out += struct.pack("<IIII", int(t), int((t % 1) * 1e6), len(d), len(d)) + d
    return bytes(out)


def mgmt_action(ta, ra, body):
    return bytes([0xd0, 0]) + bytes(2) + ra + ta + BSSID + bytes(2) + body + bytes(4)


def awdl_with_plan(ta, plan):
    seq_tlv = bytes([15, 3, 0, 3, 255, 255]) + b"".join(bytes([c, 0x80 if c else 0]) for c in plan)
    body = bytes([0x7f, 0x00, 0x17, 0xf2, 8, 0x10, 3, 0]) + bytes(8) + bytes([18]) + struct.pack("<H", len(seq_tlv)) + seq_tlv
    return mgmt_action(ta, bytes([255] * 6), body)


def data_frame(ta, ra, seq, retry=False, size=1500):
    return bytes([0x08, 0x08 if retry else 0]) + bytes(2) + ra + ta + BSSID + struct.pack("<H", seq << 4) + bytes(size)


def ack(ra):
    return bytes([0xd4, 0, 0, 0]) + ra + bytes(4)


def addba(ta, ra, action):
    body = bytes([3, action, 102, 2, 0x10, 0, 0, 0, 0])
    return mgmt_action(ta, ra, body)


PLAN = [149] * 4 + [161] * 4 + [6] + [149] * 3 + [161] * 4
packets = []
for k in range(5):
    packets.append((10.0 + k * 0.5, awdl_with_plan(PH, PLAN)))
# 1 s of data in two bursts of 100 ms with a 400 ms silent gap and then more: each frame twice (a sniffer duplicate) and some retried.
seq = 0
t = 10.0
for burst in range(3):
    for i in range(100):
        f = data_frame(US, PH, seq, retry=(i % 20 == 0))
        packets.append((t, f)); packets.append((t + 0.00002, f)); packets.append((t + 0.00005, ack(US)))
        seq += 1; t += 0.001
    t += 0.4
packets.append((10.2, addba(US, PH, 0))); packets.append((10.3, addba(PH, US, 1))); packets.append((10.3002, ack(PH)))
packets.sort(key=lambda p: p[0])

with tempfile.TemporaryDirectory() as d:
    path = pathlib.Path(d) / "c.pcap"
    path.write_bytes(pcap(packets))
    fr = sn.load(str(path))
    res = sn.analyze(fr, US, PH)
    text = "\n".join(t for _, t in res["lines"])
    assert list(res["plan"]) == PLAN, res.get("plan")
    assert "161 x8, 149 x7, 6 x1" in text, text
    assert "300 distinct frames (300 duplicates dropped)" in text, text
    assert "30 retransmissions" in text or "15 retransmissions" in text, text
    assert "1 ADDBA answers" in text and "1 ADDBA requests" in text, text
    p = subprocess.run([sys.executable, "-I", str(root / "tools/diag/awdl_sniff.py"), str(path), "--us", "26:b1:90:69:57:28", "--phone", "b6:33:bd:88:71:8c"],
                       capture_output=True, text=True)
    assert "the plan has channel 161 in 8 of 16 slots" in p.stdout, p.stdout + p.stderr
    # Duplicates: the same sequence number again within 5 ms is one frame; the same number a long time later is another.
    rows = [(0.0, 5, False, 1500), (0.001, 5, False, 1500), (1.0, 5, False, 1500)]
    assert len(sn.distinct_frames(rows)) == 2
    assert sn.channel_plan(bytes([15, 3, 0, 3, 255, 255]) + bytes([161, 0x80, 0, 0, 149, 0x80])) == [161, 0, 149]
    assert sn.plan_share(PLAN, 161) == 8 and sn.plan_share(PLAN, 149) == 7
print("awdl_sniff: passed")
