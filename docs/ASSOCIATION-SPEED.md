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

## Open

- The three 2.4 to 3.8 s airportd scans are not explained by the firmware's dwell times (three channels at 110 TU is about 0.35 s). The timeline
  separates the time spent before the scan reaches the firmware from the time spent after the scan ends.
- The link dropped about 4 s after the first association on the second boot (BSSID changed at 27 s). The deauth entry in the timeline gives the reason.
