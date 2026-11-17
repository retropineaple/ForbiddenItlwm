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

**The fix** is outside the driver: lower the wired interface to 1484 (`sudo networksetup -setMTU Ethernet 1484`). **Verified:** a 2,882,165,196-byte send over AWDL completed at 34 MB/s with no rejections. Two attempts right after the change still failed on stale 1500-sized connection state; a fresh connection was fine. [TROUBLESHOOTING.md](TROUBLESHOOTING.md#big-airdrop-sends-die-after-a-couple-of-seconds-unexpected-40) has the user-facing steps.

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

**6. The stream runs, but the video breaks up (current state).** With stages 0 to 5 in place the iPhone's camera stream is delivered to `llw0` (about 40,000 frames and 55 MB in the first minute, `RxFailed` 0). `ContinuityCaptureAgent` then logs `VideoPacketBuffer ... Error while detecting missing packets missingMediaPacketsCount=8 mediaCount=25 parityCount=0` and `Failed to parse reconstructed packet`: a good part of each video frame's packets are missing, and there is no error correction (`parityCount=0`). Where the loss is not: the driver handed on every frame it heard (`DataRxHandedOff` equals `DataRx`), 0 were rejected, `ItlwmLLWRxDelivered` equals `RxWanted`, and the kernel's UDP and IPv6 counters show about 12 drops from full socket buffers in 52,000 datagrams. Where it is: the sequence-gap counters say about 7.5% of the iPhone's unicast frames were never heard, in 342 bursts over the run (longest 100 frames). What decides the next step is what the new counters show (a run with the camera streaming): many bad CRCs (`ITLAWDLRxMpduBadCrc`, in runs) mean radio corruption; few CRC errors but gaps means the frames never reach us and the gap timestamps (`SeqGapRing`) should be compared with the AWDL schedule; `SeqLate` close to `SeqMissing` means reordering. Not yet known which.

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
