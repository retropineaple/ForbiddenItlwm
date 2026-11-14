# BTLEAdvFixup optional advanced patches

The standard BTLEAdvFixup path remains the four compatibility patches for advertising,
scanning, advertising-set random address setup, and extended connection setup. The USB/HCI
ordering workaround remains unconditional when the plugin is enabled.

The six advanced page patches are **opt-in** with `-btleadvextra`; they are not applied by
default and are available only for the targeted Tahoe builds 25G332 (26.7.2) and 25G342
(26.7.3). They are:

- `addressResend`: resend an advertising-set random address after parameter changes.
- `rpaHook` and `rpaSave`: capture software-resolved identity/RPA pairs in a sixteen-slot cache.
- `falAdd`, `falHelper`, and `falRemove`: substitute the latest over-the-air RPA in controller
  Filter Accept List add/remove commands while retaining identity addresses in bluetoothd's
  software mirror.

The RPA/FAL group addresses the mismatch where bluetoothd resolves an iPhone's RPA to its
identity address, but a third-party controller without a programmed Resolving List receives the
RPA. A controller Filter Accept List containing only the identity address therefore cannot match
the advertisement. The implementation is derived from the approach documented in
[BlueToolFixup-FixA2DP-BLE PR #1](https://github.com/crazyhulk/BlueToolFixup-FixA2DP-BLE/pull/1).

The extra set advanced the user's iPhone Mirroring flow to the PIN-confirmation phase. This is
an observed functional boundary, not proof that mirroring completed, not a controlled comparison
of each patch, and not an AirDrop transfer result. The injected helper code and writable cache
remain experimental; unknown builds, Sonoma and Sequoia pass through without these extras.

The event-mask fix is split into a separate PR for normal `-btleadv` operation,
as requested in review. This branch does not include or require that new PR.

Historical 25G332 pattern/hardware validation is user-reported; its original binary is
not retained for a fresh offline check of helper branches, page co-location or cache
placement. The refactored opt-in kext has not been deployed. A clean-boot test of the
flag on/off paths remains necessary. This branch is rebased on 0.5.4-beta.2,
including upstream's current standard 25G332 compatibility support and HCI fixes.
