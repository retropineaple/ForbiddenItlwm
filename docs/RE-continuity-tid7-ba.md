# Continuity TID-7 Block Ack starvation (ASM + live)

> **Correction (2026-11-18, later).** The `ENOSPC` that this note reads as "command ring contention" between ADD_STA, sync
> frames and the low-latency refresh is a wedged ring, not contention. `-itlawdlcoexlowlat` was in the boot-args in
> every run below; the AX200 never completes `LOW_LATENCY_CMD` (the sync send returns `EWOULDBLOCK` after 1 s), the command's
> slot stays pending, and on the 32-slot ring every command after the index wraps returns `ENOSPC` permanently. The 10 ms
> throttle, the BA setup hold and the busy-gating below treat a symptom of that, and the TID-7 ADDBA ordering fix is the only
> part that stands on its own. Boots without the flag had `BaPeerAdded=Yes`. Counters that show the real state:
> `ITLAWDLCmdTimeouts`, `ITLAWDLCmdEnospcRing/Slot/Resp`, `ITLAWDLCmdReclaimed`, `ITLAWDLCmdQueued`.

## Live evidence (`/var/tmp/cc-vo`, realtime VO build)

| Key | Value | Meaning |
| --- | --- | --- |
| `BaPeerAdded` | Yes | Peer station exists |
| `BaTids` | **1** (bit 0 only) | **No TID-7 BA** |
| TID7 traffic | 56200 seen / 6230 miss (**11.09%**) | Continuity video |
| `BaBusySkips` | **47016** | BaHandle aborted almost every tick |
| `BaStatus` / `LowLatencyResult` | `0x…8000001c` ENOSPC | Cmd ring contention |
| `BaActionSeen` | ADDBA req **207**, resp 1 | Phone keeps sending ADDBA |
| Seq miss present | 6234 / 6267 | Loss inside present slots |

## Code causes (no guesswork)

1. `PendingAddbaRequests::take` used `__builtin_ctz` → **lowest TID first**. TID 0 re-ADDBA starved TID 7.
2. `awdlCoexSendLowLatency` on ENOSPC set `BaBusyUntilUS`, and BaHandle **returned entirely** while busy → pending TID-7 ADDBA never reached `ADD_BA_TID`.
3. AppleBCMWLANCoreMac strings: `ampdu_tid`, `Error: cannot set Block Ack Policy on TID %d`, `ampdu_{tx,rx}_ba_window_cfg` — BA is **per-TID**.

## Port (round 1)

1. `take()` prefers **highest** ready TID (`__builtin_clz`).
2. Busy backoff gates **ADD_STA only** (`!BaPeerAdded`); per-TID ADD_BA continues.
3. Lowlat ENOSPC no longer arms `BaBusyUntilUS`.
4. ADD_BA_TID ENOSPC re-queues without arming global busy when peer exists.

## Regression `cc-tid7` (round 1 on EFI)

| Key | Value |
| --- | --- |
| `BaPeerAdded` | **No** |
| `BaTids` | 0 |
| `BaBusySkips` | **113583** |
| `BaBusyEvents` | 1696 (all ADD_STA ENOSPC) |
| TID7 miss | **13.91%** (worse) |
| `BaSessions`/`BaAnswers` | 1 (stale from an earlier brief success) |

Cause: 100 ms full-BaHandle abort while `!BaPeerAdded` + Continuity sync/data/pause still competing for the host cmd ring → ADD_STA never recovers.

## Port (round 2)

1. Replace 100 ms busy abort with **10 ms AddPeerNextUS** throttle (50 ms when idle).
2. **BA setup hold**: while Continuity/realtime and pending ADDBA without peer, skip sync TX, data pump, and pause-step so ADD_STA owns the cmd ring (`BaSetupHolds`).
3. Defer ContinuityPower host cmds until peer exists.
4. Clear `BaSessions` on RemovePeer.

## Expect after reboot

`BaPeerAdded=Yes`, `BaTids` includes bit 7, `BaBusySkips` not 100k+/min, TID7 miss below prior 11–14%.
