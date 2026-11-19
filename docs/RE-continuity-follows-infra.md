# Apple Continuity / ULL / FollowsInfra RE notes (Sequoia Boot KC)

Artifact: `/tmp/kc-kexts/IO80211Family.ok` (x86_64 MH from BootKernelExtensions.kc)
SHA-256: 44e0dcb8dd5df0063ec20ce662eff3012ab63d5c0fb67d19021166ba075720d9
__TEXT base: 0xffffff8002111000

## Key strings → owning procedures (Hopper xrefs)

| String | VA | Containing procedure | Role |
| --- | --- | --- | --- |
| adaptCommonSocialChannelForUltraLowLatencyInfra | 0xffffff800233fe7f | sub_ffffff800216c154, sub_ffffff8002172c6c | Named bool config (parsed with rtgRetroModeOn2GEnabled, forceDisableAssocCC, …) |
| User set adaptCommonSocialChannel… | 0xffffff8002340806 | sub_ffffff800216c154 | User/sysctl change log |
| Disabling AWDL in FollowsInfra mode because | 0xffffff8002345075 | sub_ffffff8002189b54 (monitorAWDLState) | Disable when DFS/locale or not associated |
| setIsRTGUltraLowLatencyInfraActive | 0xffffff800235bb9a | sub_ffffff800219a7f0 | Sets ULL-infra active flags + schedule state |
| kAwdlScheduleStateUltraLowLatencyInfra | 0xffffff800235f5f5 | (enum/name) | Schedule state for Continuity ULL |
| AwdlFollowsInfra | 0xffffff8002336137 | no direct code xref (name/property) | Follows-infra mode label |

## ASM evidence (Capstone)

### adaptCommonSocialChannelForUltraLowLatencyInfra is a config key
At 0xffffff800216d70d / 0xffffff800217470f the code LEAs the string and calls the same property get/set helpers used for neighboring bools (`rtgRetroModeOn2GEnabled`, `forceDisableAssocCC`, `assistedDiscoveryInRetroModeEnabled`).

### FollowsInfra disable (monitorAWDLState)
At 0xffffff800218b02c–0xffffff800218b059:
- reason A: "infra is on DFS or not allowed in locale"
- reason B: "we're not associated"
- selected by `cmp byte ptr [ivars+0x3ec0], 0` / cmovne, then log via "Disabling AWDL in FollowsInfra mode because %s" / "monitorAWDLState"

### setIsRTGUltraLowLatencyInfraActive
At 0xffffff800219ab10–0xffffff800219ab50:
- writes active bool to ivars+0x422b and ivars+0xc3f8
- logs "Setting isRTGUltraLowLatencyInfraActive to %s"
- when a peer/context is present, sets dword ivars+0x3eb8 = (active?1:0)*2+3 → 5 when active (ULL infra schedule state)

## Port into ForbiddenItlwm (AX200)

### Regression (2026-11-18)
Auto-follow on `awdlCoexContinuityActive()` alone moved the Mac listener toward infra **161** while `ITLAWDLCoexPeer5GHz=149` and the phone's TLV-18 still named 149/36/6. Result live: `MultiState=move-uncertain`, `MoveFailures=1`, `FollowsInfra=Yes` sticky with `Channel=149`, `DataRxUcast=0`, Photo Booth stopped talking to the iPhone camera. Apple's ULL adapt is coordinated on both ends; a Mac-only social-channel move desyncs Continuity.

### Current gate
Follow infra only when:
- `-itlawdlcoexfollowsinfra`, or
- Continuity warm **and** `awdlCoexPeerNames(seq, stationPrimary, true)`

Also: clear `awdlCoexFollowsInfra` on `move-uncertain`; throttle bypass only when FollowsInfra/force is already set (not bare ContinuityActive).

ioreg: `ITLAWDLCoexFollowsInfra`, `ITLAWDLCoexFollowsInfraForce`

AppleBCMWLANCoreMac also has setLLW_PARAMS / AppleBCMWLANLowLatencyInterface / wlan.llw.* — Broadcom LLW firmware path; Intel equivalent is existing ASYNC LOW_LATENCY_CMD + ContinuityPower.
