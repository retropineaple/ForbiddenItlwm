#!/usr/bin/env python3
"""Scorecard for Continuity Camera choppiness captures (ioreg dump or live).

  python3 -I tools/diag/cc_scorecard.py                  # live ioreg
  python3 -I tools/diag/cc_scorecard.py /var/tmp/cc-*/registry_after.txt
"""
from __future__ import annotations

import re
import struct
import subprocess
import sys
from pathlib import Path


def load_text(path: Path | None) -> str:
    if path is None:
        return subprocess.check_output(["ioreg", "-l", "-w0"], text=True, errors="replace")
    return path.read_text(errors="replace")


def props(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    for m in re.finditer(r'"([^"]+)"\s*=\s*([^,\n]+)', text):
        out[m.group(1)] = m.group(2).strip()
    return out


def u32(v: str | None) -> int | None:
    if v is None:
        return None
    v = v.strip().strip('"')
    try:
        n = int(v, 0)
    except ValueError:
        return None
    return n & 0xFFFFFFFF


def parse_data(v: str | None) -> bytes | None:
    if not v:
        return None
    m = re.search(r"<([0-9a-fA-F]+)>", v)
    if not m:
        return None
    h = m.group(1)
    if len(h) % 2:
        return None
    return bytes.fromhex(h)


def tid_u32s(data: bytes | None, n: int = 8) -> list[int]:
    if not data:
        return [0] * n
    out = []
    for i in range(n):
        off = i * 4
        if off + 4 <= len(data):
            out.append(struct.unpack_from("<I", data, off)[0])
        else:
            out.append(0)
    return out


def decode_lowlat(result: int | None) -> str:
    if result is None:
        return "missing"
    if result & 0x80000000:
        err = result & 0x7FFFFFFF
        names = {5: "EIO", 35: "EAGAIN/EWOULDBLOCK", 45: "ENOTSUP", 102: "EOPNOTSUPP"}
        if err == 35:
            return f"ERROR 0x{err:x} (EAGAIN/EWOULDBLOCK) — sync wait timed out (firmware may send no response; try ASYNC)"
        return f"ERROR 0x{err:x} ({names.get(err, '?')}) — command not accepted"
    if result == 0x08000000:
        return "skipped AX210+"
    if result == 0x10000000:
        return "no DYNAMIC_QUOTA capability"
    if result == 0x40000000:
        return "command not listed in firmware"
    if (result & 0xFF000000) == 0x20000000:
        return f"unsupported cmd ver {result & 0xFF}"
    if result & 0x100:
        return f"accepted ver={result & 0xFF}"
    return f"raw 0x{result:08x}"


def gap_deltas(ring: bytes | None, count: int | None) -> list[int]:
    if not ring or not count:
        return []
    events = []
    for i in range(min(count, 48)):
        off = i * 12
        if off + 12 > len(ring):
            break
        gp2, missing, seq, length, tid, retry = struct.unpack_from("<IHHHBB", ring, off)
        events.append(gp2)
    # ring is chronological overwrite; deltas between successive non-zero gp2
    deltas = []
    prev = None
    for g in events:
        if prev is not None and g >= prev:
            deltas.append(g - prev)
        prev = g
    return deltas


def scorecard(text: str, label: str) -> None:
    p = props(text)
    seen = u32(p.get("ITLAWDLCoexSeqSeen"))
    miss = u32(p.get("ITLAWDLCoexSeqMissing"))
    late = u32(p.get("ITLAWDLCoexSeqLate"))
    runs = u32(p.get("ITLAWDLCoexSeqGapRuns"))
    longest = u32(p.get("ITLAWDLCoexSeqLongestGap"))
    tid_seen = tid_u32s(parse_data(p.get("ITLAWDLCoexSeqTidSeen")))
    tid_miss = tid_u32s(parse_data(p.get("ITLAWDLCoexSeqTidMissing")))
    ring = parse_data(p.get("ITLAWDLCoexSeqGapRing"))
    ring_n = u32(p.get("ITLAWDLCoexSeqGapRingCount"))
    deltas = gap_deltas(ring, ring_n)
    llr = u32(p.get("ITLAWDLCoexLowLatencyResult"))
    wanted = u32(p.get("ItlwmLLWRxWanted"))
    delivered = u32(p.get("ItlwmLLWRxDelivered"))
    failed = u32(p.get("ItlwmLLWRxFailed"))
    crc = u32(p.get("ITLAWDLRxMpduBadCrc"))
    overrun = u32(p.get("ITLAWDLRxMpduBadOverrun"))

    print(f"=== Continuity Camera scorecard: {label} ===")
    print(f"build:     {p.get('ItlwmAWDLInterfaceBuild', '?')}")
    print(f"llw stage: {p.get('ItlwmLLWStage', '?')}  RxWanted={wanted} Delivered={delivered} Failed={failed}")
    print(f"AWDL ch:   {p.get('ITLAWDLCoexChannel', '?')} width={p.get('ITLAWDLCoexOwnWidth', '?')} peer5g={p.get('ITLAWDLCoexPeer5GHz', '?')}")
    print(f"follows:   {p.get('ITLAWDLCoexFollowsInfra', '?')} force={p.get('ITLAWDLCoexFollowsInfraForce', '?')} continuityPower={p.get('ITLAWDLCoexContinuityPower', '?')}")
    print(f"realtime:  {p.get('ITLAWDLCoexRealtime', '?')} notes={p.get('ITLAWDLCoexRealtimeNotes', '?')} txAcVo={p.get('ITLAWDLCoexTxAcVo', '?')}")
    print(f"lowlat:    {p.get('ITLAWDLCoexLowLatency', '?')} result={decode_lowlat(llr)}")
    ba_tids = u32(p.get('ITLAWDLCoexBaTids'))
    ba_busy = u32(p.get('ITLAWDLCoexBaBusySkips'))
    if ba_tids is not None:
        bits = [i for i in range(8) if ba_tids & (1 << i)]
        print(f"BA:        PeerAdded={p.get('ITLAWDLCoexBaPeerAdded', '?')} Tids={ba_tids} bits={bits} BusySkips={ba_busy} SetupHolds={p.get('ITLAWDLCoexBaSetupHolds', '?')} Status={p.get('ITLAWDLCoexBaStatus', '?')}")
    print(f"cam flag:  {p.get('ITLAWDLCoexCam', '?')}")
    print(f"RxMpdu:    BadCrc={crc} BadOverrun={overrun} DupDropped={u32(p.get('ITLAWDLRxDupDropped'))} ReorderHeld={u32(p.get('ITLAWDLRxReorderHeld'))}")
    if seen and seen > 0 and miss is not None:
        print(f"Seq:       seen={seen} missing={miss} ({100 * miss / seen:.2f}%) late={late} gapRuns={runs} longest={longest}")
    else:
        print(f"Seq:       seen={seen} missing={miss} late={late} gapRuns={runs} longest={longest}")
    print(f"TID seen:  {tid_seen}")
    print(f"TID miss:  {tid_miss}")
    if tid_seen[7] > 0:
        print(f"TID7 miss: {100 * tid_miss[7] / tid_seen[7]:.2f}%")
    present = u32(p.get("ITLAWDLCoexSeqMissingPresent"))
    away = u32(p.get("ITLAWDLCoexSeqMissingAway"))
    unknown = u32(p.get("ITLAWDLCoexSeqMissingUnknown"))
    if present is not None or away is not None or unknown is not None:
        print(f"Seq split:  present={present} away={away} unknown={unknown}")
    print(f"schedule:  presentEntries={p.get('ITLAWDLCoexPeerPresentEntries', '?')} known={p.get('ITLAWDLCoexScheduleKnown', '?')} calibrated={p.get('ITLAWDLCoexScheduleCalibrated', '?')}")
    seq = parse_data(p.get("ITLAWDLCoexPeerSequence"))
    if seq and len(seq) >= 16:
        print(f"peer seq:  {list(seq[:16])}")
    if deltas:
        # microseconds → TU (1024 µs)
        tus = [d / 1024.0 for d in deltas]
        print(f"gap Δgp2:  n={len(tus)} min={min(tus):.1f} TU med={sorted(tus)[len(tus)//2]:.1f} TU max={max(tus):.1f} TU")
        near100 = sum(1 for t in tus if 90 <= t <= 110)
        near300 = sum(1 for t in tus if 280 <= t <= 320)
        print(f"           near-100TU={near100} near-300TU={near300}")
    for line in text.splitlines():
        if "Channel:" in line:
            print(f"infra:     {line.strip()}")
            break


def main() -> int:
    if len(sys.argv) == 1:
        scorecard(load_text(None), "live")
        return 0
    for arg in sys.argv[1:]:
        path = Path(arg)
        if path.is_dir():
            after = path / "registry_after.txt"
            scorecard(load_text(after if after.exists() else path / "registry_before.txt"), str(path))
        else:
            scorecard(load_text(path), str(path))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
