#!/bin/bash
# One Continuity Camera attempt, captured from every side that matters for llw0.
#
#   sudo bash tools/diag/llw_attempt.sh [seconds=50] [output folder]
#
# Start it, wait for the line that says NOW, then pick the iPhone in Photo Booth. It records, for the whole run:
#   dtrace.txt        the kernel netif path and the driver's llw0 callbacks (trace_llw_netif.d)
#   llw0.pcap         what the kernel stack sees on llw0
#   awdl0.pcap        what it sees on awdl0 (no mDNS)
#   skywalk_*_N.txt   Skywalk's own flow, channel, flow-owner, flow-switch and interface tables, three times
#   registry_*.txt    the driver's ItlwmLLW* counters and netstat before and after
#   log.txt           the camera agent, rapportd, wifip2pd and kernel lines about llw0 / the stream
# Everything is handed back to the user who ran sudo. Nothing is sent anywhere and nothing is changed.
# Needs root for dtrace, tcpdump and skywalkctl. Stops by itself; no Ctrl-C needed.
set -u
DUR=${1:-50}
[ "$(id -u)" = 0 ] || { echo "Run it with sudo: sudo bash $0 [seconds] [folder]"; exit 1; }
here="$(cd "$(dirname "$0")" && pwd)"
OUT=${2:-/var/tmp/izzy-llw-$(date +%H%M%S)}
mkdir -p "$OUT"
START="$(date '+%Y-%m-%d %H:%M:%S')"

snapshot() { # <label>
    # skywalkctl flow can hang indefinitely on this Mac; bound each call.
    for c in flow channel flow-owner flow-switch interface; do
        ( skywalkctl "$c" >"$OUT/skywalk_${c}_$1.txt" 2>&1 ) &
        local spid=$!
        local i=0
        while kill -0 "$spid" 2>/dev/null; do
            if [ "$i" -ge 8 ]; then
                kill -TERM "$spid" 2>/dev/null
                sleep 1
                kill -KILL "$spid" 2>/dev/null
                echo "skywalkctl $c timed out" >>"$OUT/skywalk_${c}_$1.txt"
                break
            fi
            sleep 1
            i=$((i + 1))
        done
        wait "$spid" 2>/dev/null || true
    done
}
registry() { # <label>
    { ioreg -l -w0 | grep -E 'ItlwmLLW|ItlwmAWDLUmiAckPosted|ITLAWDLCoex(DataTx"|DataRxUcast"|Channel|OwnWidth|Peer5GHz|PeerPresent|PeerSequence|Schedule|Cam|LowLatency|ContinuityPower|FollowsInfra|Seq|Gate|Ba|RxReject)|ITLAWDLRx(Mpdu|Dup|Reorder|BadCrc)' | sed 's/^[ |]*//' | sort -u
      echo "---"
      netstat -I llw0 -b; netstat -I awdl0 -b
      echo "---"
      system_profiler SPAirPortDataType 2>/dev/null | awk '/Current Network Information:/{p=1} p&&/Channel:/{print; exit}'
    } >"$OUT/registry_$1.txt" 2>&1
}

registry before
tcpdump -i llw0 -n -U -w "$OUT/llw0.pcap" >"$OUT/tcpdump_llw0.txt" 2>&1 &
P1=$!
tcpdump -i awdl0 -n -U -w "$OUT/awdl0.pcap" 'ip6 and not udp port 5353' >"$OUT/tcpdump_awdl0.txt" 2>&1 &
P2=$!
dtrace -Z -q -s "$here/trace_llw_netif.d" -c "sleep $DUR" -o "$OUT/dtrace.txt" &
P3=$!
sleep 3
echo
echo "  >>> NOW: in Photo Booth, pick the iPhone from the Camera menu. Recording for $DUR s. <<<"
echo
sleep $((DUR / 3)); snapshot 1
sleep $((DUR / 3)); snapshot 2
wait "$P3" 2>/dev/null          # dtrace ends when its sleep does
snapshot 3
kill -TERM "$P1" "$P2" 2>/dev/null   # SIGTERM: a background job here ignores SIGINT
wait "$P1" "$P2" 2>/dev/null
registry after

/usr/bin/log show --start "$START" --style compact --predicate \
    'process == "ContinuityCaptureAgent" || process == "rapportd" || process == "wifip2pd" || (process == "kernel" && eventMessage CONTAINS "llw0")' \
    >"$OUT/log.txt" 2>&1

if [ -n "${SUDO_USER:-}" ]; then chown -R "$SUDO_USER" "$OUT"; fi
chmod -R a+rX "$OUT"
echo "Done. Files are in $OUT"
ls -la "$OUT" | awk '{print "  " $5 "\t" $9}' | tail -n +4
