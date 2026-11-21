# Continuity Camera: availability-window events, real UMI transmit, sync alignment and ULL infra

Technical notes for `-itlawdlawevents`, `-itlawdlumitx`, `-itlawdlsyncalign` and `-itlawdlull` (Sequoia 15.x, IO80211Family
24H232, x86_64). All four are opt-in. The addresses and offsets below are from that build's disassembly; on other builds the code
that needs them is a no-op (see [BOOT-FLAGS](BOOT-FLAGS.md)).

## Why it was needed

A Continuity Camera session works only once the iPhone joins the Mac's real-time group (RTG). The iPhone does that when it hears the
Mac's unicast master indication (UMI, option `0xb000000`) and then adopts the Mac's channel plan. Before these flags:

1. The driver never posted the availability-window (AW) events the Broadcom firmware posts, so the family's `inAvailabilityWindow()`
   stayed false and `isSafeToSendUmiNow` never allowed the UMI.
2. The family's UMI request (ioctl 149) reached the driver, which acknowledged it with a fake status (message 69). The acknowledgement
   hid the fact that no UMI ever went on the air.
3. The iPhone never saw a UMI, never sent an RTG TLV (type 31), and kept its default 149/161 plan, which is away from our channel
   about half the time (the 2 Hz judder and the bursts of lost frames in [AWDL-REFERENCE-DATA](AWDL-REFERENCE-DATA.md)).

With all four flags the iPhone sends its RTG TLV (`parseAWDLRealTimeGroupTLV ... RTG(Active) TLV of Peer(Active)`, `ITLAWDLRtgPeerSeen`
counts up) and the family composes the real Mac's plan.

## What the family does (24H232)

| Item | Detail |
| --- | --- |
| AW state | `inAvailabilityWindow()` is ivars+0x304, set by event 41 and cleared by event 43 |
| Time left | `awRemainingTime()` = (ivars+0x4290 x ivars+0xbe60) - (now - ivars+0x7fa8) |
| UMI path | `handleUmiTimer` -> `sendUnicastMI` -> `buildMasterIndicationTemplate` -> `ioctl_internal(149)` |
| ioctl 149 | `APPLE80211_IOC_AWDL_OOB_REQUEST`, a 56-byte record: destination at +4, option at +0x14 (6, or 14 = UMI on dwell), our BSSID at +0x16, template length at +0x28, kernel pointer to the template at +0x30. The buffer is reused, so the driver copies the template at once |
| UMI status | message 69, status byte at +6: 0 acknowledged, 1 failed |
| Event 41 | 36 bytes: AW number (dword at 0, reserved word zero), sync state at 4, master MAC at 8, top master at 0xe, FwAwStartTime (big-endian microseconds) at 0x14, channel version/number/flags at 0x18/0x1c/0x20 |
| Event 42 | 8 bytes |
| Event 43 | 40 bytes |
| Clock | slot = (AwNumber % 64) / 4; AwNumber moves by 4 per 64 TU slot (65.536 ms); FwAwStartTime steps by 65536 us; a deviation over 5121 us is only logged |
| Context | events must be posted inside the family workloop gate |

A real Mac and the iPhone share one AW counter: the UMI goes out at `AW: 37020 Slot: 7`, the iPhone's own count, and the iPhone adopts the
Mac's composed plan within 14 to 106 ms (RTG start `0xb000000`, stop `0xc000000`).

## What the driver does

`ForbiddenItlwm/AirportItlwmV2.cpp` (interface) and `itlwm/hal_iwx/ItlIwx.cpp` (radio).

- **`-itlawdlawevents`** - `awEventTimer` runs on the interface workloop. `postAwEvent` posts event 41 at every slot start and event 43 16 TU
  later (`APPLE80211_M_AWDL_AVAILABILITY_WINDOW_END`). Without `-itlawdlsyncalign` the AW number is a free-running 65536 us slot clock. The
  sync state comes from the native state; master and top master are our frame address; channel and flags come from the requested sequence
  entry for that slot.
- **`-itlawdlumitx`** (needs `-itlawdlawevents`) - `noteUmiRequest` records the first 56-byte request and, when the template pointer is a
  kernel address, copies the template into a queue of two. `AirportItlwm::buildAWDLUnicastMI` builds the frame (`ItlAWDL::buildMasterIndicationTLVs`,
  `itlBuildSync`, `buildLocalFrame` subtype 3, receiver = the peer). `awdlCoexTxSend` in the radio sends it in place of a sync probe once
  `sendGate(now, true, kAwdlAwSyncWindowUS * 4)` is open. The transmit completion in `iwx_rx_tx_cmd` reports `itlAWDLUmiTxResult`, and the
  interface posts message 69 with 0 (acknowledged) or 1 (failed). The old fake acknowledgement is disabled while this is on.
- **`-itlawdlsyncalign`** - `itlAWDLPeerClock` exports the iPhone's AW clock (extended-window number, start and length) from the peer schedule
  when it is current and fresh. `itlBuildSync` then takes the master from the iPhone's election TLV (TLV 24, value +6) and sets the epoch
  to the extended-window start and the AW counter to `eaw * 4`; `postAwEvent` posts event 41 on the iPhone's window boundaries. Without
  it the frames use the old fixed schedule (host-uptime counter, own MAC as master).
- **`-itlawdlull`** - `applyNdpKeep` sets the family's ULL flags through `setAwdlCtl` (controller and manager `P2PForULLRTInfra`, the AWDL
  control byte at +0xca54, `isAdaptCommonSocialChannelForUltraLowLatencyInfra`) and re-asserts them every 5 s. The RTG TLV then carries
  flag 0x20.
- **Related, already documented:** `-itlawdlpeeralive` and `-itlawdlrtgkeep` keep the iPhone in the family's peer table.

## Recommended combination

```
-itlawdlawevents -itlawdlumitx -itlawdlsyncalign -itlawdlull -itlawdlpeeralive -itlawdlrtgkeep itlawdlcoexskipdtim=2
```

`-itlawdlumiack` (the fake acknowledgement) must not be set with `-itlawdlumitx`. `-itlawdlcoexmulti` stays off: it collapsed throughput to
one-stream MCS 1-2 and about 28% RTP loss in the 2026-11-20 test.

## Registry properties to check

On the interface (`ioreg -l -w0 | grep ItlwmAWDL`):

| Property | Expected with all flags |
| --- | --- |
| `ItlwmAWDLAwEventsEnabled` | Yes |
| `ItlwmAWDLAwAligned`, `ItlwmAWDLAwAlignedStarts` | Yes, and counting |
| `ItlwmAWDLUmiTxEnabled`, `ItlwmAWDLUmiRequests` | Yes, at least 1 |
| `ItlwmAWDLUmiFrameTlvs`, `ItlwmAWDLUmiFrameLength` | `5,18,24,31,12,7`, about 309 bytes |
| `ItlwmAWDLUmiTxResultsPosted` | at least 1 |
| `ItlwmAWDLUllEnabled`, `ItlwmAWDLUllActive` | Yes |

In the radio driver:

| Property | Meaning |
| --- | --- |
| `ITLAWDLUmiBuilt`, `ITLAWDLUmiSubmitted`, `ITLAWDLUmiAcked`, `ITLAWDLUmiFailed` | UMI frames built, handed to the radio, acknowledged by the iPhone, and failed |
| `ITLAWDLUmiDeferred` | send attempts held because the gate was closed (many is normal) |
| `ITLAWDLUmiLastTxStatus` | last firmware status; 1 is success |
| `ITLAWDLRtgPeerSeen`, `ITLAWDLRtgPeerTlv` | RTG TLVs heard from the iPhone and the last one (flags 0 on the real Mac too) |
| `ITLAWDLRtgOwnSeen`, `ITLAWDLRtgOwnTlv` | our own RTG TLVs (flags 0x20 with ULL) |

Kernel log lines that confirm the chain: `sending UMI#... (AW: n Slot: s)` with `(n % 64) / 4 == s`, `cachedPeerHasChangedChannelSequence ...`,
`addPeerToRealTimeGroup`, then `parseAWDLRealTimeGroupTLV ... Peer(Active)` and `setScheduleState ... RTG: Active`.

## Known limits

- Offsets and symbols are for 24H232 only.
- The UMI receive path is observed, not acted on: `ItlwmAWDLUmiRxRing` (20-byte entries) records the action frames in the 3 s after an acknowledged
  UMI; read it with `tools/diag/umi_rx.py`.
- The frame still differs from the real Mac's on the air in ways not yet measured over the air: TLV 4 flags (our literal 0x1000 against 0x1800
  in AWDLLab's captures), the remaining-AW field outside the first AW of a slot, and TLV 18 width (we advertise 20 MHz, the iPhone 80 MHz).
