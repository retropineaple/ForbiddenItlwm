# AX211 same-channel A/B/A replication — 2026-11-15

Follow-up to [maintainer review](https://github.com/retropineaple/ForbiddenItlwm/pull/47#issuecomment-6883032139).
This is a new ordered A1 → B → A2 sequence, separate from the earlier single A/B pair.
It measures this host; it is not a discovery fix or a general throughput guarantee.

## Configuration and method

- AX211 (8086:7A70), macOS 26.7.3 / 25G342. Actual infrastructure channel and AWDL listener both verified as **149 / 80 MHz** after each test boot.
- A1 and A2: self-built upstream beta.2, `69e88fa0fdb0ef7122f2aee81111651c32260a62`, loaded UUID `3D378C4F-DD82-306F-ADF1-3DD77D5A2F2F`.
- B: `0affe073f549b7d35b80a7b286ace6eb6999d71a`, loaded UUID `A57DCB05-511F-31A0-A61A-3AFFC9B5DA5E`, with `-itlawdlcoexsamechprobe`. The include/comment cleanup produces the same signed Kext bundle bytes as the earlier tested B.
- The other three split feature flags (control RX, single legacy rate, peer handoff) were absent in **all test builds**. Bluetooth and unrelated EFI files were unchanged. No BPF capture or sysctl writes were used.
- Same Mac → iPhone file: 247,636,698 bytes, SHA256 `a31d673c39851f0f6eb3fa53f2570cd06182b402d414e0d5716fab8b96296159`. User confirmed each listed transfer completed on the receiver; no receiver-side hash was obtained.
- A manual reboot separated each build. The phone was connected to the same AP before file transfers and instructed to stay awake/unlocked. The reference Mac's Wi-Fi was explicitly off for the measured B and A2 runs; for A1 it was initially reported off, but later clarification established that it had only been lid-closed. Its actual radio state during A1 is therefore uncertain.
- Gateway ping was bound to the Wi-Fi interface and its DHCP source address, with a configured interval of 0.1 s. Interface counters and driver event buffers were sampled alongside sharingd phase logs. Event and ping-reader timestamps share Mach absolute uptime; Python's process-relative monotonic clock was not used to correlate processes.
- Two early B starts were rejected because agreement had already completed before ping/capture. Neither supplies ping-covered negotiation evidence or file results. A temporary one-shot boot sampler, running as the ordinary user, then began at boot uptime **29.188 s**, BA `off`, probe opens 0, no retained events. It captured the sequence below and both B transfers, then exited. The temporary launchd task was removed before A2.

## File results

Decimal MB/s = file bytes / sharingd UPLOAD-phase duration / 1,000,000. ASK is reported separately.
Counter deltas bracket each UPLOAD phase and include protocol/background traffic.

| Stage/run | UPLOAD s | MB/s | ASK s | AWDL TX bytes | Ordinary LAN TX bytes (en0+en1) | BA state during upload | BA TX frames / notifications delta |
|---|---:|---:|---:|---:|---:|---|---:|
| A1 / 1 | 72.251 | 3.427 | 26.215 | 264,507,331 | 9,024,113 | asking | 0 / 0 |
| A1 / 2, awake repeat | 69.120 | 3.583 | 5.501 | 264,586,389 | 13,504,925 | asking | 0 / 0 |
| B / 1 | 9.127 | 27.132 | 4.594 | 264,170,962 | 313,132 | agreed | 163,055 / 4,075 |
| B / 2 | 7.464 | 33.177 | 1.389 | 264,210,018 | 357,694 | agreed | 170,970 / 4,087 |
| A2 / 1 | 81.368 | 3.043 | 6.225 | 264,049,132 | 241,203 | asking | 0 / 0 |
| A2 / 2 | 65.265 | 3.794 | 1.853 | 264,162,379 | 314,020 | asking | 0 / 0 |

The per-run arithmetic means are A1 **3.505**, B **30.155**, A2 **3.419 MB/s**.
Both baseline stages stayed `asking` with no received ADDBA responses or aggregated TX; B established BA and used aggregate TX. Full-file-sized AWDL deltas and much smaller LAN deltas support AWDL delivery in all six files. This ordered reversal supports an effect of the isolated change on this host, with variable radio/application timing; the stages were not randomized.

An additional A1 file completed in 70.031 s (3.536 MB/s), but the user reported the phone briefly slept and its progress UI disappeared/reappeared. It is preserved separately and **excluded** from the six standard runs and means. A1 used three sampling segments with gaps; the two listed A1 files were fully covered, but the repeated files are warm repetitions on the same boot, not independent cold negotiations.

## Retained B negotiation during ping

Both first64 and rolling-newest128 event buffers were retained. The accepted B capture has no event-index gaps or conflicting rows.

```text
index  uptime_ms  event  a(token)  b(status/asks)  c(flags)  d(window)  id
11     43026      P      91        3               0         0          0
15     43067      R      91        0               7         64         0
```

The same-token request → successful immediate matched ADDBA response took **41 ms**. Flags 7 include matched, immediate, and AWDL-BSSID bits. Both occurred after the collector/ping started. Probe counters ended at opens/closes **1/1**, max/last duration **47,895 µs**, restore failures **0**, live **false**.

**Peer-identity limitation:** the user reported the test phone and reference Mac Wi-Fi were off at this boot-time negotiation. The capture does not identify the responding peer, so this sequence is not asserted to be the later test iPhone's negotiation. It is one ping-covered B negotiation, not repeated cold negotiations for each file. The two rejected B starts also retained successful P/R sequences, but those preceded ping and are not used for window-impact evidence.

## Wi-Fi gateway ping

Counts are from final ping summaries; exact missing counts are shown because ping rounded some small loss percentages to `0.0%`. Statistics cover whole sampling segments, including idle time, not just UPLOAD phases.

| Capture | Sent / received | Missing | RTT min / median / max ms |
|---|---:|---:|---:|
| A1 first file + initial idle | 5,879 / 5,878 | 1 | 1.598 / 16.761 / 202.621 |
| A1 additional phone-sleep file, excluded | 1,556 / 1,555 | 1 | 1.615 / 14.541 / 173.330 |
| A1 awake repeat | 2,993 / 2,992 | 1 | 1.403 / 17.344 / 150.543 |
| B, negotiation + both files | 2,347 / 2,347 | 0 | 1.619 / 22.804 / 190.831 |
| A2, both files | 3,370 / 3,370 | 0 | 1.457 / 22.389 / 172.822 |

The accepted B sample lasted 426.110 s. Although ping was configured for 100 ms, observed reply-line gaps had median **201.075 ms** and maximum **3,267.294 ms**; actual logging/probe cadence was not uniformly 10 Hz. These are reader-observation gaps, not measured RF transmit times, so they cannot distinguish scheduling/buffering from network delay by themselves.

Near the B P event, received ping lines were observed at offsets **−107.703 ms**, **+97.102 ms** (RTT 3.634 ms), and **+201.767 ms** (RTT 5.859 ms). The two following replies fall within a conservative 250 ms envelope, but **neither falls inside the actual 47.895 ms window**. Thus this run observed no lost gateway ICMP probes overall and no restore failure; it **does not prove that infrastructure frames survive the receive window without loss**. A tighter, reliably timed experiment or RF observation remains needed for that question.

## Remaining limits and restoration

- One host/adapter, one channel pair, one ordered A/B/A sequence and one ping-covered B negotiation. A1's other-device radio state was not fully controlled. No sleep/wake or long-term discovery claim; ASK times remain variable.
- The user separately reported that this host sometimes fails to list the 149-channel AP until Wi-Fi is cycled. Cause is unresolved; this is not claimed fixed by #47. Successful connection-time channel/country checks do not explain an intermittent scan failure.
- Narrowing the filter bits to avoid promiscuous reception remains separate follow-up work, as requested.
- Full EFI backups were verified before each deployment. After testing, all **67 original EFI files** matched the pre-test hashes byte-for-byte. A final reboot verified the user's original driver UUID, original boot arguments and defaults; phone/reference-Mac settings were restored. Raw logs, EFI, serial numbers and peer/interface addresses remain local.
