# Same-channel management-response probe

The later [A/B/A replication with gateway ping and a retained P/R sequence](SAME_CHANNEL_ABA.md) supplements the initial single-pair results below, including explicit timing, peer-identity and device-isolation limits.

Run `python3 tests/portable_rx/test-samech.py` on macOS with Python3/CLT. Production-extraction tests use ASan/UBSan, compare emitted MAC commands with a beta.2 oracle while the flag is off, inject firmware restore failures, and verify unchanged RX parser/guard function hashes. No driver is loaded by this test.

The only new boot flag is `-itlawdlcoexsamechprobe`, **absent/off by default**. No control-TCP window, single-rate policy, idle handoff, regional picker, sysctl, or new Bluetooth patch is included.

## Hardware A/B protocol

Maintainer request: https://github.com/retropineaple/ForbiddenItlwm/pull/44#issuecomment-6877530248 . The old combined candidate's speed is not evidence that this isolated change fixes #24.

1. Use upstream 0.5.4-beta.2 (`69e88fa`) as A; this branch plus `-itlawdlcoexsamechprobe` as B. Retain the same existing AWDL flags, Bluetooth stack, phone, file, placement and network configuration. Do not enable any of the other experiments. Only change the tested driver/flag, keeping a verified bootable EFI backup; manually reboot between builds.
2. Confirm **Mac AP149/80 MHz and AWDL149/80 MHz** for each run. Keep iPhone Wi-Fi connection, unlock state and all other devices consistent. If either channel differs, mark the run invalid for this comparison.
3. Before Mac→iPhone transfer, run `python3 tests/portable_rx/collect_same_channel.py --label beta2-baseline --seconds 60 --out baseline.json`; for B use `--label same-channel-on --out enabled.json`. No root or packet capture required. The sampler saves only allowlisted counters, including `ITLAWDLCoexBaTxStateName` and the four decoded `ITLAWDLCoexBaActionSeen` buckets. It never changes boot args or toggles the feature.
4. Transfer the same file each run; record byte size, receiver-confirmed outcome, file-phase duration, handshake duration separately, and interface-byte evidence for AWDL versus ordinary LAN. Repeat A/B/A if possible. Save driver executable SHA/UUID, actual boot args, AP channel and build identity locally alongside each run.
5. Compare whether the BA state leaves `asking`, how ActionSeen changes, window opens/closes/restore failures, and successful application throughput. Do not infer a fix from one negotiated BA or a host test. If the isolated window does not reproduce the improvement, report that result and keep the PR draft.

No A/B values are fabricated or copied from the combined release. This PR does not claim discovery is fixed. Prepared binaries are not installed automatically.

## One completed A/B pair — 2026-11-15

AX211 PCI 8086:7A70, **macOS 26.7.3/25G342**, Mac AP149/80MHz and AWDL149/80MHz in both runs. These are new results, not the earlier 25G332 combined-candidate measurements. A is self-built upstream 0.5.4-beta.2/69e88fa; B is the isolated ea81caa change with `-itlawdlcoexsamechprobe`. The other three split experiments are absent. Each run started after a manual reboot; the same Bluetooth stack, phone and network settings were retained, with the native Mac online and not sniffing.

Reference: 247,636,698 bytes, SHA256 `a31d673c39851f0f6eb3fa53f2570cd06182b402d414e0d5716fab8b96296159`. Both outgoing files were confirmed complete on the receiver by the user; **no remote file hash was collected**. No BPF capture or sysctl write. Durations are sharingd UPLOAD/ASK phase timestamps; interface bytes include protocol and concurrent background traffic.

| Observation | A: beta.2 | B: window enabled |
|---|---:|---:|
|File/protocol phase|65.321 s|16.088 s|
|File bytes / phase time, decimal MB/s|3.791|15.393|
|ASK phase|6.103 s|7.521 s|
|AWDL TX byte delta|264,181,413|265,361,412|
|Ordinary en0+en1 TX byte delta|16,406,928|5,785,422|
|BA state before / after / all samples|asking / asking / asking|agreed / agreed / agreed|
|ActionSeen before → after|[11,0,0,0] → [12,0,0,0]|[3,1,0,0] → [4,1,0,0]|
|BA TX frame delta|0|178,186|
|BA TX notification delta|0|3,889|
|Probe opens/closes before → after|not published by stock A|1/1 → 1/1|
|Probe restore failures before → after|not published by stock A|0 → 0|

`ActionSeen` buckets are ADDBA request, ADDBA response, DELBA, other. B's response count 1 and its one closed probe were **already present before transfer sampling**; there was no new response/window opening observed during the measured file. This supports successful BA establishment followed by aggregated traffic, but does not capture the exact window→reply handshake timing. A's response count remained0 and no aggregated TX was recorded. The matching full-file-sized AWDL deltas and the system's AWDL-path log distinguish these runs from a LAN-selected transfer.

Both builds passed the runtime build check (`verified`); loaded UUIDs were A `3D378C4F-DD82-306F-ADF1-3DD77D5A2F2F`, B `A57DCB05-511F-31A0-A61A-3AFFC9B5DA5E`. Initial infrastructure PHY-mode/rate reports were 802.11ac/260Mb/s in both; these are not the measured AWDL throughput.

**Initial-pair scope:** B's file-phase average was 4.06× A in this one ordered A/B pair. ASK time did not improve. This initial pair did not include A/B/A replication, randomized order, RF capture of the negotiation, sleep/wake or other-adapter validation. The subsequent A/B/A is documented separately above. This is evidence for review of the opt-in same-channel change, not a general speed guarantee or a discovery fix. Raw logs, interface/peer addresses, personal EFI and configuration are kept out of the PR.
