# What the AX200 firmware API offers for AWDL

Read from FreeBSD's `if_iwlwifi.ko` (the Linux iwlwifi driver under linuxkpi, `mvm` for the AX200, `mld` for newer cards),
disassembled offline. Command identifiers and sizes below are the immediates the driver loads before its send call; what
each command does is Linux iwlwifi's documented behaviour. Nothing here has been run against the AX200 firmware by us.

## The pieces, and what AWDL uses each for

| Firmware object | Command (id, size) | Seen in | AWDL use |
| --- | --- | --- | --- |
| PHY context | `PHY_CONTEXT_CMD` 0x08, 32 B | `iwl_mvm_phy_ctxt_apply` | a channel and width: one per channel AWDL visits (our `kAwdlCoexPhyId`) |
| MAC context | `MAC_CONTEXT_CMD` 0x28, 148 B | `iwl_mvm_mac_ctx_send` | the listener: its address, BSSID, filter flags and ACK rates |
| Binding | `BINDING_CONTEXT_CMD` 0x2b | `iwl_mvm_binding_update` | ties MAC contexts to a PHY context |
| Time quota | `TIME_QUOTA_CMD` 0x2c | `iwl_mvm_update_quotas` | the firmware's own airtime split between bindings, in percent: what a 16-slot schedule across two channels needs |
| Time event | `TIME_EVENT_CMD` 0x29, 36 B | `iwl_mvm_time_event_send_add` | be on a channel at a given time, for a duration, repeating at an interval; answered by a response and start/end notifications |
| Session protection | group 3 cmd 5 (`0x305`), 24 B | `iwl_mvm_schedule_session_protection`, `iwl_mvm_start_p2p_roc` | the newer form of the same: protect a time span on the channel |
| Remain on channel | group 3 cmd 0xe (`0x30e`) | `iwl_mvm_roc_add_cmd` | park on a channel for a P2P device; notified by `iwl_mvm_rx_roc_notif` |
| Station | `ADD_STA` 0x18 | `iwl_mvm_sta_send_to_fw` | the iPhone's station: capabilities, aggregation limits |
| Rate scaling | TLC offload | `iwl_mvm_rs_fw_rate_init` | rate control for that station comes from the firmware, from the capabilities it is given |
| Transmit aggregation | queue per TID | `iwl_mvm_sta_tx_agg_start`, `iwl_mvm_sta_tx_agg_oper` | start allocates the queue, operational enables the TID once the peer's ADDBA response arrived |
| Receive aggregation | BAID / `ADD_STA` | `iwl_mvm_sta_rx_agg` | receive reordering for the peer's sessions |

Newer cards (`mld`) add NAN: `iwl_mld_limits_nan` and the NAN cluster and discovery-window-end notifications. The AX200's
`mvm` has no NAN symbols, so AWDL on it has to be built from the contexts, quotas and time events above.

## What this says about our design

- **Slot following is a firmware job.** The firmware can be told a channel and a time (time event or session protection), and it
  splits airtime between bindings by quota. Our listener sits on one channel; following the 16-slot plan would be a repeating time
  event or quota split on a second PHY context, not host retuning (the multi-channel mode that collapsed throughput retuned from the host).
- **The firmware gets the peer's capabilities only through `ADD_STA` and the TLC command.** Linux fills both from the peer's HT, VHT
  and HE capabilities. IO80211 hands us the same capabilities for an AWDL peer in `setPEER_CACHE_CONTROL` (HT cap info, A-MPDU
  parameters, MCS set, VHT maps, width at +0x44), and `awdlCoexBaAddPeer` ignores them: it sets the width from our own station,
  two streams, and a fixed aggregation size.
- **A transmit aggregation queue is only enabled after the peer's ADDBA response.** Linux's mac80211 handles that response; here
  it is our job, and the response has never reached the host (`ITLAWDLRawBaAction` [92, 0, 0]).

## What the AX200's own firmware declares

`itlwm/firmware/iwlwifi-cc-a0-68.ucode` (the image this driver loads on the AX200; version string `release/core66_LinADLPV::01d30b0c`,
API 68, build 30608140), parsed from its TLV header with the names of `if_iwxreg.h`. Its ENABLED_CAPABILITIES and API_CHANGES_SET
TLVs say:

| Declared | Why it matters for AWDL |
| --- | --- |
| `SESSION_PROT_CMD` (54) | time on a channel is asked for with session protection (`0x305`), as Linux does when this is set; the older time event is the fallback |
| `DYNAMIC_QUOTA` (44), `BINDING_CDB_SUPPORT` (39) | the firmware manages the airtime split between bindings itself, and bindings can sit on different PHY contexts at once |
| `NAN_SUPPORT` (34) and API `NAN2_VER2` (31) | the image contains a NAN engine (discovery windows, a sync beacon, data paths). NAN frames are not AWDL frames, so it is not a drop-in for the 16-slot schedule, but it is the only firmware-side slot scheduler on this card |
| `TLC_OFFLOAD` (43) | the firmware chooses the TX rate and aggregation for a station from the capabilities it is given |
| `MULTI_QUEUE_RX_SUPPORT` (68) | receive queues per CPU, with the firmware's own reorder (BAID) path |
| `UMAC_SCAN` (2), API `ADAPTIVE_DWELL` (32), `ADAPTIVE_DWELL_V2` (42) | the scan path our association work already uses |
| `BT_MPLUT_SUPPORT` (67) | the Bluetooth multi-priority table; sent with the coexistence command |
| API `STA_TYPE` (30), `NEW_RX_STATS` (35), `WIDE_CMD_HDR` (14) | the station and receive descriptor layouts the driver already uses |

Not declared: `CHANNEL_SWITCH_CMD` is among the unnamed bits here and was not decoded.

## Open questions this data does not answer

- Whether the firmware accepts a repeating session protection (or time event) with an interval, as AWDL's slots need. The capability
  says it takes the command, not what interval and repeat it permits.
- Why the firmware delivers the phone's ADDBA requests and not its responses.
