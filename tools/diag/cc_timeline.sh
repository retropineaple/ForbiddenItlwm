#!/bin/bash
# Per-second timeline of the camera stream's receive loss, for A/B tests while it streams (toggle Bluetooth, move the
# router's channel, ...). Each line: time, frames seen, frames missing by sequence number, unicast data frames, llw0
# packets in, and the loss over the last interval. Read-only (ioreg), no root.
#
#   bash tools/diag/cc_timeline.sh [seconds] [outfile]        default 90 s, /var/tmp/izzy-cc-timeline.txt
#
# Then python3 -I tools/diag/cc_timeline.py can summarise; or read the file. Note the wall-clock time you changed
# something and compare the "loss%" column before and after.
SECS=${1:-90}
OUT=${2:-/var/tmp/izzy-cc-timeline.txt}
get() { ioreg -l -w0 | grep -oE "\"$1\" = [0-9]+" | head -1 | awk '{print $3}'; }
echo "# start $(date '+%F %T')  (seconds=$SECS)" > "$OUT"
printf '%-9s %9s %9s %9s %9s %7s\n' time seen missing ucast llw_in loss% >> "$OUT"
ps=0; pm=0; first=1
for ((i = 0; i < SECS; i++)); do
    R=$(ioreg -l -w0)
    s=$(echo "$R" | grep -oE '"ITLAWDLCoexSeqSeen" = [0-9]+' | head -1 | awk '{print $3}')
    m=$(echo "$R" | grep -oE '"ITLAWDLCoexSeqMissing" = [0-9]+' | head -1 | awk '{print $3}')
    u=$(echo "$R" | grep -oE '"ITLAWDLCoexDataRxUcast" = [0-9]+' | head -1 | awk '{print $3}')
    l=$(netstat -I llw0 -b 2>/dev/null | awk 'NR==2{print $5}')
    s=${s:-0}; m=${m:-0}
    if [ $first -eq 0 ]; then
        ds=$((s - ps)); dm=$((m - pm))
        if [ $((ds + dm)) -gt 0 ]; then loss=$((dm * 1000 / (ds + dm))); lossf="$((loss / 10)).$((loss % 10))"; else lossf="-"; fi
        printf '%-9s %9s %9s %9s %9s %7s\n' "$(date +%T)" "$ds" "$dm" "${u:-0}" "${l:-0}" "$lossf" >> "$OUT"
    fi
    first=0; ps=$s; pm=$m
    sleep 1
done
echo "# end $(date '+%F %T')" >> "$OUT"
echo "done: $OUT"
