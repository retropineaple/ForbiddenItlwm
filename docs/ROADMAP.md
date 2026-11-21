# Roadmap

What's being worked on, what comes next, and what's planned further out. These are plans, not promises: there are no dates, and priorities change with what testers find. Issue numbers link to the discussion.

## Now

- **Bluetooth stays on for days** ([#62](https://github.com/retropineaple/ForbiddenItlwm/issues/62)). `bluetoothd` can crash after hours under memory pressure, because BlueToolFixup and BTLEAdvFixup patch its code in the copy macOS shares between processes. The fix is to patch a private copy instead.
- **Mac → iPhone on the remaining slow setups** ([#24](https://github.com/retropineaple/ForbiddenItlwm/issues/24), [#26](https://github.com/retropineaple/ForbiddenItlwm/issues/26), [#63](https://github.com/retropineaple/ForbiddenItlwm/issues/63)): where the Mac's Wi-Fi is on another channel than the iPhone's AirDrop channel, or the listener shares it without `-itlawdlcoexmulti`.
- **Stable Wi-Fi when the Mac is on 2.4 GHz** ([#16](https://github.com/retropineaple/ForbiddenItlwm/issues/16)). With the listener on its own channel, one AX201 kept dropping its normal Wi-Fi while that was on 2.4 GHz. 0.5.2-test1 has a test flag (`-itlawdlsameband`) for it.

## Next

- **AirDrop on Sonoma** ([#55](https://github.com/retropineaple/ForbiddenItlwm/issues/55)): on 14.8.9 the driver and the listener start, but macOS doesn't ask for `awdl0` yet. More Sonoma builds after that.
- **AirDrop without a Wi-Fi network.** The listener runs beside the Mac's Wi-Fi connection, so with no network joined AirDrop stays on "Waiting". A real Mac doesn't need a network for this.
- **Fewer boot flags.** The beta needs a long list of flags. They should become sensible defaults, with flags only for testing.
- **Faster normal Wi-Fi while AirDrop is on.** The Mac's own Wi-Fi loses about 30 to 45% of its speed while AirDrop's listener runs, because the two share the radio. Giving the listener less airtime while nothing is being sent should win most of that back.
- **Wi-Fi 6 by default.** The driver can use Wi-Fi 6 (802.11ax), but only with the boot argument `itlwm_he=1`, which comes from the upstream code. One tester runs it with AirDrop without problems ([#58](https://github.com/retropineaple/ForbiddenItlwm/issues/58)); the goal is to test it here and turn it on by default.
- **Faster connects.** A cold iPhone → Mac connect takes 3 to 10 s, mostly before any data moves. The target is under 2 s in both directions.
- **AirDrop's channel picked the way macOS picks it** ([#35](https://github.com/retropineaple/ForbiddenItlwm/issues/35), [#68](https://github.com/retropineaple/ForbiddenItlwm/issues/68)). macOS works out its own AirDrop channels with Apple's rules, but the listener still guesses from what nearby devices announce. The next test build publishes macOS's choice; if testers' results show it's the better one, the listener will follow it. With two devices on different channels, the listener should also follow the one it's exchanging files with.
- **More macOS builds:** Sequoia 15.6 ([#19](https://github.com/retropineaple/ForbiddenItlwm/issues/19)) and each new Tahoe or Sequoia update, checked against Apple's Kernel Debug Kit before AirDrop is switched on for it.

## Later

- **Instant Hotspot:** joining the iPhone's hotspot from the Wi-Fi menu without typing the password. It starts with a Bluetooth LE connection to the iPhone, which times out today ([#39](https://github.com/retropineaple/ForbiddenItlwm/issues/39), [#40](https://github.com/retropineaple/ForbiddenItlwm/issues/40)).
- **Sidecar and Universal Control over Wi-Fi** ([#42](https://github.com/retropineaple/ForbiddenItlwm/issues/42)): they need AWDL traffic registration, which the driver doesn't support yet. Sidecar over USB works. AirPlay over this driver already works for users, in both directions (#29, #42).
- **More than one AirDrop channel at a time, like a real Mac (research).** Apple devices change channel 16 times in each quarter of a second, following a shared schedule, so they can meet a device on 149 and another on 44, and their own Wi-Fi in between. The listener stays on one channel. Intel's firmware can be told to be on a channel at set times, and that might let it follow macOS's schedule slot by slot. It's an experiment first: it isn't known yet whether this firmware allows it.
- **Continuity Camera:** using the iPhone as the Mac's webcam ([PR #72](https://github.com/retropineaple/ForbiddenItlwm/pull/72)). In progress on Sequoia with `-itlawdlllw`: stream reaches the Mac; radio-level Seq miss still ~7–14% in bursts ([DEBUGGING.md Case 3](DEBUGGING.md#case-3-continuity-camera-gets-part-of-the-way)).
- **A unified Bluetooth kext that survives macOS updates:** one Bluetooth kext made for ForbiddenItlwm, replacing BTLEAdvFixup, IntelBluetoothFirmware and BlueToolFixup, with patches that keep working across small macOS updates.
- **A look at Ventura 13.** Ventura needs its own study of that version's Wi-Fi stack first, so it isn't certain.
- **Sleep and wake:** not tested on the project's hackintosh, where sleep is disabled. One user reports five nights of scheduled sleep with AirDrop on: four woke normally, and on one the driver stayed unavailable until Wi-Fi was turned off and on ([#38](https://github.com/retropineaple/ForbiddenItlwm/issues/38)).

## Not planned

- **iPhone Mirroring:** it very likely needs a T2 chip or Apple silicon, which a hackintosh doesn't have.
- **Internet Sharing over Wi-Fi** ([#28](https://github.com/retropineaple/ForbiddenItlwm/issues/28)): the card can't act as an access point with this driver. Sharing over Bluetooth or USB works.

## Done recently

- **0.6.0:** channel 44 ([#17](https://github.com/retropineaple/ForbiddenItlwm/issues/17), [PR #21](https://github.com/retropineaple/ForbiddenItlwm/pull/21)); fast Mac → iPhone when the Mac's Wi-Fi shares the listener's channel ([#22](https://github.com/retropineaple/ForbiddenItlwm/issues/22), [PR #47](https://github.com/retropineaple/ForbiddenItlwm/pull/47)); AirDrop's listener and Wi-Fi recover across reconnects ([PR #12](https://github.com/retropineaple/ForbiddenItlwm/pull/12)); a security review.
- **0.5.4:** laobamac's later fixes to the base driver ([#36](https://github.com/retropineaple/ForbiddenItlwm/pull/36)); block ack without `-itlawdlcoexmulti` ([#16](https://github.com/retropineaple/ForbiddenItlwm/issues/16)); new LE mice and keyboards pair again with BTLEAdvFixup 0.9.1 ([#31](https://github.com/retropineaple/ForbiddenItlwm/pull/31)).
- **0.5.1:** AirDrop works on Sequoia ([#7](https://github.com/retropineaple/ForbiddenItlwm/issues/7), [#13](https://github.com/retropineaple/ForbiddenItlwm/issues/13), [#19](https://github.com/retropineaple/ForbiddenItlwm/issues/19)).
- **0.5.0:** Wi-Fi starts again on machines where 0.4.x didn't ([#14](https://github.com/retropineaple/ForbiddenItlwm/issues/14), [#16](https://github.com/retropineaple/ForbiddenItlwm/issues/16)); channel 149 in China ([#11](https://github.com/retropineaple/ForbiddenItlwm/issues/11)); a Sonoma 14.8.9 test build.
- **0.2.0:** iMessage and other push services over Wi-Fi.

See the [changelog](../CHANGELOG.md) for everything.

## Open items from the 2026-11-20 camera and AirDrop investigation

Details and numbers are in [AWDL-REFERENCE-DATA](AWDL-REFERENCE-DATA.md).

- Block Ack: the iPhone answers our ADDBA requests (status 0, window 64) but the host never sees the answers, so aggregation never starts. Read `ITLAWDLRawBaAction` after a transfer to see whether the answers reach the host.
- AirDrop Mac to iPhone over `awdl0` is much slower than the real Mac (peak 15 Mbit/s against 80 to 93): few slots used, 25% retries, rates capped at HT MCS 7 (`-itlawdlht2ss` lifts the cap; untested).
- A listener that visits channels 149 and 6 per the family's plans (the real Mac does) is not implemented; discovery after the iPhone's AWDL has idled depends on it coming back to 161.
- Continuity Camera: the remaining 2.8% loss is spread over all slots and is not explained by the iPhone's schedule.
- Frame differences against the real Mac still to measure over the air: TLV 4 flags, remaining-AW field, TLV 18 width.

