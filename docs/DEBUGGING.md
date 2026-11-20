# Debugging AWDL

How to find out what the AirDrop link is doing, with the tools this repository has. Everything here was used to find the two problems in [the cases below](#case-1-a-send-stalls-after-the-iphone-sat-idle), on an Intel AX200 (AMD Ryzen 9 5950X desktop) with macOS Sequoia 15.8.2 (24H232) and 0.5.4-beta.2. Commands marked **root** need `sudo`; the rest only read.

## 1. Start with the state

```bash
ifconfig awdl0                                   # exists, UP, an fe80:: address, mtu 1484
kmutil showloaded --list-only | grep -i forbidden   # the loaded build's UUID, to be sure it's the one you installed
ioreg -l -w0 | grep -E '"ItlwmAWDL(InterfaceBuild|BuildCheck|NetworkStage|DebugFlagsSet)"'
ioreg -l -w0 | grep -E '"ITLAWDLCoex(Added|Channel|CountryLearned|BaFilterOff|PromiscLive|BaPeerAdded)"'
bash tools/compat/check_compat.sh                # the whole picture, including the wired-MTU check
```

`awdl0` missing, `BuildCheck` not `verified`, or `Added` No means AirDrop isn't running: see [TROUBLESHOOTING.md](TROUBLESHOOTING.md). Otherwise go to the counters.

## 2. Boot flags for debugging

The tested flag set in [BOOT-FLAGS.md](BOOT-FLAGS.md) already includes `-itlawdlcoexbatrace` (Block Ack events in the registry). Add these when you need more:

| Flag | What it gives you |
| --- | --- |
| `-itlawdlpmdebug` | macOS's AWDL peer-manager debug flags, so the kernel log says why peers are accepted or rejected. Verbose |
| `-itlawdlsamples` | Raw frame samples in the registry. **They can hold device names and addresses**; never share them |

Don't add the dedicated-mode flags (`-itlawdlrx` and the ones that build on it), and don't use `-itlawdlbafilterkeepoff` or `-itlawdlbanopromisc`: they belong to that mode, where normal Wi-Fi is down.

## 3. What the registry counters mean

All under the I/O Registry root (`ioreg -l -w0 | grep ITLAWDLCoex`). Counters only go up; compare two readings.

**The iPhone's presence**

| Key | Meaning |
| --- | --- |
| `PeerSyncFrames` | AWDL sync frames heard from the phone. Rising about once a second means it is on our channel and awake |
| `AwdlAction` | AWDL action frames heard (all peers) |
| `PeerPresentEntries` | How many of the phone's 16 channel-sequence slots currently point at our channel |

**The send side**

| Key | Meaning |
| --- | --- |
| `TxBuilt`, `TxOK`, `TxFailed` | Our AWDL sync frames built, completed, and failed |
| `DataTx` | Data frames the driver sent for `awdl0` |
| `UcastOK`, `UcastFailed` | Unicast frames the firmware reported as acknowledged or not by the phone's radio. A burst of `UcastFailed` with no `UcastOK` means the phone wasn't there to answer |
| `GroupOK`, `GroupFailed` | Group (multicast) frames. For these the status only means the frame left the queue |
| `BaTxStateName` | Our send Block Ack agreement: `asking` while the phone hasn't answered, `agreed` when it works. A state that stays `asking` means every frame goes out singly (see the Mac → iPhone item in [TROUBLESHOOTING.md](TROUBLESHOOTING.md)) |
| `BaTxFrames`, `BaTxAcked` | Frames sent in aggregates and how many the phone's Block Ack acknowledged. `BaTxAcked` / `BaTxFrames` is the real delivery rate: 99.6% in the 2.8 GB test |
| `MgmtFailed`, `BaAskFailed` | Management frames (ADDBA, DELBA) that failed, and Block Ack requests that failed. Small numbers are normal |

**The send gate** holds frames while the phone is off our channel, so they aren't wasted.

| Key | Meaning |
| --- | --- |
| `GateHolds` | Hold episodes started |
| `GateAwayTicks`, `GateDozeTicks` | How often a pass was held because the phone was away, or dozing until its next availability window |
| `GateForced` | Frames sent anyway after the hold's limit, rather than starve |
| `PeerPauses` | Times the phone's firmware queue was paused while it was away |

**The receive side**

| Key | Meaning |
| --- | --- |
| `DataRxUcast`, `DataRxGroup`, `DataRx` | Data frames received, unicast, group, and the total handed to `awdl0` (`DataRxHandedOff`) |
| `BaFilterOff`, `PromiscLive`, `BaFilterChanges` | Whether promiscuous receive is off (Yes is normal while a peer is active), whether the firmware has it on, and how often it changed |
| `PromiscDropped` | Frames heard through the listener while promiscuous receive was on (or uncertain) that were dropped so they don't reach the Wi-Fi station's stack. Large numbers are normal |
| `BaPeerAdded`, `BaPeerAccepted` | The phone's firmware station exists; the phone's Block Ack request was answered |
| `BaSessions`, `BaRxIdleTeardowns` | Receive sessions opened, and torn down after 60 s idle |

**Where received frames are lost or dropped** (added while chasing Continuity Camera's video loss; all at the registry root). The driver counts at the first point it sees a frame and again at each place it can drop one. (`ITLAWDLRxMpduBadCrc` to `ReorderHeld` and `SeqGapRing`, and the per-TID and bin counters under Sequence gaps, were added most recently and had not been read on hardware when this was written; `SeqSeen`, `SeqMissing`, `SeqGapRuns`, `SeqLongestGap` were.)

| Key | Meaning |
| --- | --- |
| `ITLAWDLRxMpduTotal` | Every frame the firmware reports, before any check (all stations and channels, not just AWDL) |
| `ITLAWDLRxMpduBadCrc`, `ITLAWDLRxMpduBadOverrun` | Frames the firmware flagged as failing the CRC, or overrun, and the driver dropped at its first check. Radio corruption shows up here, and nowhere else: a corrupted frame has no usable sender or sequence number |
| `ITLAWDLRxBadCrcRuns`, `ITLAWDLRxBadCrcRunLongest` | How many runs of consecutive bad frames, and the longest run |
| `ITLAWDLRxDupDropped` | Frames dropped as duplicates of one already received (retries) |
| `ITLAWDLRxReorderHeld` | Frames the receive reorder buffer kept back to deliver in order. They come out later, so this is not loss |
| `ITLAWDLCoexRxRejectReasons` | Data frames the listener heard from a known peer that were not handed on, by the first check each failed (`DataReject` in `include/AWDLDataFrame.hpp`: short, QoS Null, wrong BSSID, protected, A-MSDU, outer length, not LLC/SNAP, not IPv6, bad IPv6). `python3 -I tools/awdl_rx_rejects.py` reads and explains it; with two registry dumps it shows the change between them. `DataRxMalformed` is its total. In a healthy run it stays 0 |
| `ITLAWDLCoexRxOuterLengthSamples` | The first few frames rejected for a wrong outer length (128 bytes each, with a 2-byte total length in front), to learn their layout. This is how the camera's plain RFC 1042 frames were identified; the parser now accepts them, so only frames with a genuinely wrong length land here |

**Sequence gaps** measure frames the iPhone sent that this radio never heard. 802.11 numbers each TID's frames; the driver watches the unicast QoS data of its Block Ack peer and counts a forward step of 2 to 2047 as missing frames. A frame that arrives behind the newest one is counted as late (reordering), not missing. Caveat: the sender numbers per receiver, so frames it sent to another device would look like gaps too. Compare with the media stack's own loss figure before treating this as exact.

| Key | Meaning |
| --- | --- |
| `ITLAWDLCoexSeqSeen`, `SeqMissing` | Frames seen, and frames missing by sequence number. 7.5% missing broke the camera's video |
| `SeqGapRuns`, `SeqLongestGap`, `SeqRunBins` | How many gaps, the longest, and their lengths in bins (1, 2, 3-4, 5-8, 9-16, 17-32, 33+). Many short gaps and a few long ones point at different causes |
| `SeqLate` | Frames that arrived behind the newest one. If it is close to `SeqMissing`, the gaps are reordering, not loss |
| `SeqTidSeen`, `SeqTidMissing` | Per TID (8 values each). Loss on one TID only is probably another receiver's frames |
| `SeqGapRing`, `SeqGapRingCount` | The last 48 gap events, 12 bytes each: firmware time (`gp2`, microseconds, u32), frames missing (u16), sequence number (u16), length (u16), TID (u8), retry (u8). The firmware time lets you line the gaps up with the AWDL schedule |

**`llw0`** (with `-itlawdlllw`; `ioreg -l -w0 | grep ItlwmLLW`):

| Key | Meaning |
| --- | --- |
| `ItlwmLLWStage` | `registered` once the interface exists |
| `ItlwmLLWRxWanted`, `RxDelivered`, `RxFailed` | Frames claimed for `llw0`, delivered into its queue, and failed. `RxDelivered` equal to `RxWanted` with `RxFailed` 0 is the healthy state |
| `ItlwmLLWRxDestinationsSeen`, `TxDestinations` | Frames by IPv6 destination class (link-local, unique local, multicast, other). Camera media is unique local |
| `ItlwmLLWTxOffered`, `TxForwarded`, `TxDropped`, `TxCompleteFailed` | Packets macOS gave `llw0` to send, those queued onto `awdl0`'s ring, dropped, and failed to complete |
| `ItlwmLLWEnableCalls`, `DisableCalls`, `Mtu`, `MtuResult`, `CreateResult` | Lifecycle: `Enable` 1 once macOS enabled it, the MTU in force, the result of creating it |

`ifconfig llw0` and `netstat -I llw0 -b` give the kernel's view (`Ipkts`, bytes). `llw0`'s `Ipkts` should track `ItlwmLLWRxDelivered`.

**On the interface** (`ioreg -w0 -r -c ItlAWDLInterface -d 1 -l`): `ItlwmAWDLTxPacketsOffered`, `TxAccepted`, `TxCompleted` and `TxDropped` count packets from macOS into the driver. `TxDropped` should stay 0. `ItlwmAWDLClientEvents` records who opened and closed `awdl0`. `ItlwmAWDLLastUnsupported` names the last call the driver refused, usually `setMacAddress` or `setAWDL_AWDL_ADVERTISERS`, which is expected ([noise](#6-log-noise-that-is-harmless)).

The action-frame gap counters (`ITLAWDLAf*`), the UMI and RTG counters and the analysis tools added for the camera and AirDrop work are described in [AWDL-DIAGNOSTICS](AWDL-DIAGNOSTICS.md).

## 4. Logs

Use `/usr/bin/log`: in zsh a bare `log` is a built-in. `log show --start` wants whole seconds (`"2026-11-15 11:35:26"`); fractions return nothing.

```bash
# AirDrop outcome: the lines that say whether a send worked, over which transport
/usr/bin/log show --last 10m --info --style compact \
  --predicate 'process == "sharingd" AND (eventMessage CONTAINS "send-finish" OR eventMessage CONTAINS "TransferResult" OR eventMessage CONTAINS "Transfer connection failed" OR eventMessage CONTAINS "Ask failed")'

# macOS's AWDL peer manager (with -itlawdlpmdebug it is much chattier)
/usr/bin/log show --last 10m --info --style compact \
  --predicate 'process == "kernel" AND (eventMessage CONTAINS "IO80211AWDLPeerManager" OR eventMessage CONTAINS[c] "awdl")'

# stream it to a file while you test
/usr/bin/log stream --style compact --info --debug --predicate '<one of the above>' > live.log
```

What to read in a `send-finish` line: `result` 0 is success; `failureSubType` says why it failed (`cancelled`, `unexpected-40` is `EMSGSIZE`, `unexpected-57` is `ENOTCONN`); `transport` is `awdl`, `directWired` (your Ethernet, not AWDL at all), or `other` (cancelled before a path was chosen); `size` and `bytesPerSec` give the speed.

## 5. Capture and trace

`python3 -I tools/diag/cc_analyze.py` reads a run's registry keys and prints PASS or FAIL for each thing that has gone wrong in a camera run so far: a wedged command ring, no TID 7 Block Ack, a station that never sleeps or a power table the firmware refused, loss by sequence number, gaps tied to the access point's beacons (chi-square against a uniform spread, and split by beacon class), and what rates and widths the iPhone really uses. Run it live, on a saved `ioreg -l -w0`, or with two dumps to see the difference. `tests/cc_analyze_test.py` keeps its maths honest.


### Packets (root to capture)

```bash
sudo tcpdump -i awdl0 -nn -s 0 -w big.pcap 'ip6 and not udp port 5353'
python3 -I tools/diag/awdl_flow.py big.pcap
```

The analyzer prints, per flow, the clock time of its first and last packet, packets and bytes each way, the longest gaps, and a one-line timeline. A flow that ran steady shows a dense timeline; one that stutters shows empty buckets and long gaps. Stop `tcpdump` before a very large send: a 589 MB transfer would fill the disk.

The capture is taken **above the driver**. It shows what macOS handed to `awdl0`, not what went over the air, and packets macOS refuses before the interface (`EMSGSIZE`) never appear in it.

### Counters over time

```bash
bash tools/diag/awdl_counters.sh 600 1.5 > series.txt     # 10 minutes, a line every ~1.5 s
```

Each line starts with the clock time, so a stall in the capture or the log can be lined up with the counters. Look at `BaFilterOff`, `DataRxUcast` and `UcastOK` first.

### Is a packet size dropped inside the driver?

```bash
bash tools/diag/awdl_ping_ladder.sh fe80::xxxx:xxxx:xxxx:xxxx%awdl0     # address from `ndp -an | grep awdl0`
```

It pings with payloads from 56 to 1436 (IP sizes 104 to 1484), in shuffled order over three rounds. A real size limit shows as the same sizes failing in every round. Failing sizes that move around are timing: the iPhone is on our channel only part of the time, and an idle one can lose whole steps. Run it with the iPhone awake, and read a result where even the 56-byte ping fails as "the phone wasn't there", not as a size.

### What macOS asks of the driver, and what it waits for (root, `dtrace`)

For Continuity Camera, `tools/diag/trace_peer_cache.d` (the peer-cache request and its caller), `tools/diag/trace_umi.d` and `tools/diag/trace_umi_chain.d` (each wait on a peer-manager event, each signal, the UMI chain) print one line per call with a short stack. Start one in the foreground, reproduce, then Ctrl-C; the output file is the result. See [Case 3](#case-3-continuity-camera-gets-part-of-the-way).

### Capturing a Continuity Camera attempt (root)

`tools/diag/llw_attempt.sh` runs a 50 s capture to `/var/tmp/izzy-llw-HHMMSS`: `awdl0` and `llw0` packet captures, the registry before and after, the log, Skywalk's own tables (`skywalkctl` interface, channel, flow, flow-owner, flow-switch) and `tools/diag/trace_llw_netif.d`. Start it, then open Photo Booth and choose the iPhone camera within the window. An empty `llw0.pcap` (24 bytes) means no traffic crossed `llw0`: check the iPhone was listed as a camera and the stream was started inside the window before suspecting the driver. `python3 -I tools/awdl_rx_rejects.py` and the counters above read the driver's side.

### Who refuses a packet (root, `dtrace`)

SIP has to allow `dtrace` (`csrutil status`; it was disabled on the test Mac). Run it in the foreground and stop it with Ctrl-C, so it flushes. **`fbt` could not see this driver's own functions on the test Mac** (`dtrace -l -n 'fbt::*ItlIwx*:entry'` matched nothing although the kext was loaded; a script that probes them runs silently and prints nothing, because `-Z` allows probes that don't exist). `fbt` works for Apple's kernel and IOSkywalkFamily, so probe macOS's side and use the registry counters for the driver's.

```bash
sudo dtrace -Z -e -s tools/diag/trace_emsgsize.d              # compile check, no output means it compiled
sudo dtrace -Z -q -s tools/diag/trace_emsgsize.d -o emsgsize.out
# reproduce the failure, then Ctrl-C and read emsgsize.out
```

`trace_emsgsize.d` prints the kernel function that returns `EMSGSIZE` and its stack, for `sharingd`. `trace_emsgsize_size.d` also prints the length of each rejected packet. Don't run the second one through a bulk send: at about 18,000 packets a second it overloaded `dtrace`.

## Case 1: a send stalls after the iPhone sat idle

**Symptom.** Mac → iPhone sat on "Waiting". A capture showed five connection attempts in 32 s, each retrying its SYN about ten times, and no packet at all from the iPhone. `ping6` got 0 of 6 replies.

**What ruled things out.** The phone was present (`PeerSyncFrames` rose 16 times in 8 s) and its radio acknowledged our frames (`UcastOK` rose), yet `DataRxUcast` stayed at 5707.

**The clue.** Sampling every 6 s: at 11:52:05 `BaFilterOff` No, `PromiscLive` Yes and nothing arriving; at 11:52:11 `BaFilterChanges` went 32 → 33, the filter turned off, and `DataRxUcast` jumped 36 within the same interval. Later, with the filter off, a send moved 207 frames. The earlier successes (a ping at 11:37, an AirDrop at 11:41) all had the filter off.

**The cause**, in the code: `receiveSession = BaPeerAdded && BaTids` decided `wantOff`. The receive TID is torn down after 60 s idle (`BaRxIdleTeardowns` went 6 → 9), while the peer station and the send agreement live up to ten minutes. In between, promiscuous receive was on, the firmware didn't acknowledge the station's frames, and a send we started got no replies until the phone opened a session itself.

**The fix.** `wantOff = BaPeerAdded && …` in `awdlCoexBaHandle`. **Verified:** `BaFilterOff` stayed Yes in all 48 samples of an 85 s transfer, with no filter changes.

## Case 2: big sends die with `unexpected-40`

**Symptom.** Sends of 16 MB and 589 MB failed about 1.5 s in, every time; a 0.4 MB and a 2.8 MB send had worked. The driver looked clean.

**What the data said.** The capture showed each failing QUIC flow as a ramp of about 1.4 s, then 1,170 packets (1.6 MB) handed to `awdl0` in 0.3 s, then silence. The driver sent the whole burst (`DataTx` +1,192) and the phone's Block Ack covered 97% of it. `TxDropped` was 0. So the radio and the driver carried the data; the connection died in macOS about 70 ms after the last packet, with `NWError 40`.

**Narrowing it.** `ifconfig`, `ndp -i awdl0` and the route to the phone all said MTU 1484. No UDP payload in the capture exceeded 1436, and no ICMPv6 "packet too big" came back. `trace_emsgsize.d` then named the function: `ip6_output_list` returned `EMSGSIZE` to `udp6_output`, from `sendmsg` in `sharingd` (62 times over seven failed sends). `trace_emsgsize_size.d` gave the lengths of the refused packets: 1494 to 1500 bytes, above 1484.

**The cause.** The log showed the QUIC connection created over `en0` (Ethernet, MTU 1500): `[C101 IPv6#…%en0 … quic]`, `path over en0 … established`. QUIC sized its datagrams for 1500 and sent them out `awdl0`.

**The root cause, from the assembly (IO80211Family 24H232).** `IO80211VirtualInterface::peerToPeerConfigureIfnet` (0xffffff80022a6cfc) sets awdl0's MTU with `safeMetaCast(controller, "AppleBCMWLANCore")` and then `mov esi, 0x5dc` (1500) if the controller is Apple's Broadcom one, `mov esi, 0x5cc` (1484) if not (0xffffff80022a6dc5 and ...6dcc), then `ifnet_set_mtu`. On Apple's own hardware awdl0 is 1500, the same as a wired interface, so this failure cannot happen there; every other controller is given 1484, and AirDrop's QUIC, sized for the wired path, is refused on awdl0. The driver now sets 1500 itself after the family has run (`prepareBSDInterface`, the mechanism `llw0` already uses) and registers 1500 on Sequoia; `itlawdlmtu=N` (1280 to 1500) chooses another value, 1484 being the family's. `ItlwmAWDLMtu` and `ItlwmAWDLMtuResult` in the registry show what was set. `python3 -I tools/diag/airdrop_check.py` finds this case, and the others below, in one run. The older fix, which still works for builds without this, is to lower the wired interface to 1484 (`sudo networksetup -setMTU Ethernet 1484`). **Verified:** a 2,882,165,196-byte send over AWDL completed at 34 MB/s with no rejections. Two attempts right after the change still failed on stale 1500-sized connection state; a fresh connection was fine. [TROUBLESHOOTING.md](TROUBLESHOOTING.md#big-airdrop-sends-die-after-a-couple-of-seconds-unexpected-40) has the user-facing steps.

**A trap in this case.** The first retest after the MTU change succeeded, but `transport` said `directWired`: AirDrop had gone over your LAN, not AWDL, so it proved nothing about AWDL. Check the transport before believing a result. The proof came when the iPhone was disconnected from the network and `transport` read `awdl`.

## Case 3: Continuity Camera gets part of the way

Continuity Camera (the iPhone as the Mac's camera) is the one Continuity feature that needs the AWDL driver to answer more than discovery does. Handoff, Universal Clipboard, calls, Instant Hotspot and AirPlay Receiver need nothing from it; `tools/diag/continuity_check.sh` shows what is ready, and its camera section reports each stage below (the iPhone listed, the driver's peer-cache and UMI counters, the CPU-family check, and how many lines of each failure the log holds). On the test Mac (AX200, macOS Sequoia 15.8.2) the camera went from a black picture, through the stages below, to a stream that runs but loses packets; each stage had its own log line. You can tell where yours stops from the line each stage logs.

**0. The iPhone isn't in Photo Booth's list at all.** This is the iPhone, not the driver. The camera agent keeps a list of usable devices; look for the iPhone in it:

```sh
log show --last 3m --style compact --predicate 'process == "ContinuityCaptureAgent"' | grep -A2 RapportDevices
```

`RapportDevices : ()` means it isn't offered. The iPhone has to be unlocked, awake, near the Mac, with Continuity Camera on (Settings > General > AirPlay & Continuity). After a reboot it can take a few minutes, and toggling Bluetooth on the iPhone makes it advertise again.

**1. `Traffic register failed 'RemoteCamera' ... 0xE00002CA` (an I/O error), after about 3 s.** macOS asks the driver to add the iPhone to a firmware peer cache (`setPEER_CACHE_CONTROL`) and waits for event 51 to say it's done. Fixed: the driver answers and posts event 51 after the handler returns. Check `ItlwmAWDLPeerCacheRequests` and `ItlwmAWDLPeerCacheCompleted` in the registry; they should be equal. `tools/diag/trace_peer_cache.d` shows the request and its caller.

**2. The same error, `Ensemble start Failure, waiting for UMIStatus` in the kernel log.** The next step sends the iPhone a unicast action frame and waits 3 s for its transmit status (message 69). macOS sends it from a timer that only runs inside an AWDL availability window, and the driver reports none (`monitorAWEvents WARNING: Did not get AW start event` every second). With `-itlawdlumiack` the driver posts the status itself, and the log says `Peer acked Ensemble start UMI request`. `tools/diag/trace_umi_chain.d` prints each wait and signal: every `WAIT event=9` should be followed straight away by `SIGNAL event=9`. `ItlwmAWDLUmiAckPosted` counts the statuses.

**3. `No supportedPayloads for this hardware`, then a stream timeout (`Code=-536870186`).** Not the driver and not Wi-Fi. macOS decides the Mac has no video format for the camera because `-[VCHardwareSettingsMac canDoHEVC]` returns false. It reads `hw.cpufamily` and refuses a fixed list of older Intel families (Penryn 0x78ea4fbc, Nehalem, Westmere, Sandy Bridge, Ivy Bridge, Haswell, Broadwell and a few older ones); Skylake (0x37fc219f) and newer pass. An AMD machine with the usual "Force CPUFAMILY_INTEL_PENRYN" kernel patch reports Penryn. Check yours:

```sh
sysctl -n hw.cpufamily      # decimal; 2028061628 is Penryn, 939270559 is Skylake
```

Changing the patch's immediate to 0x37fc219f made `canDoHEVC` return true on the test Mac, and the error went away. That value is read by the whole system, so treat it as a change to your own OpenCore setup: try it on a copy of `config.plist`, run `tools/oc/ocvalidate.sh` on it, keep the old file, and watch audio, sleep and the GPU. This repo does not change it for you. The disassembly came from `xcrun dyld_info -disassemble -arch x86_64h /System/Library/PrivateFrameworks/AVConference.framework/Versions/A/AVConference` (it reads the system cache directly).

**4. `VCTransportSessionNW startNWConnection: nw_connection ready timeout`, "Server ignoring low latency without local llw0".** The stream negotiates, then the media stack opens a UDP connection to the iPhone at a per-attempt `fdXX:XXXX:XXXX::NN` address with the flags "LowLatency Handoff". With only `awdl0`, that address has no interface, route or neighbor, so the connection never becomes ready (`AVCVideoStream create error ... Code=32002`). Fixed by `llw0` (`-itlawdlllw`). Getting it to exist took four things that are easy to miss: macOS's request 293 wants the BSD name written back at +8; the family's power-state handler deadlocks against the 293 handler on the work loop (override it); two Skywalk interfaces with the same MAC get the same `ifnet` unique id and the second one never appears (use the MAC plus `"ll"`); and every Skywalk interface must stay at power state 1 (`ioreg -p IOPower`). Check with `ifconfig llw0` (`UP,RUNNING`, a link-local address) and the `ItlwmLLW*` keys.

**5. The iPhone's frames arrive but the driver rejects them (`DataRxMalformed` in the thousands, almost all "outer length").** This looked like several packets aggregated into one frame; it was not. The frames carry a plain RFC 1042 header (`aa aa 03 00 00 00 86 dd` and then IPv6) with no AWDL header, and the parser read the ethertype `86dd` as a length. `ITLAWDLCoexRxOuterLengthSamples` gave the bytes and `tools/awdl_rx_rejects.py` the split. Plain RFC 1042 frames belong to `llw0`, Apple-header frames to `awdl0` (what Broadcom's host does), and `llw0`'s own transmit frames must be plain too. All three are fixed.

**6. The stream runs, but the video breaks up.** With stages 0 to 5 in place the iPhone's camera stream is delivered to `llw0` (about 40,000 frames and 55 MB in the first minute, `RxFailed` 0). `ContinuityCaptureAgent` then logs `VideoPacketBuffer ... Error while detecting missing packets missingMediaPacketsCount=8 mediaCount=25 parityCount=0` and `Failed to parse reconstructed packet`: a good part of each video frame's packets are missing, and there is no error correction (`parityCount=0`). Where the loss is not: the driver handed on every frame it heard, 0 were rejected, `ItlwmLLWRxDelivered` equals `RxWanted`, the kernel's UDP and IPv6 counters show about 12 drops from full socket buffers in 52,000 datagrams, and the firmware reported **0 frames with a bad CRC or overrun** out of 17,647 (`ITLAWDLRxMpduBadCrc`), so it is not corrupted frames. Reordering is not it either: `SeqLate` was 5 against 1,469 missing.

What the measurements say: about 8.7% of the iPhone's frames never reach the driver, almost all on TID 7 (the video: 1,449 missing of 15,045 seen; TIDs 0 and 6 lost 7 and 13), in bursts of 1 to 33 frames. The last gap events (`SeqGapRing`, firmware time in the `gp2` field) fall about every 96 to 104 ms (often exactly 96.0 TU) and in multiples near 300 ms, with the larger gaps at the 300 ms spacing. The Mac's Wi-Fi was associated on **channel 161 while AWDL was on 149**. The firmware shares one radio between the two channels, and a period of 100 ms with every third visit longer is the access point's beacon interval and a DTIM period of 3: with power save on, the station has to hear those beacons on 161. That is a reading of timing, not a measurement of the firmware's schedule.

**What macOS tells the iPhone about our width (asm).** The capability TLVs are bounds-checked copies of cached state: `buildIEEE80211CntnrTlv` (TLV 17) only copies a buffer, and `getAwdlVHtCapabilityIE` copies 11 bytes out of the peer manager (`ivars+0x951`, `+0x959`, `+0x95d`); `adjustVHTCapsForChains` only rewrites the two VHT MCS maps by chain count and never the width. Width travels in the channel sequence instead: TLV 18 entries are `{channel, operating class}` (encoding 3) and the class fixes the width (124/125 = 20 MHz, 126/127 = 40, 128 = 80), and `IO80211AWDLPeerManager::getSelfChannelSequenceMinBandwidth` / `IO80211AWDLPeer::channelSequenceMinimumBW` reduce a sequence to its minimum over the slots (flag bits `0x2` 20 MHz, `0x4` 40, `0x400` 80). Our `getVHT_CAPABILITY` returns a fixed 80 MHz, two-stream element (`ForbiddenItlwm/AirportItlwmSkywalkInterface.cpp`). The hypothesis these pointed at was that the listener is held to 20 MHz by the overlap with the station's channel while our TLV 18 announces class 128, so the iPhone sends 80 MHz frames we cannot decode. **It was tested and is wrong:** `ITLAWDLCapOwnTlv18` was `{149, 124}` (20 MHz) in all 16 slots as macOS built it, and `-itlawdlcoexclass20` rewrote nothing in 1,226 frames. What the same run did show (`ITLAWDLRxRatesTid7`, 80,063 TID 7 frames): every frame the iPhone sent us was HE at 20 MHz, 94% on one stream, mostly MCS 2 to 4, and the frame that follows a gap is at MCS 0 or 1, so the iPhone's rate control reacts to losses it never retransmits. Loss was 11.1% in that run (17.8% in the one before), with 0 bad CRCs. The pattern, loss that grows with MCS and almost no two-stream frames, is what a receiver on one chain looks like; `ITLAWDLPhyCmdInfo` records the PHY context command version, which builder took it and the idle/active RX chain counts the listener's context really got (4 entries of 8 bytes: id, version, path 0 plain / 1 v3 with the diversity promotion / 2 ultra-high-band, requested static and dynamic chains, effective idle and active, antennas). **Apple's recipe for the station while AWDL runs (asm, AppleBCMWLANCoreMac 24H232).** `AppleBCMWLANPowerManager::configurePowerSaveForAWDL(bool)` (0xffffff800157fc92, log lines `MIMO_PS: Enter/Leave`) calls `setBeaconListenInterval()` (the extended DTIM, from separate "real time extended DTIM" parameters kept next to the default ones), `configureEarlyBeaconTermination` (iovar `bcntrim`, 3 when enabled), `configureOpportunisticPowerSave` (`ops_cfg`, 0xf) and `configureDigitalNapPowerSave` (`nap_enable`). Every one of them reduces how often and how long the infrastructure station holds the radio. The driver had done the opposite: while AWDL data had been seen in the last two seconds (`awdlCoexContinuityActive()`) it switched the station's power save off at both the MAC and the device level, so the station was an always-active binding, and the registry showed `continuityPower=Yes` in every recent run. It now keeps power save on, sets `POWER_MANAGEMENT_ENA` (which is also what enables beacon abort; before, the flag was never set and beacon abort was always off) and `SKIP_OVER_DTIM` with `skip_dtim_periods` (see `itlawdlcoexskipdtim` in [BOOT-FLAGS.md](BOOT-FLAGS.md)). Not yet measured on hardware: read `ITLAWDLGapPhaseBins` and the loss against the earlier runs (12%, 63% of gaps 13 to 38 ms after a beacon).

**Gap phase against the access point's beacons.** `ITLAWDLGapPhaseBins` and `ITLAWDLGapPhaseMissing` (16 u32 each) count receive gaps, and the frames missing in them, by where they fall in the beacon interval: (firmware time of the frame after the gap, minus the firmware time of the last beacon seen from our access point) modulo the interval, in sixteenths. `ITLAWDLBeaconsSeen` and `ITLAWDLBeaconIntervalUS` say how many beacons that rests on. Gaps spread evenly across the bins are not tied to the beacons; most of them in one or two bins are, and then the firmware leaving the AWDL channel to hear the station's beacons is the cause. **Result on the test Mac (780 beacons, 662 gaps, 7,190 missing frames, 12% loss):** not uniform (chi-square 605 against 30.6 at p<0.01). 63% of the gaps and 59% of the missing frames end 12.8 to 38.4 ms after the last beacon, peaking at 19.2 to 25.6 ms (1,557 missing frames in that bin). So the radio leaves the AWDL channel at about each beacon and comes back 13 to 38 ms later, which is a long visit, not just the beacon. About 40% of the gaps are spread evenly across the interval and are not tied to the beacons: a second loss source. `ITLAWDLGapPhaseByClass`, `GapMissingByClass` and `BeaconClassCount` (3 x 16 u32, and 3 u32) split the same histogram by the class of the last beacon: 0 an ordinary beacon, 1 a DTIM beacon with nothing buffered, 2 a DTIM beacon announcing buffered group (multicast or broadcast) traffic, after which the access point sends it and a power-saving station stays awake for it. `ITLAWDLBeaconDtimPeriod` is the DTIM period.

**Checked and ruled out:** the firmware image's command-version table (`iwlwifi-cc-a0-68.ucode`, TLV 48, 177 entries, found by walking the TLV chain; the PHY context entry is `08 01 03 00` at file offset 0xb4) lists `PHY_CONTEXT_CMD` as version 3, so the 1, 1 request is promoted to two chains and the listener is on two. The same table has no entry for `LOW_LATENCY_CMD` (group 3, command 3): the firmware does not implement it.

**A command-ring wedge looked like a Block Ack problem.** With `-itlawdlcoexlowlat` on, the scorecard showed `BaPeerAdded=No`, `BaTids=0`, `BusySkips` in the hundreds of thousands and every host command returning `ENOSPC` (`0x1c`). The cause is in `iwx_send_cmd`: a command stays pending until the firmware completes it, the AX200 does not complete `LOW_LATENCY_CMD` (the sync wait ends in `EWOULDBLOCK`, errno 35, which was misread as a firmware refusal), and the AX200's command ring has only 32 slots, so after about 31 more commands the ring index wraps onto the dead slot and nothing can be sent again. The low-latency command is now never sent, a slot the firmware stays silent about for 3 s is reclaimed, and the registry counts it: `ITLAWDLCmdSent`, `Queued`, `QueuedPeak`, `Timeouts`, `LastTimeoutId`, `EnospcRing`, `EnospcSlot`, `EnospcResp`, `Reclaimed`. Healthy: `Timeouts` 0, the three `Enospc` counters 0, `Queued` a small number.

**What the iPhone and the Mac say in the Real Time Group TLV.** Apple's peer manager reads TLV 31 of the iPhone's action frames (`parseAWDLRealTimeGroupTLV`, asm in IO80211Family 24H232): bit 5 of payload byte 3 is the peer's "ultra low latency infra active" flag, which switches Apple's schedule to a state where AWDL follows the infrastructure channel. `ITLAWDLRtgPeerTlv` and `ITLAWDLRtgOwnTlv` hold the last TLV from the iPhone and from the frames macOS builds for us, and `ITLAWDLRtgPeerUll` / `RtgOwnUll` count how often the bit was set. Not yet read on hardware.

**Tried: the station without power save (`-itlawdlcoexcam`).** `ITLAWDLCoexCam` read Yes after the reboot and the loss did not improve: 4,106 missing of 42,388 (9.7%), 338 gaps, longest 443 frames, and the gap spacing was still about 100 and 300 TU. So it is not power save: the firmware visits the station's channel for the access point's beacons however the station is configured. The iPhone's real-time stream gets no retransmission (retries were only about 0.3% of frames), so any time the Mac is away from channel 149 is lost frames.

**Tried: same channel (AP on 149) without a working low-latency kick.** AWDL opened at 80 MHz (`OwnWidth` 80) and the ~100/300 TU gap cadence mostly went away, but TID-7 loss stayed high (~11–12% in a 60 s Photo Booth capture; Continuity media ~3.85% missing). Dual-channel hopping is real, but it is not the only loss.

**Tried: `-itlawdlcoexlowlat` (AX200).** A synchronous `LOW_LATENCY_CMD` timed out with `EWOULDBLOCK` (`ITLAWDLCoexLowLatencyResult` `0x80000023`) even though the boot-arg was Yes — the firmware does not answer that command. Sending it `IWX_CMD_ASYNC` makes the registry report `accepted ver=1`. On channel 161 (AWDL still 149 @ 20 MHz) a 60 s capture then showed window TID-7 miss ~8.8% and Continuity media ~3.41% missing (`RxFailed` 0, gaps still near 100 TU). Better than before, not watchable.

**Tried: same-channel 149 @ 80 MHz with working ASYNC lowlat.** Window TID-7 miss ~11.9%, Continuity media ~4.2% — **worse** than dual-channel+lowlat. Dual-channel hopping is not the main loss.

**Peer schedule (TLV 18) during Continuity:** `ITLAWDLCoexPeerSequence` was `[149,149,149,149,149,149,36,36,6,149,149,149,149,149,36,36]` — 11 of 16 slots on 149, the rest on **36** and **6**. `PeerPresentEntries` moves between ~2 (idle) and ~11 (busy).

**Measured: Seq gaps vs presence (`/var/tmp/cc-sched`, dual-channel 161/149, ASYNC lowlat accepted).** Window ~60 s Photo Booth: Seq miss **14.2%** (TID-7 **14.3%**), Continuity media ~**2.8%** missing, `RxFailed` 0. Of classified Seq missing: **present 86.8%**, away 6.7%, unknown 6.5%. Gaps still cluster near 100 TU (infra beacon hop). Away slots are not the story — frames vanish while the peer's schedule says it is on 149.

**BA during Continuity:** live registry showed `ITLAWDLCoexBaBroken` Yes, `BaPeerAdded` No, `BaTids` empty, while TID-7 traffic was flowing — a prior ADD_STA/REMOVE_STA failure had quarantined Block Ack for the whole firmware generation, so Continuity Camera ran without a TID-7 BA session. Clearing `BaFailedGen` on ADDBA (event `C`) was not enough: `BaStatus` was `0x8000001c` (`ENOSPC`) — the host command ring is full during Continuity bursts, `BaAddPeer` fails, and we re-quarantined in a loop (events `C` then `B`/`Q`/`N`). Fix: do not quarantine on transient `ENOSPC`/`EAGAIN`/`EBUSY`; put the ADDBA back for retry (event `N`).

**Measured with BA held (`/var/tmp/cc-ba`):** `BaBroken` No, `BaPeerAdded` Yes, `BaTids` 129 (TID 0+7), `FilterOff` Yes. Window TID-7 miss **11.7%** vs **14.3%** without stable BA (`cc-sched`) — real but modest. Present-side still ~92% of gaps; ~100 TU clustering remains (infra on 161, AWDL on 149 @ 20 MHz). Continuity media ~4.5% missing in that run.

**Beacon steal mitigation (in progress):** while Continuity data was heard in the last 2 s, refresh `LOW_LATENCY_CMD` every second and drop station + device power-save (same effect as `-itlawdlcoexcam`, but automatic for the stream). Registry: `ITLAWDLCoexContinuityPower`, `ITLAWDLCoexLowLatencyRefreshes`. Pure `-itlawdlcoexcam` alone did not remove the ~100/300 TU pattern; measure whether continuous lowlat + no-PS does with BA held.

**ENOSPC retry storm:** with ContinuityPower Yes / Refreshes climbing, BA stayed `PeerAdded` No because `BaAddPeer` hit `ENOSPC` every ~2 ms (193k `N` events) while lowlat refresh competed for the same host command ring. Fix: 100 ms backoff after transient ENOSPC (`BaBusySkips` / `BaBusyEvents`), throttle `N` events, and skip lowlat refresh during that backoff.

**Ported from AppleBCMWLANCoreMac (ASM): Continuity realtime → WME VO.** `setRealTimeMode` writes `wme_ac_sta` for VO; `setLLW_PARAMS` uses `WLC_SET_VAR llw`. On AX200, LLW registration arms realtime and AWDL/llw data TX uses `EDCA_AC_VO` (was hardcoded BE). Lowlat refresh waits for `BaPeerAdded` so Continuity ADD_STA is not ENOSPC-starved. Registry: `ITLAWDLCoexRealtime`, `TxAcVo`. Notes: [RE-continuity-realtime-vo.md](RE-continuity-realtime-vo.md).

**Ported from Apple IO80211Family (Sequoia Boot KC): Continuity FollowsInfra (gated).** Apple strings/ASM show `adaptCommonSocialChannelForUltraLowLatencyInfra`, `AwdlFollowsInfra`, and `setIsRTGUltraLowLatencyInfraActive`. A first Mac-only port that moved the listener onto infra (161) while the iPhone's AWDL sequence still named 149 left `MultiState=move-uncertain`, `MoveFailures≥1`, `DataRxUcast=0`, and Photo Booth stopped seeing the camera. Apple's ULL adapt is coordinated on both ends. The driver now follows infra only when `-itlawdlcoexfollowsinfra` is set, or when Continuity is warm **and** the phone's TLV-18 slots already name the station primary. A failed PHY move clears `ITLAWDLCoexFollowsInfra`.

What is not known about the UMI: `-itlawdlumiack` only posts the status locally. Apple's own sequence sends a real unicast action frame (the ioctl 149 template with option 1 in TLV 12) to peers that declare the 6E-capable bit; whether the iPhone needs to see it for the stream to be healthy is untested.

## 6. Log noise that is harmless

macOS's AWDL peer manager logs errors for calls this driver refuses. In the tests none of them stopped AWDL or a transfer:

| Message | What it is |
| --- | --- |
| `Failed to update awdl0 MAC address … Can't assign requested address` (wifip2pd) | macOS tries to rotate the AWDL address once per AWDL start; the driver refuses (`setMacAddress`). The address stays fixed |
| `programChipWithNewMaster: FAIL … AWDL can't send AWDL_ADVERTISERS` (kernel, many times) | The driver doesn't implement that call. In a decompile of `IO80211AWDLPeerManager::programChipWithNewMaster` (24H232) a failure only logs and skips a bookkeeping update; it doesn't return early |
| `setIOCTL ERROR … APPLE80211_IOC_P2P_COEX_PROTECTION`, `Unable to get SIB coex status`, `invalid card capabilities`, `aw start event missing` | More unsupported calls and events the driver doesn't produce |

## 7. Pitfalls found the hard way

- **zsh doesn't split an unquoted `$LIST`**, so `for S in $LIST` loops once with the whole string. Use `/bin/bash` for loops like the ping ladder (the script already starts with `#!/bin/bash`).
- **A background shell job ignores SIGINT**, and `dtrace` doesn't install its handler when SIGINT is ignored, so `kill -INT` never flushes it. Run `dtrace` in the foreground and press Ctrl-C.
- **`dtrace -o` output stays buffered** until the program exits. An empty file from a daemon that is still running isn't proof of anything.
- **A boot-time trace is too late for `awdl0`'s creation**, which happens about 4.6 s after boot, before a LaunchDaemon's `dtrace` has armed. Trace at runtime instead (the failures in both cases above can be reproduced at runtime).
- **A capture above the driver can't see rejected packets**, and a clean driver counter doesn't prove the link carried the data: check the transport and the radio-level counters (`UcastFailed`, `BaTxAcked`) too.
- **Don't trust one pseudocode listing over the disassembly.** When reading Apple's kernel code with a decompiler (REA with Hopper), one function came out with its conditions inverted relative to the raw `otool` disassembly. Hopper also can't load a kernel collection (`BootKernelExtensions.kc`) directly; extract one entry first.
- **`/tmp` is wiped at a reboot**, scratch scripts included. Keep what you need in the repository (`tools/diag/`).
