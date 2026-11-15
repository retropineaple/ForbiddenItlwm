#!/bin/bash
# Continuity feature check: for each feature, what it needs from this Mac and what evidence of it working the
# system has logged recently. Read-only, needs no sudo, sends nothing anywhere, prints no account or device names.
#
#   bash tools/diag/continuity_check.sh [minutes=15]
#
# "No evidence" is not a failure: most features log only when you use them. Run the check, then do the action in the
# "Try:" line on the iPhone or the other Mac, then run it again: the counts should move.
set -u
export LC_ALL=C
MIN=${1:-15}
if [ -t 1 ]; then GRN=$'\033[32m'; YEL=$'\033[33m'; BLU=$'\033[36m'; BLD=$'\033[1m'; RST=$'\033[0m'; else GRN=; YEL=; BLU=; BLD=; RST=; fi
ok()   { printf '  %s[ OK ]%s %s\n' "$GRN" "$RST" "$*"; }
warn() { printf '  %s[WARN]%s %s\n' "$YEL" "$RST" "$*"; }
note() { printf '  %s[info]%s %s\n' "$BLU" "$RST" "$*"; }
head1() { printf '\n%s%s%s\n' "$BLD" "$*" "$RST"; }
count_log() { # <predicate>: number of matching unified-log lines in the window
    /usr/bin/log show --last "${MIN}m" --info --style compact --predicate "$1" 2>/dev/null | grep -cE '^[0-9]{4}-'
}
running() { ps -axo comm | sed 's#.*/##' | grep -qx "$1"; }

head1 "0. Shared prerequisites"
BT=$(system_profiler SPBluetoothDataType 2>/dev/null | awk -F': ' '/^ *State:/ { print $2; exit }')
[ "$BT" = "On" ] && ok "Bluetooth is on." || warn "Bluetooth state: ${BT:-unknown}. Handoff, Instant Hotspot, Universal Control and Continuity Camera all need it."
HOOK=$(ioreg -l -w0 2>/dev/null | grep -oE '"BTLEAdvHookRouted" = [0-9]+' | head -1 | awk '{ print $3 }')
[ "$HOOK" = "1" ] && ok "BTLEAdvFixup's hook is routed (the Mac can advertise over Bluetooth LE)." || warn "BTLEAdvFixup's hook isn't routed (${HOOK:-not loaded}): iPhones won't see the Mac over Bluetooth."
if ifconfig awdl0 >/dev/null 2>&1; then
    ok "awdl0 exists ($(ifconfig awdl0 | sed -n 's/.*mtu \([0-9]*\).*/mtu \1/p' | head -1))."
else
    warn "No awdl0: AirDrop, AirPlay to Mac, Sidecar, Universal Control and Continuity Camera over Wi-Fi can't work."
fi
for IF in llw0 p2p0 ap1; do
    ifconfig "$IF" >/dev/null 2>&1 && note "$IF exists." || note "$IF doesn't exist (macOS only asks for it when a feature needs it; this driver doesn't create it)."
done
ROLE=$(ioreg -l -w0 2>/dev/null | grep -oE '"ItlwmAWDLCreateRole" = [0-9]+' | head -1 | awk '{ print $3 }')
note "Virtual-interface role macOS last asked the driver for: ${ROLE:-none} (6 is AWDL; this driver builds only that one)."
BLE=$(count_log 'process == "sharingd" AND eventMessage CONTAINS "BLE NearbyInfo"')
[ "$BLE" -gt 0 ] && ok "Bluetooth LE scanning works: $BLE nearby-device events from sharingd in the last ${MIN} min." || warn "No nearby-device events from sharingd in ${MIN} min: Bluetooth LE scanning may not be working."

head1 "1. Handoff, Universal Clipboard, iPhone calls and SMS"
for P in useractivityd rapportd identityservicesd; do running "$P" && ok "$P is running." || warn "$P isn't running."; done
UA=$(count_log 'process == "useractivityd"')
RP=$(count_log 'process == "rapportd" AND (eventMessage CONTAINS "CompanionLink" OR eventMessage CONTAINS "RPNearby" OR eventMessage CONTAINS "device changed")')
CL=$(count_log 'process == "sharingd" AND eventMessage CONTAINS "CompanionLink"')
note "useractivityd log lines: $UA; rapportd device events: $RP; sharingd CompanionLink events: $CL (last ${MIN} min)."
[ "$CL" -gt 0 ] && ok "The Mac sees a companion device (an iPhone signed in to the same Apple Account)." || note "No companion-link events yet."
note "Try: copy text on the iPhone and paste on the Mac (Universal Clipboard); open a page in Safari on the iPhone and look for the Handoff icon at the right of the Mac's Dock. Run this again afterwards."

head1 "2. Instant Hotspot / Personal Hotspot"
TH=$(count_log 'process == "sharingd" AND eventMessage CONTAINS "TetheringAgent"')
note "TetheringAgent events: $TH (last ${MIN} min). The Mac learns of the iPhone's hotspot over Bluetooth, then joins it as an ordinary Wi-Fi client."
[ "$TH" -gt 0 ] && ok "sharingd's TetheringAgent sees the iPhone." || note "No TetheringAgent events."
note "Try: with the iPhone's cellular data on and the Mac off your Wi-Fi network, open the Wi-Fi menu: a Personal Hotspot entry should list the iPhone. Join it from there."

head1 "3. AirPlay to Mac, Sidecar, Universal Control"
if netstat -an -p tcp 2>/dev/null | grep -qE '\.7000 .*LISTEN'; then ok "The AirPlay Receiver is listening (TCP 7000)."; else note "The AirPlay Receiver isn't listening: turn it on in System Settings > General > AirDrop & Handoff."; fi
running UniversalControl && ok "UniversalControl is running." || note "UniversalControl isn't running (it starts with Handoff and a second Mac or iPad)."
AP=$(count_log 'process == "airplayd" OR process == "AirPlayXPCHelper" OR process == "AirPlayUIAgent"')
note "AirPlay daemon log lines: $AP. Try: on the iPhone, Control Center > Screen Mirroring > pick this Mac."
note "Universal Control needs a second Mac or an iPad on the same Apple Account, Bluetooth and Wi-Fi on both, and Handoff on both. Sidecar needs an iPad."

head1 "4. Continuity Camera"
running ContinuityCaptureAgent && ok "ContinuityCaptureAgent is running." || warn "ContinuityCaptureAgent isn't running."
CC=$(count_log 'process == "ContinuityCaptureAgent" OR process == "ContinuityCaptureDaemon"')
note "Continuity Camera log lines: $CC. Try: open Photo Booth or FaceTime and pick the iPhone as the camera (iPhone near the Mac, unlocked)."

head1 "Link state behind all of it (AWDL)"
KEYS='Added|Channel|CountryLearned|BaPeerAdded|BaFilterOff|PromiscLive|PeerSyncFrames|DataTx|DataRxUcast|UcastOK|UcastFailed'
ioreg -l -w0 2>/dev/null | grep -oE "\"ITLAWDLCoex($KEYS)\" = [A-Za-z0-9]+" | sed 's/"ITLAWDLCoex//; s/" = /=/' | sort -u | tr '\n' ' '; echo
