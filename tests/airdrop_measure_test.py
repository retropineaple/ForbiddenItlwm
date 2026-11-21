"""tests for tools/diag/airdrop_measure.py: the parsers, the speed and stall analysis, and the verdicts, on made-up runs."""
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("airdrop_measure", root / "tools/diag/airdrop_measure.py")
am = importlib.util.module_from_spec(spec)
spec.loader.exec_module(am)

NETSTAT = """Name       Mtu   Network       Address            Ipkts Ierrs     Ibytes    Opkts Oerrs     Obytes  Coll
awdl0     1500  <Link#25>   3e:ba:c5:d6:fa:f0   1000     0   1500000     2000     0    3000000     0
awdl0     1500  fe80::3cba:c5ff:fed6:faf0%awdl0 fe80::3cba:c5ff:fed6:faf0  1000     -   1500000     2000     -    3000000     -
"""
assert am.parse_netstat(NETSTAT, "awdl0") == (1500000, 3000000, 1000, 2000)
assert am.parse_netstat(NETSTAT, "en0") is None

REG = '"ItlwmAWDLTxParked" = 129\n"ItlwmAWDLTxParkFrames" = 2048\n"ITLAWDLCoexCountryLearned" = No\n"ITLAWDLCoexUcastOK" = 90\n'
r = am.parse_registry(REG)
assert r == {"ItlwmAWDLTxParked": 129, "ItlwmAWDLTxParkFrames": 2048, "ITLAWDLCoexCountryLearned": 0, "ITLAWDLCoexUcastOK": 90}, r

LOG = (
    '2026-11-21 10:00:01.000 sharingd: airdrop.send-finish {"result":"0","size":"524288000","bytesPerSec":"14000000","transport":"awdl","duration":"37.4"}\n'
    '2026-11-21 10:05:00.000 sharingd: Ask failed Error Domain=Sharing.SFAirDropSend.Failure Code=7\n'
    '2026-11-21 10:06:00.000 sharingd: write failed: Message too long (unexpected-40)\n'
)
counts, transfers = am.parse_log(LOG)
assert counts == {"emsgsize": 1} and len(transfers) == 1
t = transfers[0]
assert t["kind"] == "send" and t["result"] == "0" and t["transport"] == "awdl" and t["bytesPerSec"] == "14000000", t


def run_samples(mbit_per_second, iface="awdl0", extra=None):
    """One sample per second; each entry is the Mbit/s moved in that second on `iface` (in + out)."""
    out, total = [], 0
    for i in range(len(mbit_per_second) + 1):
        ifs = {n: (0, 0, 0, 0) for n in ("awdl0", "llw0", "en0", "en1")}
        ifs[iface] = (total // 2, total - total // 2, 0, 0)
        if extra:
            for n, per in extra.items():
                ifs[n] = (per * i // 2, per * i - per * i // 2, 0, 0)
        out.append({"t": 100.0 + i, "if": ifs})
        if i < len(mbit_per_second):
            total += int(mbit_per_second[i] * 1e6 / 8)
    return out


OPTS = {"ifaces": ["awdl0", "llw0", "en0", "en1"], "interval": 1.0, "min_mbit": 0.0, "max_stall": 3.0,
        "active_floor": 1.0, "max_fail_pct": 5.0, "min_bytes": 1000000}

# A clean 20 s transfer at 100 Mbit/s on awdl0 passes everything.
res = am.analyze(run_samples([100] * 20), {}, {}, "", OPTS)
assert not res["failed"], res["lines"]
assert abs(res["speed"]["mean_mbit"] - 100) < 1 and res["speed"]["stalls"] == [] and res["interfaces"]["awdl0"]["share"] > 99.9

# A rate floor is enforced.
res = am.analyze(run_samples([40] * 20), {}, {}, "", dict(OPTS, min_mbit=60))
assert res["failed"] and any(l == "FAIL" and "wanted" in x for l, x in res["lines"])

# Four idle seconds inside the transfer are one stall of 4 s; idle seconds before and after are not.
res = am.analyze(run_samples([0, 0, 90, 90, 0, 0, 0, 0, 90, 90, 0]), {}, {}, "", OPTS)
assert res["speed"]["stalls"] == [(104.0, 4.0)] and res["failed"], res["speed"]
res = am.analyze(run_samples([0, 0, 90, 90, 0, 0, 90, 90, 0]), {}, {}, "", OPTS)     # a 2 s gap is under the limit
assert not res["speed"]["stalls"] and not res["failed"]

# Bytes that went out on the wired en0 fail the run; en1 (the AX200 through itlwm) does not.
res = am.analyze(run_samples([100] * 10, extra={"en0": 5_000_000}), {}, {}, "", OPTS)
assert res["failed"] and res["interfaces"]["en0"]["share"] > 1
res = am.analyze(run_samples([100] * 10, extra={"en1": 5_000_000}), {}, {}, "", OPTS)
assert not res["failed"]

# A stall is explained by what the counters did inside it.
def with_reg(samples, regs):
    for s, r in zip(samples, regs):
        s["reg"] = r
    return samples
base = {"ITLAWDLCoexGateAwayTicks": 0, "ITLAWDLCoexGateHolds": 0, "ItlwmAWDLTxPacketsOffered": 0, "ItlwmAWDLTxParked": 0,
        "ITLAWDLCoexDataTx": 0, "ITLAWDLCoexUcastOK": 0, "ITLAWDLCoexChannel": 161}
S = run_samples([0, 90, 90, 0, 0, 0, 0, 90, 90, 0])
regs = [dict(base) for _ in S]
for i in range(len(S)):                       # in the stall (samples 3 to 7) the gate holds the peer and frames are parked
    if i >= 3:
        regs[i].update(ITLAWDLCoexGateAwayTicks=100 * (min(i, 7) - 2), ITLAWDLCoexGateHolds=5 * (min(i, 7) - 2),
                       ItlwmAWDLTxParked=40 * (min(i, 7) - 2), ItlwmAWDLTxPacketsOffered=40 * (min(i, 7) - 2))
with_reg(S, regs)
hints = am.stall_hints(S, S[3]["t"], 4.0)
assert any("parked for the peer" in h for h in hints) and any("peer away" in h or "not on the listener's channel" in h for h in hints), hints
res = am.analyze(S, {}, {}, "", OPTS)
assert any("stall at +" in x and "parked" in x for _, x in res["lines"]), res["lines"]
regs2 = [dict(base) for _ in S]               # nothing offered at all: the sender stopped
assert any("nothing was offered" in h for h in am.stall_hints(with_reg(run_samples([0, 90, 90, 0, 0, 0, 0, 90, 90, 0]), regs2), 103.0, 4.0))
assert am.stall_hints([{"t": 1.0, "if": {}}], 1.0, 4.0) == []                         # no registry samples: no hints

# MTUs: Apple's 1500 on awdl0 and llw0; the old 1484 or a wider wired interface fails.
IFC = "awdl0: flags=8843<UP,BROADCAST,RUNNING,SIMPLEX,MULTICAST> mtu 1500\nllw0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500\nen0: flags=8863<UP> mtu 1492\n"
assert am.parse_mtus(IFC) == {"awdl0": 1500, "llw0": 1500, "en0": 1492}
assert all(l == "PASS" for l, _ in am.check_mtu(am.parse_mtus(IFC)))
old = am.check_mtu(am.parse_mtus(IFC.replace("llw0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1500", "llw0: flags=8863<UP,BROADCAST,SMART,RUNNING,SIMPLEX,MULTICAST> mtu 1484")))
assert ("FAIL", "llw0 MTU 1484 (Apple's is 1500)") in old
wide = am.check_mtu({"awdl0": 1484, "en0": 1500})
assert any(l == "FAIL" and "en0=1500" in x for l, x in wide)
res = am.analyze(run_samples([100] * 10), {}, {}, "", dict(OPTS, mtus={"awdl0": 1500, "llw0": 1484}))
assert res["failed"] and any("llw0 MTU 1484" in x for _, x in res["lines"])

# No transfer: informational only.
res = am.analyze(run_samples([0] * 5), {}, {}, "", OPTS)
assert not res["failed"] and "no transfer" in res["lines"][0][1]

# Driver counters: deltas, the unicast failure ratio, drops and a full parking ring.
reg0 = {"ITLAWDLCoexUcastOK": 100, "ITLAWDLCoexUcastFailed": 0, "ItlwmAWDLTxDropped": 0, "ItlwmAWDLTxParked": 10,
        "ItlwmAWDLTxParkFrames": 2048, "ItlwmAWDLTxParkedPeak": 15}
reg1 = {"ITLAWDLCoexUcastOK": 1100, "ITLAWDLCoexUcastFailed": 20, "ItlwmAWDLTxDropped": 0, "ItlwmAWDLTxParked": 40,
        "ItlwmAWDLTxParkFrames": 2048, "ItlwmAWDLTxParkedPeak": 22}
res = am.analyze(run_samples([100] * 10), reg0, reg1, "", OPTS)
assert not res["failed"] and res["registry"]["ITLAWDLCoexUcastOK"] == 1000 and res["registry"]["ItlwmAWDLTxParked"] == 30
reg1b = dict(reg1, ITLAWDLCoexUcastFailed=300, ItlwmAWDLTxDropped=7, ItlwmAWDLTxParkedPeak=2048)
res = am.analyze(run_samples([100] * 10), reg0, reg1b, "", OPTS)
fails = [x for l, x in res["lines"] if l == "FAIL"]
assert any("unicast" in x for x in fails) and any("dropped" in x for x in fails) and any("ring filled" in x for x in fails), fails

# The log's verdicts count: a failed send and an EMSGSIZE fail the run, a good send does not.
res = am.analyze(run_samples([100] * 10), {}, {}, LOG, OPTS)
assert res["failed"] and any("send: result=0" in x and l == "PASS" for l, x in res["lines"]) and any("emsgsize" in x for _, x in res["lines"])

# A saved run is judged again by --replay, with the same verdict and exit status.
with tempfile.TemporaryDirectory() as d:
    saved = pathlib.Path(d) / "run.json"
    samples = run_samples([100] * 10)
    saved.write_text(json.dumps({"samples": samples, "reg0": reg0, "reg1": reg1, "log": "", "ifaces": OPTS["ifaces"], "interval": 1.0}))
    p = subprocess.run([sys.executable, "-I", str(root / "tools/diag/airdrop_measure.py"), "--replay", str(saved)], capture_output=True, text=True)
    assert p.returncode == 0 and "PASS" in p.stdout and "mean 100" in p.stdout, p.stdout + p.stderr
    saved.write_text(json.dumps({"samples": samples, "reg0": reg0, "reg1": reg1b, "log": "", "ifaces": OPTS["ifaces"], "interval": 1.0}))
    p = subprocess.run([sys.executable, "-I", str(root / "tools/diag/airdrop_measure.py"), "--replay", str(saved)], capture_output=True, text=True)
    assert p.returncode == 1 and "FAIL" in p.stdout, p.stdout
print("airdrop_measure: passed")
