# Wi-Fi association speed

How long the AX200 takes to scan and join, how to measure it, and what the driver does about it.

## Baseline (2026-11-21, ForbiddenItlwm 0.6.0, macOS 15.8.2, AX200)

Two boots, measured from the unified log (airportd auto-join, `Apple80211Scan`, configd DHCP), seconds after `AirportItlwm::init`:

| Step | Boot 1 | Boot 2 |
| --- | --- | --- |
| airportd starts auto-join | about 4.0 | 4.4 |
| scans (three passes, each over 3 to 38 channels) | 3.5 / 3.8 / 2.4 s | 3.5 / 3.8 / 2.4 s |
| "Found matching network" | 19.4 | 19.3 |
| link up (DHCP `INIT/REBOOT` on en1) | about 23.5 | 23.4 |
| link drops and the cycle repeats | not checked | DHCP `BOUND` at 50.5 |

Scan requests that reach the driver before it is ready are answered `0xe00002d8` (NotReady) or `-3903`; the registry counts them in
`ItlwmScanStats` (`notready=`).

## What the join did

`setWCL_ASSOCIATE` (`startWCLJoin` in `ForbiddenItlwm/WCL/WCL.cpp`) used to put net80211 in the SCAN state and scan **every** channel before
authenticating, even though macOS had just scanned and named the BSSID. `ieee80211_end_scan` then chose the BSS and joined.

Now `startWCLJoin` looks the BSSID up in the node cache (`ieee80211_find_node`); if it is there with the same SSID and was heard less than 20 s
earlier, it sets `ic_wcl_join_channel`. `iwx_umac_scan_fill_channels` then adds only that channel to the scan (a directed probe, about 10 TU of
dwell on a clear channel, or one passive dwell on a DFS channel). If the BSS is not cached, or the scan finds nothing, `ieee80211_end_scan` clears
the channel and the next scan covers everything, so the worst case is the old behaviour. `-itlnofastjoin` disables it.

## Measuring

The driver keeps the last 96 steps in `ItlwmWCLTimeline`; read them after a join:

```
python3 -I tools/diag/assoc_timeline.py
```

| Step | Meaning |
| --- | --- |
| scan requested / accepted / refused | macOS's scan request and the driver's answer (refusal code in `val`) |
| scan done event | net80211 finished the scan (`arg` is its state) |
| scan finished | the scan result was reported to macOS |
| join requested / started / refused | the association request |
| join channel | the channel chosen for the one-channel scan, 0 for all channels |
| association done | authentication and association completed (`val` is the 802.11 status) |
| first beacon, link up, link down, deauth | the rest of the join and any drop (`val` is the reason) |
| join finished | `arg` is the status macOS was given |
| driver ready / stopped | firmware initialisation done or torn down |

## What the first timeline showed (2026-11-21, `assoc1`)

```
0      driver ready        453   scan requested        4003  scan done (3550 ms later)
4015   scan requested      4120  scan done (105 ms)    4189  scan requested -> done 7923 (3734 ms)
11724  scan done           12455 scan requested -> done 14861 (2406 ms)
14944  join requested; driver stopped, ready again at 15131; join channel 0
18934  scan done (3803 ms)  18968 association done (+34 ms)  18989 first beacon  19003 link up
```

- Authentication, association and the first beacon take about 70 ms. Nearly everything else is scanning.
- A scan request waited 2.4 to 3.8 s because net80211 never stopped scanning: whenever a scan found nothing joinable,
  `ieee80211_end_scan` started another full scan of every channel (3.8 s each), and a WCL scan or join request only attached to the scan
  that happened to be running. The done events at 4003, 7923, 11724, 14861 and 18934 are that loop.
- The join restarted the firmware (the private-MAC change: "driver stopped" then "driver ready" 187 ms later). That cleared the scan cache, so the
  join found no cached BSS and scanned all channels (`join channel 0`).

## Changes after the timeline (`assoc2`)

- The SCAN state is held idle (`ic_wcl_scan_idle`) when neither a WCL scan nor a join asked for a scan; `iwx_newstate_task` no longer starts a scan
  then. A WCL scan or join request starts one from the idle state, and a finished WCL scan clears `ic_wcl_scan_requested` so net80211's follow-up
  iteration stays idle.
- A deferred join reads its BSS's channel from the cache before the firmware restart (`wclJoinChannelHint`) and uses it afterwards.

## Second timeline (`assoc2`)

- A scan restricted to a few channels now finishes in about 0.12 s (326 to 444 ms, 4203 to 4320 ms), against 2.4 to 3.8 s before the idle SCAN state.
- A scan of every channel still takes 3.7 to 4.4 s (454 to 4139, 7940 to 12350): thirty-odd channels at the firmware's dwell times, with the passive dwell
  (110 TU) on the DFS ones. macOS asks for these itself; the new `scan parameters` step records the type and channel count of each request.
- The join still scanned every channel (`join channel 0`, 3.8 s) because the "driver stopped" at 12454 came a millisecond *before* the join request:
  the firmware restart had already emptied net80211's node cache, so the hint taken at the join request found nothing. `assoc3` keeps its own record of
  every BSS a scan reported (`wclSeen`, 64 entries, 30 s) and reads the join's channel from that.
- Authentication, association and link up again took about 70 ms.
- While associated, scans (net80211 state 4) take 0.4 to 1.6 s each; airportd issues them back to back.

## Third timeline (`assoc3`): link up in 9.9 s

```
410  scan requested (type 2 passive, 3 channels)  -> done 758 (348 ms), 5 beacons heard
770  scan requested (1 channel)                   -> done 887 (117 ms)
5089 scan requested (type 2 passive, 38 channels) -> done 9500 (4411 ms), 11 beacons heard
9552 join requested, join channel 161             -> scan done 9863 (120 ms) -> association 9896 -> link up 9928
```

- Link up came 23.4 s after driver init at the baseline, 19.0 s with `assoc1`, 16.5 s with `assoc2`, and 9.9 s with `assoc3`. The join's one-channel scan now
  takes 120 ms.
- airportd's scans are type 2, passive. A passive dwell is 110 TU per channel: 38 channels took 4.4 s, 3 channels 0.35 s.
- The 4.2 s gap before the broad scan (888 to 5089) is airportd's. The quick 3-channel scan heard 5 beacons but not the saved network, so airportd waited and
  then scanned everything. A single passive dwell (about the beacon interval) can miss an access point's beacon.

## Probing on passive scans (`assoc4`)

For a type 2 scan the driver now sends a wildcard probe request on every channel that allows probing (`AirportWCL::probeChannel`: not DFS, not passive-only)
and drops the forced-passive flag, so access points answer at once and the adaptive dwell is 10 to 30 TU instead of 110. DFS channels stay passive.
`-itlstrictpassive` turns it off. The expected effect is that the quick scan finds the saved network, so airportd joins without the broad scan and its pause
(about 5.5 s link-up), and any broad scan takes about 2 s instead of 4.4 s.

## Open

- Whether the quick scan now finds the saved network; the beacon count in the timeline shows what each scan heard.
- The remaining DFS channels (about 15 of 38) still cost 110 TU each in a broad scan.
