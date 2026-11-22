# AWDL reference data: real Mac against the hackintosh (2026-11-20)

What was measured with a real Mac, a second Mac in sniffer mode, and the hackintosh, and what each number says. Facts are marked **measured**;
anything else is a hypothesis and says so. Collection scripts are in [`tools/real-mac-kit`](../tools/real-mac-kit/README.txt); analysis tools in
[AWDL-DIAGNOSTICS](AWDL-DIAGNOSTICS.md).

Machines: real Mac `Mac17,3` (Apple M5, macOS 27.2.1, build 26B131, Apple's own Wi-Fi driver); hackintosh `MacPro7,1`, AX200 under
ForbiddenItlwm, macOS 15.8.2 (24H232); the same iPhone throughout. The real Mac runs a newer macOS, so its numbers are the behaviour to
match, not a byte-for-byte target.

## Continuity Camera

| | Real Mac | Hackintosh |
| --- | --- | --- |
| RTP loss (llw0 capture, 19 s) | **0.00%**: 32,228 and 21,702 packets, no loss runs | about 2.8% of the iPhone's frames never heard (17,293 of 624,194 in the 20 s analyzer run) |
| Channel plan in the real-time group | `161 161 149 161 161 161 161 161 161 161 149 161 161 161 161 161` | `161` in all 16 slots (the plan we advertise) |
| iPhone RTG TLV | `flags:0x0 infrachannel:161 rtgPeersCount:1` | the same (flags 0x0), `ITLAWDLRtgPeerUll` stays 0 |
| UMI | built by the family after receiving one (`umiOption 0xb000000`) | built by `-itlawdlumitx`, acknowledged by the iPhone |

After the four flags in [AWDL-CONTINUITY-UMI-AND-AW-EVENTS](AWDL-CONTINUITY-UMI-AND-AW-EVENTS.md) the iPhone joins the group and the
announced schedule is `161` in every slot: none of the remaining gaps is explained by the iPhone being away. The remaining loss is spread over
all 16 slots (12.7 to 30.7 per 1000), is not tied to the rate (gaps per 1000 frames: 4.1 at MCS 0-7, 4.0 at MCS 8-11), has no schedule period,
and about 2.5% of frames heard carry the retry flag. Unexplained: a weak period near 959 ms (R 0.29, against 0.20 by chance).

## AirDrop discovery (Mac to iPhone, opening the sheet)

**Real Mac** (kernel log, one second resolution unless noted): `sharingd` and `rapportd` start browsing at 35.549; `wifip2pd` creates
`AWDLBrowse` for `_airdrop._tcp.local` and `_applicationservicepairing._tcp.local` and sets ioctl 120 (`AWDL_PEER_SERVICE_REQUEST`) once per
service; the family logs `Adding PTR:._airdrop._tcp.local. service PTR browse`; at 36.025 it switches to `AWDL Fast Discovery`; the iPhone's
first sync frame is heard at 36.127 and `Posted Peer Presence` follows 1 ms later; `sharingd` sees `Endpoints discovered` at 36.119 and starts
HELLO at 36.121. About 0.6 s from opening the sheet to HELLO. The `AWDL_UPDATE_SERVICE_COUNT` ioctl fails with `FAIL:45` and no handler on the
real Mac too, so that log line is not a defect.

**Hackintosh:** presence works (`Posted Peer Presence` 133 ms after the first frame in one run) and is immediate when the iPhone is already
awake on channel 161. When the iPhone's AWDL is idle it is not on 161 for tens of seconds (70 s measured between 18:17:00 and 18:18:10),
the family logs `No Action frames for(2532mSec)` and `Posted Peer Absence` about 4 s after the last frame, and the neighbour entry
disappears. A send started while the entry is missing waited 14.5 s in "Preparing transfer connection" and continued 0.8 s after presence.

The iPhone's neighbour entry was present in 43 of 43 one-second samples on the real Mac and in 22 of 43 and 29 of 43 on the hackintosh. The real
Mac also marks the iPhone absent (12 s after presence in one run) but keeps its entry.

## What the sniffer (channel 161, second Mac) shows

- The iPhone sends action frames steadily on 161 when awake: 5.7 per second in the discovery capture (median gap 131 ms, longest 0.5 s), each
  carrying its service records (TLV 2, about 8 per frame). In the Mac-to-iPhone and iPhone-to-Mac captures the rate was 3.3 and 2.2 per second,
  with a 6.3 s and a 72.1 s gap, the latter after the transfer ended.
- Our action frames: 15 to 20 per second (PSF plus MIF).
- The iPhone's TLV 4 names the hackintosh as master (`668c945fb377`), so it follows our sync; its TLV 18 (in that capture) lists 161 in 6 of 16 slots, 149 in two,
  6 in one, idle in seven, and class 128 (80 MHz); ours advertises class 124 (20 MHz).
- **Rates:** 99.7% of our unicast data frames (Mac to iPhone) are HT MCS 7, 20 MHz, one stream (65 Mbit/s nominal). The iPhone sends us HE MCS 7
  to 11 at 20 MHz. The ladder topped out at HT MCS 7 (`kAwdlCoexRateMcs`).
- **Retries:** 25.5% of our large data frames carry the retry flag, and only 112 of 618 slots (18%) in 40 s carried any large frame; when a slot
  was used it held about 66 frames back to back.
- **Block Ack:** the hackintosh sent 1452 ADDBA requests in 61 s for 74 distinct ones, so each is retried about 20 times by the firmware (about
  78 in the capture towards the other Mac). The iPhone answers each request with ADDBA status 0, immediate policy, window 64, the same dialog
  token, yet `ITLAWDLCoexBaActionSeen[1]` is 0 and `ITLAWDLCoexBaTxAgreements` stays 0: the answers are not seen by the host, so aggregation
  never starts and every frame goes out alone. Why the hardware acknowledges the answers while the host never gets them is not known. `ITLAWDLRawBaAction` and `ITLAWDLRawBaActionUnicast` (action frames counted as the firmware hands them up, before any filter)
  show whether the answers reach the host at all; they were added after these captures and have not yet been read on hardware.

## AirDrop transfer paths

| | Real Mac | Hackintosh |
| --- | --- | --- |
| Mac to iPhone bulk data | all on `awdl0`, bursts of 80 to 93 Mbit/s, 1511-byte frames, QUIC | with wired `en0` up, 18 to 21 MB on `en0` (41 to 154 Mbit/s) and about 2 MB on `awdl0`; with `en0` inactive: 27.3 MB on `awdl0` in 60 s, peak 15.3 Mbit/s |
| iPhone to Mac | about 33 Mbit/s sustained on `awdl0` (115 MB in 28 s) | 69.6 Mbit/s on `awdl0` (108.5 MB in 12.5 s) |
| `awdl0` MTU / linkmtu | 1500 / 1500 | 1500 / 1500 |
| IPv6 refused with EMSGSIZE | none | 7 of 5,087 `sharingd` packets (0.14%), 1497 to 1498 bytes, from the `en0` copy of a multipath flow |

`en0`'s linkmtu is 1492 from the router's Router Advertisement, which `networksetup -setMTU` does not change; the failures are therefore small and
QUIC resends them. See [DEBUGGING](DEBUGGING.md) case 2. `tools/diag/airdrop_paths.py` shows which interface carried a transfer.

The family never entered its `Data` state during the hackintosh transfers (`Fast Discovery`, `Idle`, `Low Power` only), while the real Mac ran
`Data + Infra Data` with plan `149 149 149 149 161 161 161 161 6 149 149 149 161 161 161 161` 43 times. The driver advertises its own plan, so
whether that state matters for our speed is open.

## Action-frame gap counters (hackintosh, 4 minutes after boot)

`ITLAWDLAfCount` 654 frames from other stations on our channel; gaps: 431 under 150 ms, 52 under 300 ms, 103 under 600 ms, 4 under 1 s, 55 under
2 s, 1 under 4 s, 5 under 10 s, 2 of 10 s or more; longest 17.6 s. The counters cover every station, so one quiet iPhone can hide behind another
station's frames.

## Open items

- Block Ack answers never reach the host (above), pending a read of the new raw counters; the firmware's per-request retry storm.
- `-itlawdlht2ss` (two-stream HT up to MCS 15, off by default) is on `main` but untested on hardware.
- Whether a listener that visits 149 and 6 as the family's plans ask would find the iPhone faster. It is not implemented; the PHY retune latency
  is unmeasured and the multi-channel mode earlier collapsed throughput.
- The TLV 4 flags, remaining-AW field and TLV 18 width differences listed in the Continuity Camera note.

## What caps a send: the phone's channel plan (sniffer, 2026-11-22)

A capture of a Mac to iPhone send on channel 161 (second Mac, 80 s; `tools/diag/awdl_sniff.py`):

- During the send the phone announces the plan `149 149 149 149 161 161 161 161 6 149 149 149 161 161 161 161` (526 announcements from
  4 s to 83 s): channel 161 in 8 of 16 slots, 149 in 7, 6 in 1. It is the plan a real Mac runs in its `Data + Infra Data` state.
- Our listener is parked on 161 and advertises 161 in all 16 slots. Frames to the phone flow in 55 % of the 10 ms bins; of the 795 idle
  gaps over 10 ms, the phone transmitted nothing in 701. It was on 149 or 6.
- While frames flow the rate is about 17 Mbit/s (HT MCS7, 20 MHz, single frames, about 5 % retransmitted); over the whole send it is
  9.2 Mbit/s. 8 of 16 slots is 50 % airtime: the cap is the phone's schedule times single-frame efficiency.
- 149 and 161 lie in the same 80 MHz block (149 153 157 161, centre 155). Following the plan to 149 is a change of primary 20 MHz
  subchannel, not a retune of the radio; the AWDLLab note on time sharing (`AWDL_MCC_TIEMPO_COMPARTIDO.md`) estimates 10 to 15 ms per
  switch on an RTL8814 and a slot of 65.5 ms (4 AWs) as the shortest unit that pays. The plan's runs are 4 slots (262 ms) on 149, 4 on 161,
  1 on 6, 3 on 149 and 4 on 161.
- The phone answers every ADDBA request of ours (93 answers, status 0, window 64) and our hardware acknowledges each; the AX200 firmware
  does not hand them to the host (`ITLAWDLRawMgmtRing`).
