# AWDL diagnostics: counters and tools

Where to look when Continuity Camera or AirDrop is slow or loses frames. Start with [DEBUGGING](DEBUGGING.md); this page covers the counters and
tools added for the 2026-11-20 investigation. The measurements they produced are in [AWDL-REFERENCE-DATA](AWDL-REFERENCE-DATA.md).

## Registry counters (`ioreg -l -w0 | grep ITLAWDL`)

| Property | Meaning |
| --- | --- |
| `ITLAWDLAfCount` | action frames heard on the listener's channel from stations other than us |
| `ITLAWDLAfGapBins` | eight 32-bit counters: gap between consecutive frames under 150 ms, 300 ms, 600 ms, 1 s, 2 s, 4 s, 10 s, and 10 s or more |
| `ITLAWDLAfGapMaxMs` | longest gap in milliseconds |
| `ITLAWDLUmiBuilt` / `Submitted` / `Acked` / `Failed` / `Deferred` / `LastTxStatus` | the UMI path ([details](AWDL-CONTINUITY-UMI-AND-AW-EVENTS.md)) |
| `ITLAWDLRtgPeerSeen`, `ITLAWDLRtgPeerAbsent`, `ITLAWDLRtgPeerUll`, `ITLAWDLRtgPeerTlv` | RTG TLVs from the iPhone, frames without one, frames with the ULL flag, last TLV bytes |
| `ITLAWDLRtgOwnSeen`, `ITLAWDLRtgOwnAbsent`, `ITLAWDLRtgOwnUll`, `ITLAWDLRtgOwnTlv` | the same for our own frames |
| `ITLAWDLCoexSeq*`, `ITLAWDLCoexSeqGapRuns`, `ITLAWDLCoexSeqResyncs` | 802.11 sequence tracking per transmitter: frames the iPhone sent that we never heard, and tracker resynchronisations |
| `ITLAWDLCell*`, `ITLAWDLLossByPrevRate`, `ITLAWDLLossByNextRate`, `ITLAWDLChainDiff` | loss and retries per 8th of each of the iPhone's 16 slots, loss by the rate before and after a gap, the difference between the two receive chains |
| `ITLAWDLRawBaAction`, `ITLAWDLRawBaActionUnicast` | three 32-bit counters each (action code 0 request, 1 answer, 2 teardown): Block Ack actions as the firmware hands them up, before any filter in the receive path; the second counts those with a unicast receiver address. If the answer count rises while `ITLAWDLCoexBaActionSeen[1]` stays 0, the host receives the answers and a later filter drops them |
| `ITLAWDLRawBaActionHtc` | three 32-bit counters (action code 0 request, 1 answer, 2 teardown): Block Ack actions that carried an HT Control field (Order bit, category at byte 28). The field is cut out before anything parses the frame, so they are handled as plain ones. If the answer count rises, the phone's ADDBA answers were arriving all along and read as nothing |
| `ITLAWDLCoexBaTx*`, `ITLAWDLCoexBaAsksSkippedAway`, `ITLAWDLCoexBaActionSeen` | our Block Ack session towards the iPhone (state `asking`, asks sent, agreements) and the Block Ack actions we received by code (0 request, 1 answer, 2 teardown) |
| `ITLAWDLCoexRateStep`, `ITLAWDLCoexRateUps`, `ITLAWDLCoexRateDowns` | our unicast rate ladder position (0 legacy 24 Mbit/s, 1 to 3 HT MCS 4, 5, 7; with `-itlawdlht2ss` 4 to 6 are two-stream HT MCS 12, 13, 15, then VHT 80 MHz at 7 and 8) and `ITLAWDLCoexHt2ss` |
| `ITLAWDLCoexGate*`, `ITLAWDLCoexDataPumps`, `ITLAWDLCoexDataTx` | transmit gate holds because the iPhone is away, and the data pump's activity |

## Tools (`tools/diag`)

| Tool | Use |
| --- | --- |
| `cc_analyze.py` | Reads the registry (or a saved `ioreg` dump) and prints PASS / FAIL / INFO sections: command ring, Block Ack and station, power table, loss by sequence number, gaps against the access point's beacons, gap phase against the iPhone's 16-slot schedule, rate and width of what the iPhone sends, antenna chains, the requested channel sequences. Tests: `python3 tests/cc_analyze_test.py` |
| `llw0_rtp.py` | Reads an `llw0` pcap and reports the video RTP stream: loss, loss runs, and where in the iPhone's 524 ms traffic cycle they fall |
| `umi_rx.py` | Decodes `ItlwmAWDLUmiRxRing`, the action frames heard in the 3 s after an acknowledged UMI |
| `airdrop_paths.py [seconds] [interfaces]` | Samples `netstat -I` for `en0`, `en1` and `awdl0` during a transfer and prints bytes, share and peak rate per interface. Exits 1 when `en0` carried more than 1% |
| `trace_rtg_tlv.d` | `dtrace` script that follows the family building and parsing the RTG TLV |
| `airdrop_measure.py` | measures a transfer: per-interface throughput (mean, median, p95, peak, stalls), the driver's counters before and after (unicast failures, drops, parked frames, command ring errors, block ack, availability windows), the MTUs against Apple's 1500, and sharingd's send and receive results with the failure signatures. `--seconds 120 --json run.json --min-mbit 60` while a file is sent; `--replay run.json` judges it again. Exits 1 on any FAIL. Tests: `tests/airdrop_measure_test.py` |
| `trace_emsgsize_size.d` | sizes of IPv6 packets `sharingd` has refused with EMSGSIZE (the header comment still mentions an old awdl0 MTU of 1484) |

Typical sequence for a camera run:

```
python3 -I tools/diag/cc_analyze.py                      # judge the run
python3 -I tools/diag/airdrop_paths.py 60                # in another terminal, during an AirDrop
sudo tcpdump -i llw0 -s 160 -w llw0.pcap -G 20 -W 1      # then: python3 -I tools/diag/llw0_rtp.py llw0.pcap
```

## Reference data from a real Mac

`tools/real-mac-kit` holds the scripts used to collect the real Mac's side (`collect.sh` for a camera run with an `llw0` capture,
`collect2.sh` for the UMI timer and RTG lines, `collect_airdrop.sh` for AirDrop with `awdl0`, `llw0` and `en0` captures, neighbour-table samples and the
`sharingd`, `rapportd`, `wifip2pd` and kernel logs). Run `collect_airdrop.sh` on both machines for a like-for-like comparison. For the air itself
use a second Mac with Wireless Diagnostics, Window, Sniffer, on channel 161 (and 149).

## Sniffer capture analysis

Radiotap, 802.11 action frames (category 127, OUI `00:17:f2`, type 8) and Block Ack actions (category 3) are enough to answer most questions:
per-transmitter frame rates and gaps, TLV order and contents of a master indication, rates and retry flags of data frames, ADDBA requests
and answers by dialog token. Note that 802.11 sequence numbers are 12 bits, so counting repeats of a number across a long capture counts
wraparound, not retransmissions; use the retry flag.
