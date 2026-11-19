# Boot flags

AirDrop is off unless its flags are set, so with none of them ForbiddenItlwm behaves like AirportItlwm. The beta needs a long list of flags. They are the exact set used for every tested result. The Tahoe, Sequoia and Sonoma builds use the same flags. Simpler defaults are planned.

## The tested set

Add all of these to `boot-args`, on one line, separated by spaces:

```
-btleadv -itlawdlcoexprobe -itlawdliface -itlawdlnativeup -itlawdlcoexrx -itlawdlcoextx -itlawdlcoexmulti -itlawdlcoexdata -itlawdlmitx -itlawdlelicit -itlawdlcaps -itlawdlsvcmerge -itlawdlawsync itlawdlintcoal=4 -itlawdlcoexbatx -itlawdlcoexpeerpause -itlawdlcoexbatrace itlawdltxqueue=4096
```

What each one does:

| Flag | Effect |
| --- | --- |
| `-btleadv` | Turns BTLEAdvFixup on, so `bluetoothd` can start Bluetooth LE advertising on the Intel controller. `-btleadvoff` turns it off again |
| `-itlawdlcoexprobe` | Adds a second firmware context, the AWDL listener, beside the normal Wi-Fi station |
| `-itlawdliface` | Tells macOS the card can do AWDL and creates the `awdl0` interface |
| `-itlawdlnativeup` | Accepts the requests that let macOS's AWDL peer manager start |
| `-itlawdlcoexrx` | Hands the AWDL frames the listener hears to macOS |
| `-itlawdlcoextx` | Gives the listener its own firmware station and queue, and sends macOS's AWDL sync frames from it |
| `-itlawdlcoexmulti` | Puts the listener on its own channel: the one the iPhone uses for AirDrop (149, or 44 from 0.6.0), at 80 MHz when allowed, else 6. The firmware time-shares the radio between it and the station |
| `-itlawdlcoexdata` | Carries AirDrop's data through the listener, both ways |
| `-itlawdlmitx` | Sends macOS's own AWDL announcement (its name and services) in the sync frames. Only on verified macOS builds: `ItlwmAWDLMiState` says `on`, `off` or `blocked: build not verified` |
| `-itlawdlelicit` | Adds the extra AWDL fields Apple devices expect: election parameters, channel sequence and HT capabilities |
| `-itlawdlcaps` | Reports two spatial streams and VHT to macOS and to peers, so the iPhone uses 80 MHz two-stream rates |
| `-itlawdlsvcmerge` | Merges macOS's service records into the announcement frames, which helps discovery |
| `-itlawdlawsync` | Times idle sync frames to the iPhone's availability windows |
| `itlawdlintcoal=4` | Shorter interrupt coalescing for the listener, for lower latency |
| `-itlawdlcoexbatx` | Outbound Block Ack and firmware aggregation. This is what makes Mac → iPhone fast |
| `-itlawdlcoexpeerpause` | Pauses the iPhone's firmware queue while its schedule has it away, so frames aren't wasted |
| `-itlawdlcoexbatrace` | Records Block Ack events in the I/O Registry for bug reports. Diagnostic only, cheap |
| `itlawdltxqueue=4096` | Size of the driver's own transmit queue for `awdl0` |

## Optional

| Flag | Effect |
| --- | --- |
| `itlawdltxpool=N` | Size of the `awdl0` transmit packet pool, 896 to 8192. Default 2048. Below that, long Mac → iPhone transfers can stall |
| `-itlawdlanybuild` | Starts AirDrop on a macOS build the driver hasn't verified. **Risk of a kernel panic.** Only for testing a new build, from a USB stick. It doesn't override the check of Apple's AWDL function table |
| `itlawdlbtcoex=1` | Firmware Bluetooth coexistence mode 1. Fixed choppy Bluetooth audio during AirDrop on an older test build. Before 0.5.1 it only took effect in the monitor test mode |
| `-itllog` | From 0.6.0: keeps the driver's own log and publishes it as `ITLAWDLLog`, so a failed wake or a firmware restart can be read back afterwards. It's hex: `ioreg -l -w0 \| grep '"ITLAWDLLog"' \| sed 's/.*<//; s/>.*//' \| xxd -r -p`. **It can hold network names and addresses**: check it before posting. While it is set, any app on this Mac can read the log. From 0.6.0-beta.1 the driver no longer writes network names or EAPOL handshakes into it, and Wi-Fi addresses (MAC, BSSID) appear only as their last two bytes. Turn it off once you have what you need |
| `-itlawdlsamples` | Publishes raw frame samples in the I/O Registry for debugging. **They can hold device names and addresses.** Never share them publicly, and don't leave the flag on |
| `-itlawdlpmdebug` | Turns on all of macOS's AWDL peer-manager debug flags once AWDL is enabled (`ItlwmAWDLDebugFlagsSet` becomes Yes), so the kernel log says why peers are accepted or rejected. Very verbose in the unified log: for debugging and bug reports, not to leave on |
| `-itlawdlumiack` | Experimental, for Continuity Camera and Sidecar. Those register real-time traffic with the iPhone, and macOS then waits 3 s for a status message ("the peer acknowledged our UMI") that this driver's radio path never produces, so the registration failed with an I/O error. With the flag the driver posts that status itself, a moment after it answers the peer-cache request and the low-latency parameters that start each registration (`ItlwmAWDLUmiAckPosted` counts them). Nothing is sent over the air, so it only moves the registration forward. It does not make the camera's video work by itself: see [DEBUGGING.md](DEBUGGING.md#case-3-continuity-camera-gets-part-of-the-way). Leave it off unless you are testing Continuity Camera |
| `-itlawdlpeeralive` | Experimental, Sequoia 15.x only (IO80211Family 24H232 symbols; a no-op on other builds). During a Continuity Camera session the family dropped the iPhone from its peer table every 6 to 11 s (`IO80211AWDLPeer::shouldRemovePeerFromNDP`: no action frame for 2 s and no unicast activity for 6 s), which cleared the camera's traffic registration, ended the real-time group and stalled the video until wifip2pd registered again. The camera's video arrives on llw0, which doesn't update the family's per-peer unicast time. With the flag, each unicast frame to this Mac from a peer sets that time (`IO80211Peer::setLastRxUnicastLinkActivity`, a store of `mach_absolute_time`) and the peer's last-sync time (`IO80211AWDLPeer::setLastSyncReceived`), at most every 250 ms per peer, from inside the work loop gate. `ItlwmAWDLPeerAliveCalls` counts the updates; `ItlwmAWDLPeerAliveNoManager` / `ItlwmAWDLPeerAliveNoPeer` appear if the family's objects couldn't be found. Not yet measured on hardware |
| `-itlawdlrtgkeep` | Experimental, needs `-itlawdlpeeralive`, Sequoia 15.x (24H232 offsets). The family removes an active real-time-group peer that sends no RTG TLV (type 31) for 5 s (`IO80211AWDLPeer::actionFrameReport`: "Active RTG Peer has no RTG TLV for 5 Seconds"). The iPhone's action frames that this driver hears never carry one (`ITLAWDLRtgPeerSeen` 0 of 752), so the camera's group ended every 6 to 11 s even with the unicast and sync times kept fresh (the log shows the 5-second message just before each "Aged out"). With the flag the keep-alive also refreshes the stamp the family compares, the `mach_absolute_time` at peer ivars +0x6a0, but only when it is already set and under 2 minutes old. `ItlwmAWDLRtgKeepRefreshed` / `ItlwmAWDLRtgKeepSkipped` count them. Not yet measured on hardware |
| `-itlawdlndpkeep` | Experimental, Sequoia 15.x only. 1.5 s after AWDL is enabled, raises the family's own peer limit (`ndpRemovalTimeoutmSec`) to 100 s by calling `IO80211AWDLPeerManager::setAwdlCtl` with the request the family's ioctl wrapper builds (0x3B8C0-byte heap buffer, tail fields filled in). `ItlwmAWDLNdpKeepResult` is the return code and `ItlwmAWDLNdpKeep` says `applied`, `no-peer-manager` or `no-memory`. The ioctl route (`tools/diag/awdl_ctl.py`) is refused with errno 102 on this macOS, which is why this is done inside the kext. Try `-itlawdlpeeralive` alone first, then both. Not yet measured on hardware |
| `-itlawdlllw` | Sequoia kext only. Creates `llw0`, the low latency interface macOS asks for next to `awdl0` (wifip2pd's request 293, "Failed to create the companion low latency interface" in the log). Continuity Camera's media connection is opened as low latency over it. Built on the Wi-Fi interface's class chain with `awdl0` as its parent, like Apple's. The radio has no low latency mode, so `llw0` shares `awdl0`'s radio path, but the frames differ the way Apple's do: what `llw0` sends goes out as plain RFC 1042 (an empty-OUI SNAP header, `aa aa 03 00 00 00 86 dd`, then IPv6), and plain RFC 1042 frames received from the iPhone are delivered to `llw0` and never to `awdl0`. `awdl0`'s own frames keep the Apple AWDL header. Needs `-itlawdlcoexdata`, which the usual flag set already has. With it, the iPhone camera stream arrives (tens of thousands of frames per minute on the test Mac); see [DEBUGGING.md Case 3](DEBUGGING.md#case-3-continuity-camera-gets-part-of-the-way) for what is still lost. `ItlwmLLW*` registry keys show its stage and counters |
| `-itlawdlcoexclass20` | Experimental. Rewrites the operating class of every served slot in the channel-sequence TLV (TLV 18) of the action frames we send to the 20 MHz class of its channel, but only while the listener runs at 20 MHz. macOS builds that TLV from the driver's channel data, and the class in each slot says how wide the peer may transmit to us: Apple's `IO80211AWDLPeer::channelSequenceMinimumBW` takes the minimum over the slots (class 124/125 = 20 MHz, 126/127 = 40, 128 = 80, 129 = 160). The listener is held to 20 MHz when the Wi-Fi station's channel lies in the same 80 MHz block (channel 161 against AWDL's 149), so announcing 80 MHz tells the iPhone it may send frames the listener cannot decode. Registry: `ITLAWDLCoexClassMatch`, `ClassFrames`, `ClassRewrites`, and `ITLAWDLCapOwnTlv18` (as macOS built it) against `ITLAWDLCapOwnSentTlv18` (as sent). **Tested on the test Mac: it changes nothing.** macOS already announced class 124 (20 MHz) in every slot (`ClassRewrites` 0 after 1,226 frames), so the width we advertise already matched the listener. Kept only for repeating the check |
| `itlawdlmtu=N` | `awdl0`'s MTU, 1280 to 1500 (Sequoia kext). The default is 1500, which is what the family gives Apple's own Broadcom controller and what a wired interface has, so AirDrop's QUIC is never refused on `awdl0` ("Message too long", `unexpected-40`). The family gives every other controller 1484; `itlawdlmtu=1484` brings that back. See [DEBUGGING.md Case 2](DEBUGGING.md#case-2-big-sends-die-with-unexpected-40) |
| `itlawdlcoexskipdtim=N` | While AWDL data is moving (the Continuity Camera stream, and AirDrop transfers), the Wi-Fi station skips N DTIM periods between wake-ups (0 to 10, default 2: it wakes for every third DTIM beacon) and runs with beacon abort on; outside that it is unchanged. Intel's counterpart of what Apple's `AppleBCMWLANPowerManager::configurePowerSaveForAWDL` does on a real Mac: extend the beacon listen interval (`bcn_li_dtim`) and turn on early beacon termination (`bcntrim` 3). 0 keeps power save and beacon abort but skips no DTIMs. The cost is that downlink traffic on the Wi-Fi station can wait up to the wake interval (about 0.9 s on a DTIM-3 network) while AWDL data is moving. Registry: `ITLAWDLCoexSkipDtim`, `ITLAWDLPmSent`, `ITLAWDLPmFlags` (bit 0 power save, 1 power management and beacon abort, 2 skip over DTIM), `ITLAWDLPmSkip`, `ITLAWDLPmErr` |
| `-itlawdlcoexcam` | Experimental. Runs the Wi-Fi station without power save (constantly awake), on the theory that power save made the firmware leave the AWDL channel at every beacon of an access point on another channel (the Mac on 161, AWDL on 149, about 8% of the iPhone's camera frames lost in bursts about every 100 ms). Takes effect at the next Wi-Fi association; costs power. **Tried on the test Mac: it did not help** (9.7% of frames missing with it on, against 8.7% without, same 100 and 300 TU gap pattern; the firmware hears the beacons whether or not the station sleeps). Kept only so the test can be repeated. `ITLAWDLCoexCam` in the registry shows it |
| `-itlawdlcoexfollowsinfra` | Experimental. Forces Apple-style FollowsInfra: prefer the Mac's own usable 5 GHz infra channel. Leave off unless you are A/B testing and the iPhone is already on that channel — a Mac-only move away from the phone's AWDL social channel (149) can wedge the listener (`move-uncertain`) and stop Continuity Camera. Without the flag, follow only happens when Continuity is warm and the phone's schedule already names the infra channel. See [DEBUGGING.md Case 3](DEBUGGING.md#case-3-continuity-camera-gets-part-of-the-way) |
| `itlawdlllwrx=0` or `1` | With `-itlawdlllw`: which frames with the *Apple* AWDL header go to `llw0`. 0 (default) delivers none and only counts the destinations (`ItlwmLLWRxDestinationsSeen`); 1 delivers those whose IPv6 destination is a unique local address (`fd00::/8`) to `llw0` instead of `awdl0`. It does not matter for Continuity Camera: the iPhone's camera frames carry a plain RFC 1042 header, and those always go to `llw0`, whatever this says |
| `itlawdlllwmtu=N` | With `-itlawdlllw`: `llw0`'s MTU, 1280 to 1500. The default is 1484, `awdl0`'s, because everything `llw0` sends goes out through `awdl0`'s radio path and nothing has shown that the iPhone accepts AWDL data frames carrying more than 1484 bytes of IP. The family's own default is 1500, which is what Apple's hardware uses; set `itlawdlllwmtu=1500` to test it. A MTU the path can't carry makes connections stall without an error, as the AirDrop `unexpected-40` case did (see [DEBUGGING.md](DEBUGGING.md)) |

## Comparison flags

These restore earlier behaviour, for A/B testing only. Leave them off.

| Flag | Effect |
| --- | --- |
| `-itlawdlcoexnogate` | Turns off the send gate, which holds frames for the iPhone while its schedule has it away |
| `-itlawdlcoexcalwait` | Waits for a 300-frame calibration of the iPhone's schedule instead of assuming it straight away |
| `-itlawdlsameband` | Test (0.5.2): while the Mac's Wi-Fi is on 2.4 GHz, keep the listener on channel 6 instead of 149, so the radio doesn't time-share across bands. For setups where Wi-Fi keeps dropping with `-itlawdlcoexmulti` (#16) |
| `-itlawdlnosamechprobe` | Turns off the same-channel fix for Mac → iPhone block ack ([PR #47](https://github.com/retropineaple/ForbiddenItlwm/pull/47)), on by default from 0.6.0. The old opt-in flag `-itlawdlcoexsamechprobe` is still accepted and changes nothing |
| `-itlawdlfilterbysession` | Keeps the listener's promiscuous receive off only during a receive block-ack session, as before 0.6.0 (PR #57). From 0.6.0 it stays off while the iPhone's firmware station exists, which fixed stalled sends after the iPhone was idle |
| `-itlawdlnoirconcurrent` | Doesn't use passive channels next to the Mac's own Wi-Fi (0.6.0's rule, taken from Linux's rule for Wi-Fi Direct interfaces) |
| `-itlawdloverlap80` | Uses 80 MHz on the listener's channel even when the Mac's Wi-Fi uses the same 80 MHz block with another main channel, as before 0.6.0 (#26) |
| `-itlawdlnosharedba` | Turns off block ack when the listener shares your Wi-Fi's channel (without `-itlawdlcoexmulti`), as before 0.5.4 |
| `itlawdlpooltype=N` | AirDrop packet pool type: 0 (quantum, Tahoe's default) or 1 (packet, the default on Sequoia and Sonoma from 0.5.1). Sequoia doesn't create `awdl0` with 0 |
| `-itlawdlcoexnoorder` | Lets short frames overtake a paused aggregate queue, as older builds did |
| `-itlawdlcoexbarefill`, `-itlawdlcoexawhold`, `-itlawdlcoexbafilterhyst`, `-itlawdlcoexawsync0` | Older experiments. No gain was measured |

## Diagnostics and upstream flags

Not needed for AirDrop. Listed so you know what they do if you find them in a guide.

| Flag | Effect |
| --- | --- |
| `-itlmccprobe` | Records what the laptop's ACPI methods report about the Wi-Fi country, in the I/O Registry. Diagnostic only |
| `-itlawdlpmdebug` | Asks macOS's AWDL peer manager for its debug flags. Diagnostic only |
| `-itlawdldatacopy` | Publishes `ItlwmAWDLDataCopyAttempts`/`Success`, a receive-copy probe from the bring-up. Diagnostic only |
| `-itlawdlpoolprobe` | Probes the `awdl0` packet pools once at start and publishes the result. Diagnostic only |
| `-btlenoscan` | BTLEAdvFixup: advertising fixes only, without the scanning changes. For comparison |
| `-btleadvdbg`, `-btleadvbeta` | Lilu's standard debug and beta switches for BTLEAdvFixup |
| `itlwm_he=1` | Wi-Fi 6 (802.11ax), from the upstream driver. Untested with AirDrop; speeds vary (#58) |
| `-novht`, `-noht40` | Upstream: no 802.11ac, or no 40 MHz on the Mac's own Wi-Fi |

## Don't use

- **`-itlawdlbaaskshared`** and **`-itlawdlhideinfra`** (0.5.3 test flags) were removed in 0.6.0 and are ignored. Neither helped.
- **`-itlawdlpeercctest`** (dedicated mode only): passes the country a nearby device states in its AWDL frames to the firmware. That device isn't authenticated, so this lets anyone nearby choose the radio's country rules. Development only.
- **`-itlawdlcoexlowlat`** is ignored: the low-latency command is never sent. The AX200's firmware does not complete `LOW_LATENCY_CMD` (the driver's 1 s wait ends in `EWOULDBLOCK`, errno 35; this was first read as a refusal and it is not one). A command the firmware never completes kept its slot in the host command ring, which has 32 entries on an AX200 (128 on AX210 and newer). Once the ring index wrapped back onto that slot every later command returned `ENOSPC` for good: no peer station, no Block Ack, no TID 7. That is also what "took Wi-Fi down" on the AX211. See [DEBUGGING.md Case 3, stage 6](DEBUGGING.md#case-3-continuity-camera-gets-part-of-the-way).
- **The dedicated-mode test flags:** `-itlawdlrx` and the flags that build on it (`-itlawdlqueue`, `-itlawdl6`, `-itlawdloneshot`, `-itlawdlsyncprobe` and the other train and probe flags, `-itlawdlch149`, `itlawdlcc=`). They put the card in monitor mode, so normal Wi-Fi is down. They are kept for development.
- **Country overrides.** Nothing here changes the country's rules, and nothing should. (From 0.5.0 the driver uses channels the firmware marks passive once it has heard a network there; Linux does this only before a country is known. That needs no flag.) `itlwm_cc` only changes the code macOS displays. It doesn't open channel 149.
