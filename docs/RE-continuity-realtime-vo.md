# Apple Continuity realtime → WME VO (ASM) → AX200 port

Artifact: `/tmp/kc-kexts/AppleBCMWLANCoreMac.ok`
SHA-256: 004a5437e159e715ed7b91d6f69ee72375fc229737cb64c2c7b76e51438f2f0c
__TEXT fileoff 0x1000, VA base 0xffffff8001549000

## ASM evidence

### setRealTimeMode → wme_ac_sta VO
At `0xffffff8001551f7a` / `0xffffff8001552052`:
```
lea rsi, [rip+…] ; "wme_ac_sta"
mov word ptr [rdx+8], 0x10
call <iovar set>
```
Error strings in the same function:
- `Error: cannot set wme_ac_sta (VO): ret %x: %s` (setRealTimeMode)
- `Error: cannot set wme_ac_sta (VI): ret %x: %s` (setDefaultMode)

### setLLW_PARAMS → WLC_SET_VAR llw
- `"Cannot set llwParams (%s): %s"` / `setLLW_PARAMS` / `setLLW_PARAMSCallback`
- `"WLC_SET_VAR: llw"` / `"WLC_GET_VAR: llw"` at `0xffffff80016f2512` — length check vs 9/10
- `wlan.llw.tx.ring.size` clamped to [0x200, 0x800]

### AMPDU BA window (related Continuity reliability)
- `ampdu_tx_ba_window_cfg` / `ampdu_rx_ba_window_cfg` via WLC_SET_VAR with 0x28-byte cfg

## Live AX200 gap (before port)
- Continuity Seq miss ~9.7%, present-slot ~87%
- AWDL/llw TX forced `EDCA_AC_BE` even for QoS TID7 (maps to VO in `ieee80211_wcl_ac`)
- `BaPeerAdded=No`, `BaStatus`/`LowLatencyResult` both ENOSPC — lowlat refresh competed with ADD_STA

## Port
1. `itlAWDLNoteRealtime()` from `setLLW_PARAMS` and active `setAWDL_PEER_TRAFFIC_REGISTRATION`
2. `awdlCoexRealtime()` = ContinuityActive OR 30s after note
3. `awdlCoexDataAc()` → `EDCA_AC_VO` while realtime (else TID→AC via `ieee80211_wcl_ac`)
4. Skip Continuity lowlat refresh until `BaPeerAdded` so ADD_STA owns the cmd ring
5. ioreg: `ITLAWDLCoexRealtime`, `RealtimeNotes`, `TxAcVo`
