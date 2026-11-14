# AX211 AWDL receive-window and idle-peer experiments

This patch ports the local `portable-rx-p1` changes from ForbiddenItlwm `aa707a7afcd4eda793e0d9ffa7ee7d515cb2294e` onto upstream `69e88fa0fdb0ef7122f2aee81111651c32260a62` (0.5.4-beta.2). The original build was tested on AX211 PCI 8086:7A70, macOS 26.7.2/25G332/x86_64. **The rebased build has host/build validation, not a new hardware performance run.** Current upstream Block Ack and Bluetooth changes may alter the original results.

## Driver changes

- Bounded receive windows for known-peer short TCP control traffic. A successful, correctly owned TX completion can arm a roughly 250 ms window, with a 1 s cooldown. Generation, listener token, peer identity and the actual transmitted channel bind the completion/window; stale callbacks cannot arm a window on a different channel. Software frame/address/BSSID guards remain.
- A bounded same-channel management-response probe during BA negotiation. MAC-filter failure retains the restoration obligation; retry and removal paths restore the filter. This is gated by `-itlawdlcoexsamechprobe`.
- Conservative legacy 24 Mbps for eligible single AWDL unicast frames. Aggregated transmission and ordinary infrastructure Wi-Fi retain their rate control. This recovered native-Mac TCP ACK delivery on the original test host; it is not a global rate recommendation for every adapter.
- Idle receive-peer handoff on an already validated challenger's ADDBA request, only after the previous peer is idle and queues/control work drain. No firmware command is issued directly from RX. Active transfers should not be displaced. The more speculative short-control peer-station experiment stays disabled.
- RX-stage/Block Ack diagnostics and header dependency output for clean-build/layout review.
- Peer-driven legal channel 44 fallback, complete bandwidth-block checks, effective80+80 secondary-centre overlap protection, and channel-bound control leases. The regional work is derived from zheznanohana's PR#21 (`db25e6dcd65e63e84130c16a012d6795b81fee5a`), with explicit credit here; it is not an independently invented replacement for that PR. Firmware channel permissions, passive-beacon requirements, DFS and NOADHOC restrictions remain. No country spoofing or permission override.

Upstream's current shared-context BA behavior, received-byte DMA copy, diagnostics and `-itlawdlsameband` override are retained. The picker and passive parser now share one sequence/country state rather than introducing duplicate globals during the port. Maintainers may prefer to review/reuse the regional work through #21 separately.

## Runtime defaults and scope

The port retains the original candidate defaults; these affect eligible AWDL listener traffic on supported cards, not only a PCI-ID-specific AX211 branch. Broader default policy needs maintainer/hardware review.

| root-only sysctl | default | purpose |
|---|---:|---|
|`debug.forbidden_awdl_control_rx`|1|bounded short-control RX window|
|`debug.forbidden_awdl_control_peer`|0|unproven short-control peer station/retarget experiment|
|`debug.forbidden_awdl_single_legacy`|1|eligible single-frame legacy 24 policy|
|`debug.forbidden_awdl_peer_handoff`|1|idle receive-peer handoff|

Handlers validate0/1, only update atomic state, and do not issue firmware commands. Lifecycle tests cover register/unregister ownership and stale generation/token rejection. Existing AWDL boot flags are still required; none of this enables AirDrop on unsupported OS builds or repairs Bluetooth discovery by itself.

## Historical hardware evidence (before this upstream port)

Reference file: 247,636,698 bytes. DecimalMB/s below is application file-phase average, including protocol overhead, **not PHY rate**. AP149 and AWDL149 were 80 MHz. Local received files were size/SHA-256 verified; outgoing completion was confirmed by the receiver/user, without a remote hash.

| direction | MB/s | condition |
|---|---:|---|
|local→iPhone|30.8|AP149/AWDL149, capture enabled|
|iPhone→local|31.8|AP149/AWDL149, full incoming hash match|
|nativeMac→local|20.7|AP149/AWDL149, full incoming hash match|
|local→nativeMac|16.9|AP149/AWDL149, receiver confirmed complete|
|local→iPhone|29.4|AP149/AWDL149, no BPF, phone Wi-Fi on but disconnected from AP|
|local→iPhone|7.2|AP36/AWDL149, no BPF, phone disconnected from AP|
|iPhone→local|22.1|AP36/AWDL149, no BPF, full incoming hash match|

Interface byte counts supported full AWDL transfer for these rows. A separate18.6MB/s normal-use run selected ordinary LAN and is excluded from AWDL performance evidence. These are single-host observations and not a controlled before/after benchmark against0.5.4-beta.2.

An earlier same-boot OFF/ON/OFF receive-window comparison observed TCP setup around 1555/72/1620 ms with full AWDL file bytes, but subsequent cold/warm runs still varied. Do not treat the feature as a universal fast-handshake guarantee. No new raw captures, endpoint identifiers, EFI, Bluetooth Kexts or account data are part of this PR.

## Known limitations

- Discovery remains unresolved: a 601 s observation had 276/276 receive-listener samples available, yet closing/reopening Finder on the native Mac subsequently made this machine completely invisible. Listener availability is not proof of UI discovery.
- Two usable same-name entries sometimes resolve to the same local endpoint. Neither identity merging nor authentication bypass is attempted.
- AP36/AWDL149 sending can spend a long time asking for aggregation before data flows; the original test saw 30–40 MB/s intervals after agreement, but much lower full-file average. This port does not claim to fix that negotiation delay.
- Country44 fallback,80+80, other adapters/OS builds, sleep/wake and broad multi-device behavior require additional hardware testing. No all-country compliance/performance certification is claimed from host tests.
- The test host used a separate custom BlueToolFixup mask-fix2 setup. That source and binary companion live in the fork's release, and are intentionally outside this Wi-Fi PR. No upstream Bluetooth code is replaced.

## Validation commands

```bash
python3 tests/portable_rx/run.py
python3 tools/test_awdl.py
python3 tools/build.py --target tahoe --out build -j 8
python3 tools/abi/audit_vtable.py --target tahoe --build build --out build --lifecycle-overrides
python3 tools/abi/check_allocation_layout.py build/ForbiddenItlwm.kext/Contents/MacOS/ForbiddenItlwm --output build/hal-layout.json
python3 tools/abi/check_awdl_allocation.py build/ForbiddenItlwm.kext/Contents/MacOS/ForbiddenItlwm build/awdl-layout.json
```

These build/layout and sanitizer tests do not load a driver or change EFI. The new host tests extract relevant production functions, exercise restoration failures and ownership/channel races, and compare narrowly scoped inherited RX guards against frozen excerpts. The RX oracle incorporates upstream 69e88fa's received-byte DMA fix. Source includes and class-allocation invariants are checked after a full build; these are narrower claims than a complete ABI proof.

### Results on the upstream port

- All 16 portable-rx groups and the channel-bound completion/window regression passed with ASan/UBSan where applicable.
- Clean Tahoe, Sequoia and Sonoma builds passed (build-only validation for the latter two). Tahoe produced 76 objects and 76 dependency files; 568/568 checked virtual slots matched; ItlIwx/ItlIwm/ItlIwn allocation pairs and the ItlAWDLInterface factory/MetaClass pair matched. The existing three cast warnings remain.
- The inherited runner passed its protocol/build-guard/scheduling/HCI/ownership checks, then stopped at `btleadv_patterns.py` because it defaults to the26.7.1 pattern set and this host is 26.7.2. **The full runner is not reported as passing.** The matching `python3 tests/btleadv_patterns.py --set 2672` check passed on this host; no controller behavior is inferred from it.
- The later inherited snapshot, bridge, pool-lease and bounded RX-copy tests were run individually and all passed after the runner stopped.
- No new Kext was loaded, no EFI or Bluetooth settings were changed, and no hardware retest of this upstream port was performed.
