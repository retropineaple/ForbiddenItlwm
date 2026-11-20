# Idle receive-peer handoff host checks

Run `python3 tests/portable_rx/test-handoff.py && python3 tests/portable_rx/test-handoff-rx.py` on macOS with Python3 and CLT. ASan/UBSan production-extraction checks; no driver loading or network traffic. The only new flag is `-itlawdlpeerhandoff`, absent/off by default. These checks are not RF/AirDrop A/B evidence.

## Beta.2 follow-up: software validation and hardware retest

The isolated branch is rebased onto **0.6.0-beta.2**, exact commit
`6e151958dd1b5348c586e0efc22a2f9ed5158591`. The flag remains opt-in/off by
default; the new hardware A1/B/A2 will use this base, this base plus only
handoff, then the identical stock baseline again. **Beta.2 hardware results
are pending.** The historical measurements below are not beta.2 results.

The reconnect extraction harness now includes the real `PeerHandoff` and
`BaFilterState` types. It tests invalidation of a pending challenger when
the listener observes a firmware-generation reset, preservation on a
current-generation no-op, and survival of new-generation work when an older
waiter returns. The production reset/cleanup logic is retained.

Software checks (ASan/UBSan where implemented by the runners):

```sh
python3 tools/test_awdl.py
python3 tests/portable_rx/test-handoff.py
python3 tests/portable_rx/test-handoff-rx.py
```

## Historical hardware A/B/A on alpha.1 main

Rebased onto main `96ff71e` (0.6.0-alpha.1), retaining default-on #47 and the upstream #57 filter fix. A1/A2 use this exact main; B adds only this PR and `-itlawdlpeerhandoff`. No control-RX or single-rate policy is enabled.

Target hypothesis: an idle receive BA peer can prevent a newly discovered peer's valid ADDBA request from obtaining the station. This patch permits a bounded, validated challenger to replace an idle owner only after queues and control work are drained. It is not a BLE/mDNS discovery repair and is not evidence for stable Finder visibility.

The initial incoming-transfer attempt was incomplete/inconclusive because of discovery outages and LAN fallback. An independent outgoing redo then sent AX211 → native Mac, AX211 → iPhone, AX211 → native Mac, twice in each ordered A1/B/A2 stage. The phone remained Wi-Fi-enabled but AP-disconnected to qualify AWDL transport.

Phone completion was **0/2 on A1, 2/2 on B, 0/2 after A2 rollback**. All twelve native-Mac sends completed over AWDL. Both B transitions showed retirement activity without an error-counter increment; B sequence 2 directly qualified the bulk data's distinct A/B/A destinations using payload-free IPv6 frame headers. This supports the targeted idle-switch case on this configuration; it does not establish a discovery fix or universal default-enable recommendation.

See [the full hardware report](PEER_HANDOFF_ABA.md) for pinned revisions, timings, transport evidence, negative outcomes, first-B-sequence peer-evidence limits, extra A2 discovery preparation, lock/wake controls, capture differences, ping and final restoration. Remote receiver hashes and complete hardware queue/epoch attribution were not collected. Keep the PR opt-in pending maintainer review; do not substitute successful files alone for actual feature/transport evidence.
