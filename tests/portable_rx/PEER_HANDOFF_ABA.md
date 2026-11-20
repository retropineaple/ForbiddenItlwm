# AX211 idle-peer handoff: ordered hardware redo

> Historical results on alpha.1 main `96ff71e` and its isolated handoff patch.
> The branch has since been rebased to 0.6.0-beta.2; that hardware retest is
> pending. This report's measurements and revisions remain unchanged.

## Result and scope

On 2026-11-18, an AX211 sender repeatedly sent the same file to a native Mac,
then an iPhone, then the native Mac again. Two sequences were attempted in
each ordered driver stage, A1 stock → B opt-in handoff → A2 stock.

**The iPhone failed before file upload in both A1 sequences, completed over
AWDL in both B sequences, and failed before upload again in both A2
sequences. All twelve native-Mac sends completed over AWDL.** This supports
the targeted benefit of idle-peer handoff on this configuration. It is not a
general speed recommendation, a BLE/discovery fix, or a proof of every
internal queue/epoch guard on hardware. Keep the experiment opt-in pending
maintainer review.

This is a separate redo of the earlier incoming-transfer attempt. That
[earlier attempt](https://github.com/retropineaple/ForbiddenItlwm/pull/50#issuecomment-6969412636)
remains incomplete/inconclusive: discovery blocked its continuation, and its
successful phone file used LAN. Its samples are not included below.

## Revisions and controls

- A1 and A2: exact main `96ff71e0211789239f6fbf6b64ac8e7fd22b41aa`,
  0.6.0-alpha.1. This includes default-on #47 and upstream #57; it is not
  being described as historical beta.2 plus only #47.
- B: `5f9421af4a5b4ff95e892fdcff54d46225efdaa4`, that base plus this PR,
  with only `-itlawdlpeerhandoff` added. #48/control-RX and #49/single-rate
  policy were off in every stage. No live sysctl toggling was used.
- AX211 host: macOS 26.7.3, build 25G342. Native Mac: macOS 15.7.7.
  The same physical iPhone was used throughout; handset model/iOS build
  were not recorded in this redo.
- Host and native Mac stayed on AP channel 149, 80 MHz; host AWDL was
  verified as 149/80 MHz. Loaded UUID, arguments, feature isolation and
  Bluetooth identity were checked after every driver reboot.
- The phone's Wi-Fi remained enabled but was disconnected from the AP in
  Control Center. This condition was fixed across A1/B/A2 to prevent LAN
  fallback. Both Macs retained their normal network connections. This does
  **not** establish the same result with all devices associated to the AP.
- Reference file: 247,636,698 bytes, SHA-256
  `a31d673c39851f0f6eb3fa53f2570cd06182b402d414e0d5716fab8b96296159`.
  The source hash was checked before each sequence. Receivers confirmed
  completion, but remote received-file hashes were not collected.

Preflight on the restored original driver caught a mistakenly selected native-Mac target and a true phone send using LAN. Both were excluded from formal samples. Only the subsequent eighteen marked attempts on the verified test drivers are tabulated here.

The operator was asked to wait five seconds after each completion before
selecting the next device. The predeclared acceptance window for a completed
upload → next send START was 5–60 seconds; actual observed intervals are
reported below. A phone that did not begin transferring was cancelled before
returning to the Mac. Actual timeout/cancellation delays were not exact
20-second timers and are not treated as file throughput.

## Measurements

Decimal MB/s = reference bytes / sharingd UPLOAD duration / 1,000,000.
This is the application's file phase, not pure payload or radio airtime.
“Timeout” has no full-file rate assigned to it.

| Driver stage / sequence | Native Mac first, s / MB/s | Native→phone idle, s | iPhone, s / MB/s | Native Mac return, s / MB/s |
|---|---:|---:|---|---:|
| A1 / 1 | 22.712 / 10.903 | 15.690 | ASK/connection timeout; no upload | 14.085 / 17.582 |
| A1 / 2 | 15.616 / 15.858 | 15.030 | ASK/connection timeout; no upload | 12.285 / 20.158 |
| B / 1 | 13.407 / 18.471 | 16.019 | 8.895 / 27.840 | 11.358 / 21.803 |
| B / 2 | 22.816 / 10.854 | 14.604 | 12.793 / 19.357 | 13.693 / 18.085 |
| A2 / 1 | 14.930 / 16.587 | 21.487 | ASK/connection timeout; no upload | 11.947 / 20.728 |
| A2 / 2 | 11.060 / 22.390 | 16.900 | ASK/connection timeout; no upload | 10.066 / 24.601 |

The phone→Mac-return idle intervals in successful B sequences were 20.082 s
and 18.901 s. Failed phone attempts have no UPLOAD END, so an upload-end
interval cannot be calculated for them. Their phone START → next native
START intervals were 107.390, 66.518, 56.880 and 45.184 seconds, respectively.
These include operator wait/cancellation and are not precise failure latency.
The first A1 sequence continued to the native return despite an initial
stop-on-failure instruction; this was retained. Subsequent instructions
explicitly allowed cancellation of the phone request followed by the return.

Failures were not discarded because an all-success sequence qualifier failed.
The four baseline phone targets were corroborated by outgoing SYN addresses
matching the phone subsequently identified in the successful B data capture.
Repeated native success plus phone failure is the negative observation of
interest; it is not a zero-throughput successful file.

## Transport and feature evidence

Every completed file above has a unique sender completion record, operator
receipt confirmation and interface-byte brackets consistent with AWDL
carrying the reference file. Infrastructure traffic was far below the file
size. Neither successful B phone file was the LAN fallback seen in the
earlier attempt.

During each B phone send and native return, the global handoff retirement
counter increased by one and the handoff error counter did not increase.
The first native send did not increment retirement. This is activity in the
target transition windows; it is not per-packet or per-retirement proof of
peer identity, queue drain, listener generation and epoch correctness.

B sequence 2 has the strongest endpoint evidence: a separate capture retained
only the 14-byte Ethernet and 40-byte IPv6 base headers. Original frame
lengths identify the bulk outgoing destination without retaining file
payload. It corroborates native A → distinct phone B → the same native A,
with at least a reference file's worth of wire bytes to each destination.
The phone's bulk frames were predominantly IPv6 next-header 17 (UDP).
No UDP payload was captured, so this does not identify an application
protocol such as QUIC.

B sequence 1 predates that extra header capture. Its phone completed over
AWDL and a distinct phone TCP handshake was observed before upload, but no
pure TCP ACK flow was observed during its upload bracket. The original
TCP-only peer qualifier therefore did **not** pass for that phone send.
Its limitation is retained rather than retroactively treating absent headers
as collected. UDP bulk traffic in sequence 2 explains why a TCP-only rule
is not generally suitable; it does not reconstruct sequence 1's payload.

## Preparation and limitations

- Discovery failed before some file sequences. Each driver stage had one
  local sharingd restart/AirDrop reopen before its first actual sequence.
  A1 also included additional read-only discovery/SSH diagnosis. No
  sharingd restart, permission toggle or device disconnect was used
  between the three sends of an actual sequence.
- Before B sequence 2, the native Mac was found locked. Unlocking and bringing
  its AirDrop page forward restored visibility; temporary wake assertions
  were then used in B sequence 2 and A2. The recorded lock time was later
  than the first invisibility report, and a bare port probe still timed out
  after unlock. Locking is not established as the sole discovery cause.
- A2 required extra preparation before its first file: native AirDrop reopen,
  one native sharingd restart, and native receiving Off→Everyone. The native
  Mac had retained a TCP session to the host's prior B-boot AWDL address.
  Local logs also showed `Lost BLE device` immediately preceding Bonjour
  shutdown. Device identity was redacted. These observations do not isolate
  a Bluetooth firmware, Wi-Fi driver, account, or stale-session root cause.
  This extra A2 preparation is a comparison limitation, not a #50 fix.
- The fixed-size IPv6 header capture was added after B sequence 1 and used
  for B sequence 2 and A2. Sampling is therefore not identical throughout.
  Each actual three-send sequence had uninterrupted local collection;
  capture renewals and diagnostic work occurred between sequences.
- TCP flag capture excludes extension-header flows and UDP. Some tcpdump
  children required forced cleanup; their final packet-drop statistics are
  unavailable, not zero. Recorded historical BA-index gaps precede the
  respective initial capture snapshots. No complete hardware reconstruction
  of per-retirement queue/epoch guards is claimed.
- This is two sequences per stage on one configuration, ordered rather than
  randomized. Time/RF conditions, operator delays and preparation differed.
  Remote SHA verification and protection of deliberately busy queued work
  were not established by these file tests. Host sanitizer checks cover the
  busy/stale/error guards separately; they are not RF proof.

Wi-Fi gateway ping was bound to the host Wi-Fi interface at a requested
0.1-second interval. Whole file-bearing capture windows, including their
waiting/preparation time, recorded:

| Capture window | Replies / transmitted | Median RTT, ms | Maximum RTT, ms |
|---|---:|---:|---:|
| A1, both sequences | 6381 / 6382 | 24.207 | 259.922 |
| B, sequence 1 | 11707 / 11707 | 25.512 | 216.132 |
| B, sequence 2 | 7082 / 7082 | 23.901 | 280.034 |
| A2, sequence 1 | 13262 / 13262 | 24.143 | 258.862 |
| A2, sequence 2 | 2169 / 2170 | 26.562 | 184.337 |

Window lengths differ. Missing replies remain missing, even where ping's
rounded printed loss was 0.0%. These samples do not prove zero lost
infrastructure frames or establish a causal handoff effect on Wi-Fi latency.

## Restoration and interpretation

The frozen pre-test driver and full EFI were restored. All 67 original files,
backup contents, boot arguments and strict signature were independently
verified. After the final manual reboot, the original loaded UUID,
Bluetooth identity and runtime defaults were verified again. Temporary native
Mac wake assertions were removed. The operator was asked to restore the
phone's normal AP connection. Raw logs, addresses, captures, EFI and SSH keys
remain local.

The repeatable phone outcome, 0/2 → 2/2 → 0/2, together with AWDL transport
and retirement activity, supports this opt-in patch for the tested idle
switching case. It does not establish universal reliability, higher peak
throughput, or stable AirDrop discovery. Maintainer review and additional
devices/conditions remain appropriate. AI-assisted implementation and analysis.
