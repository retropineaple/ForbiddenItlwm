#!/bin/bash
# AirDrop transfer speed and connect times, from macOS's own AirDrop log (sharingd). Read-only: it
# changes nothing, needs no sudo and no sysdiagnose, and sends nothing anywhere.
#
#   bash airdrop_measure.sh            watch live; send or receive files, press Ctrl-C to stop
#   bash airdrop_measure.sh --last 2h  transfers from the last 2 hours (any log show time: 30m, 1d, ...)
#   bash airdrop_measure.sh --csv FILE also append one line per transfer to FILE
#
# For each transfer it prints the direction, the size, the speed macOS measured, and the connect phases:
# - Hackintosh -> device: macOS's own send timings (HELLO and ASK phases, the ASK includes the time the device
#   takes to accept), and the total from the start of the send to the end. macOS names the kind of device
#   it sent to (iPhone, iPad, Mac; a Mac is a real Mac or another hackintosh).
# - device -> Hackintosh: from the first connection the device opens to its ASK, and from the ASK to the end of
#   the data. The moment you tap this Mac on the device isn't visible here, so the time before the
#   first connection isn't included. macOS doesn't log the kind of device that sent, so it shows as
#   "device" until a send from this Mac in the same run has gone to the same driver slot.
# With the AirDrop build running, the device's driver slot follows its name, e.g. "iPhone (slot 0)".
# When ForbiddenItlwm's AirDrop build is running (live mode only), it also shows after each transfer:
# the driver slot the device used, the channel and width its frames came in on, the Wi-Fi network's
# channel and width, macOS's AirDrop schedule (16 slots, 0 = AirDrop off) and the listener's state.

set -u
# Byte-wise text handling: under a Japanese or Chinese locale awk printed "towc: multibyte conversion
# failure" for some log lines (#21). The other compat scripts do the same.
export LC_ALL=C
MODE=stream
LAST=""
CSV=""
while [ $# -gt 0 ]; do
    case "$1" in
        --last) MODE=show; LAST="${2:-1h}"; shift 2 ;;
        --csv) CSV="${2:-}"; shift 2 ;;
        -h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1 (try --help)"; exit 1 ;;
    esac
done

PRED='process == "sharingd" AND (category == "AirDropNW" OR category == "Metrics")'

driver_state() {
    # One line of the listener's state, or nothing when the AirDrop build isn't running.
    { ioreg -l -w0 -d 1 -k ITLAWDLCoexChannel; ioreg -l -w0 -r -c ItlAWDLInterface -d 1; } 2>/dev/null | awk -F' = ' '
        /"ITLAWDLCoexChannel"/ {ch=$2} /"ITLAWDLCoexOwnWidth"/ {w=$2} /"ITLAWDLCoexBaPeerWidth"/ {pw=$2}
        /"ITLAWDLCoexMultiState"/ {ms=$2; gsub(/"/,"",ms)}
        /"ITLAWDLCoexBaTxStateName"/ {gsub(/"/,"",$2); ba=$2}
        /"ITLAWDLCoexPeerPresentEntries"/ {pe=$2}
        /"ItlwmAWDLTxDropped"/ {dr=$2}
        /"ITLAWDLSchedStatus"/ {ss=$2; gsub(/"/,"",ss)}
        /"ITLAWDLCoexPeers"/ {cp=$2}
        END { # Block Ack held by a later peer slot (-itlawdlmultipeer) shows as "agreed (slot N)".
              if (ba != "agreed" && match(cp, /[0-9]+:[0-9]+ up [0-9a-f]+ A/)) {
                  slot = substr(cp, RSTART, RLENGTH); sub(/:.*/, "", slot); ba = "agreed (slot " slot ")" }
              # The scheduler, and loudly when it has fallen back to the firmware timing.
              sched = (ss == "") ? "" : (ss ~ /^on/ ? ", scheduler on" : (ss ~ /^off/ ? ", scheduler off" : ", SCHEDULER " ss))
              # Sharing the Wi-Fi channel (no -itlawdlcoexmulti, or its fallback), the listener has no width
              # of its own (OwnWidth reads -1); the width of the peer station is the one that counts there.
              shared = (ms == "off" || ms == "fallback-same" || w + 0 < 0 || w + 0 > 1000)
              if (shared) w = (pw == "" || pw + 0 == 0) ? "?" : pw
              if (ch != "") printf "listener %s/%s MHz%s, block ack %s, peer on our channel %s/16 slots, TX dropped %s%s\n", ch, w, (shared ? " (shared with Wi-Fi)" : ""), (ba==""?"-":ba), (pe==""?"-":pe), (dr==""?"-":dr), sched }'
}
export -f driver_state 2>/dev/null

peer_traffic() {
    # ITLAWDLPeerTraffic: "wifi CH/W; slot N: tx X rx Y, rx on CH/W, station W; ...", or nothing.
    ioreg -l -w0 -d 1 -k ITLAWDLPeerTraffic 2>/dev/null | awk -F'" = "' '/"ITLAWDLPeerTraffic"/ {sub(/"$/, "", $2); print $2; exit}'
}
export -f peer_traffic 2>/dev/null

mac_schedule() {
    # macOS's AirDrop plan and its own channel and role, from the scheduler (ITLAWDLSchedPlan, ITLAWDLSchedMacChan).
    ioreg -l -w0 -d 1 2>/dev/null | awk -F' = ' '
        /"ITLAWDLSchedPlan"/ {pl=$2; gsub(/"/, "", pl); sub(/.*; /, "", pl)}
        /"ITLAWDLSchedMacChan"/ {mc=$2; gsub(/"/, "", mc); sub(/;.*/, "", mc); sub(/, listener.*/, "", mc); sub(/^macOS /, "", mc)}
        END { if (pl != "") printf "macOS schedule: %s; its channel %s\n", pl, (mc == "" ? "?" : mc) }'
}
export -f mac_schedule 2>/dev/null

if [ -n "$CSV" ] && [ ! -s "$CSV" ]; then
    echo "time,direction,bytes,MBps,connect_ms,ask_ms,data_s,total_s,transport,device,slot,rx_on,wifi" > "$CSV"
fi

echo "AirDrop transfer measurements (Ctrl-C to stop)"
[ "$MODE" = show ] && echo "Reading the log from the last $LAST ..."
state="$(driver_state)"
[ -n "$state" ] && echo "Driver now: $state"
echo

if [ "$MODE" = show ]; then
    SRC=(/usr/bin/log show --last "$LAST" --style compact --predicate "$PRED")
else
    SRC=(/usr/bin/log stream --style compact --level default --predicate "$PRED")
fi

"${SRC[@]}" 2>/dev/null | awk -v csv="$CSV" -v live="$([ "$MODE" = stream ] && echo 1 || echo 0)" '
function ms(ts,   p, h, m, s) {   # "2026-11-12 17:43:56.519" -> milliseconds of the day
    split(ts, p, " "); split(p[2], h, ":")
    return ((h[1] * 60 + h[2]) * 60 + h[3]) * 1000
}
function field(name,   r) {       # a numeric or string field of a MetricEvent line
    if (match($0, "\"" name "\" : \"?[^,\"}]*")) {
        r = substr($0, RSTART, RLENGTH); sub(/.*: "?/, "", r); return r
    }
    return ""
}
function mb(bps) { return sprintf("%.1f", bps / 1000000) }
function run(name,   cmd, line) {
    if (!live) return ""
    cmd = "bash -c " name; line = ""; cmd | getline line; close(cmd); return line
}
function state() { return run("driver_state") }
# ITLAWDLPeerTraffic into arrays by slot; returns the Wi-Fi part ("36/80" or "-").
function parse(str, tx, rx, on, sta,   n, part, i, k, w) {
    split("", tx); split("", rx); split("", on); split("", sta); w = ""
    n = split(str, part, "; ")
    for (i = 1; i <= n; i++) {
        if (part[i] ~ /^wifi /) { w = substr(part[i], 6); continue }
        if (!match(part[i], /^slot [0-9]+:/)) continue
        k = substr(part[i], 6, RLENGTH - 6) + 0
        if (match(part[i], /tx [0-9]+/)) tx[k] = substr(part[i], RSTART + 3, RLENGTH - 3) + 0
        if (match(part[i], /rx [0-9]+/)) rx[k] = substr(part[i], RSTART + 3, RLENGTH - 3) + 0
        if (match(part[i], /rx on [0-9]+\/[0-9]+/)) on[k] = substr(part[i], RSTART + 6, RLENGTH - 6)
        if (match(part[i], /station [0-9]+/)) sta[k] = substr(part[i], RSTART + 8, RLENGTH - 8)
    }
    return w
}
# The slot whose count (tx for a send, rx for a receive) grew most between two readings; -1 when none
# grew by at least 20 bulk frames. A count that went down was reset when the slot was added again.
function busiest(before, after, dir,   bt, br, bo, bs, at, ar, ao, as, k, d, best, bestd) {
    have_traffic = (after != ""); if (!have_traffic) return -1
    wifi_now = parse(after, at, ar, ao, as)
    if (before == "") return -1
    parse(before, bt, br, bo, bs)
    best = -1; bestd = 19
    if (dir == "tx") {
        for (k in at) { d = (k in bt && at[k] >= bt[k]) ? at[k] - bt[k] : at[k]; if (d > bestd) { bestd = d; best = k } }
    } else {
        for (k in ar) { d = (k in br && ar[k] >= br[k]) ? ar[k] - br[k] : ar[k]; if (d > bestd) { bestd = d; best = k } }
    }
    if (best >= 0) { slot_on = (best in ao) ? ao[best] : "?"; slot_sta = (best in as) ? as[best] : "?" }
    return best
}
function slotline(slot,   t) {
    if (!live || !have_traffic) return ""
    t = (slot < 0) ? "slot unknown" : sprintf("its frames on %s MHz, its station %s MHz", (slot_on == "0/0" ? "-" : slot_on), slot_sta)
    return t sprintf("; Wi-Fi %s%s", (wifi_now == "" || wifi_now == "-") ? "not joined" : wifi_now, (wifi_now == "" || wifi_now == "-") ? "" : " MHz")
}
{
    ts = $1 " " $2; t = ms(ts)
}
# device -> Hackintosh milestones
/Found BLE device/                 { if (!rx_conn) rx_ble = t }
/AirDrop connection preparing/     { if (!rx_conn) { rx_conn = t; rx_before = run("peer_traffic") } }
/Received ASK request/             { if (!rx_ask) rx_ask = t }
/AirDrop receive performance/      { rx_end = t }
# Hackintosh -> device
/MetricEvent .com\.apple\.sharing\.airdrop\.send-start/ { tx_start = t; tx_before = run("peer_traffic"); d = field("receiverDeviceType"); if (d != "") tx_kind = d }
/MetricEvent .com\.apple\.sharing\.airdrop\.send-finish/ {
    tx_end = t; tx_size = field("size"); tx_transport = field("transport")
    d = field("receiverDeviceType"); if (d != "") tx_kind = d
    tx_hello = field("helloFinishedMS"); tx_ask = field("askFinishedMS"); tx_result = field("result")
}
/MetricEvent .com\.apple\.sharing\.AirDrop\.SessionInfo/ {
    d = field("firstDiscoveryMs")
    if (d != "") printf "%s  share sheet found the first device after %d ms\n", ts, d
}
/MetricEvent .com\.apple\.sharing\.AirDrop\.TransferResult/ {
    bps = field("bytesPerSec"); ok = field("success"); sender = field("sender")
    st = state(); after = run("peer_traffic"); sched = run("mac_schedule")
    slot_on = slot_sta = ""; wifi_now = ""
    if (sender == "1") {
        kind = (tx_kind == "") ? "device" : tx_kind
        slot = busiest(tx_before, after, "tx"); if (slot >= 0) slot_kind[slot] = kind
        total = (tx_start && tx_end >= tx_start) ? (tx_end - tx_start) / 1000 : 0
        data = (bps > 0 && tx_size > 0) ? tx_size / bps : 0
        printf "%s  Hackintosh -> %s  %s  %.1f MB at %s MB/s via %s\n", ts, kind (slot >= 0 ? " (slot " slot ")" : ""), (ok == "true" ? "OK    " : "FAILED"), tx_size / 1000000, mb(bps), (tx_transport == "" ? "?" : tx_transport)
        printf "                         connect: HELLO %s ms, ASK %s ms (includes the %s accepting); data about %.1f s; total %.1f s\n", (tx_hello == "" ? "?" : tx_hello), (tx_ask == "" ? "?" : tx_ask), kind, data, total
        sl = slotline(slot)
        if (csv != "") printf "%s,hackintosh_to_device,%s,%s,%s,%s,%.1f,%.1f,%s,%s,%s,%s,%s\n", ts, tx_size, mb(bps), tx_hello, tx_ask, data, total, tx_transport, kind, (slot < 0 ? "" : slot), slot_on, wifi_now >> csv
        tx_start = tx_end = 0; tx_size = ""; tx_kind = ""; tx_before = ""
    } else {
        slot = busiest(rx_before, after, "rx")
        kind = (slot >= 0 && (slot in slot_kind)) ? slot_kind[slot] : "device"
        bucket = field("totalBytesBucket")
        conn = (rx_conn && rx_ask >= rx_conn) ? rx_ask - rx_conn : -1
        data = (rx_ask && rx_end >= rx_ask) ? (rx_end - rx_ask) / 1000 : 0
        printf "%s  %s -> Hackintosh  %s  up to %s at %s MB/s\n", ts, kind (slot >= 0 ? " (slot " slot ")" : ""), (ok == "true" ? "OK    " : "FAILED"), bucket, mb(bps)
        printf "                         connect: first connection to ASK %s ms; ASK to end of data %.1f s\n", (conn < 0 ? "?" : conn), data
        sl = slotline(slot)
        if (csv != "") printf "%s,device_to_hackintosh,%s,%s,%s,,%.1f,,awdl,%s,%s,%s,%s\n", ts, bucket, mb(bps), (conn < 0 ? "" : conn), data, kind, (slot < 0 ? "" : slot), slot_on, wifi_now >> csv
        rx_ble = rx_conn = rx_ask = rx_end = 0; rx_before = ""
    }
    if (sl != "") printf "                         %s\n", sl
    if (sched != "") printf "                         %s\n", sched
    if (st != "") printf "                         %s\n", st
    print ""
    fflush()
}'
