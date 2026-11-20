#!/bin/zsh
# Second collection from the real Mac: the family's UMI timer lines (which channel, slot and delay the UMI goes out with), the RTG
# exchange, optionally the connected iPhone's own log for the same minutes, and a Wi-Fi state dump.
#
#   1. Start Continuity Camera (Photo Booth, the iPhone as camera), wait ~30 s so the stream is flowing.
#   2. Better: stop the camera, wait 10 s, start it again, so a fresh RTG start is inside the window.
#   3. zsh collect2.sh                (add the iPhone's name as an argument to also pull its log:  zsh collect2.sh "Izzy's iPhone")
set -u
OUT=~/Desktop/real-mac-awdl-2-$(date +%Y%m%d-%H%M%S)
mkdir -p "$OUT"
sudo -v || exit 1

{ sw_vers; sysctl -n hw.model; date; echo "iPhone model / iOS (fill in): "; } > "$OUT/system.txt"
sudo wdutil info > "$OUT/wdutil_info.txt" 2>&1
sudo wdutil dump > "$OUT/wdutil_dump.txt" 2>&1

echo "Kernel log (UMI timer, AW and RTG lines), last 6 minutes..."
/usr/bin/log show --last 6m --info --debug --predicate \
  'process == "kernel" AND (eventMessage CONTAINS "UMI" OR eventMessage CONTAINS "Slot:" OR eventMessage CONTAINS "AW:" OR eventMessage CONTAINS "RTG" OR eventMessage CONTAINS "RealTime" OR eventMessage CONTAINS "AWDL ON" OR eventMessage CONTAINS "channel sequence" OR eventMessage CONTAINS "Ensemble" OR eventMessage CONTAINS "processUnicast" OR eventMessage CONTAINS "actionFrameReport" OR eventMessage CONTAINS "sendUnicastMI" OR eventMessage CONTAINS "buildMasterIndication" OR eventMessage CONTAINS "monitorAWEvents" OR eventMessage CONTAINS "SANITY")' \
  > "$OUT/kernel_umi.log" 2>&1

echo "Daemon log (wifip2pd, rapportd), last 6 minutes..."
/usr/bin/log show --last 6m --info --debug --predicate \
  '(process == "wifip2pd" OR process == "rapportd" OR process == "sharingd") AND (eventMessage CONTAINS[c] "awdl" OR eventMessage CONTAINS[c] "rtg" OR eventMessage CONTAINS[c] "umi" OR eventMessage CONTAINS[c] "ensemble" OR eventMessage CONTAINS[c] "traffic reg")' \
  > "$OUT/daemons_umi.log" 2>&1

if [ $# -ge 1 ]; then
  echo "iPhone log (USB, paired, unlocked)..."
  /usr/bin/log collect --device-name "$1" --last 6m --output "$OUT/iphone.logarchive" > "$OUT/iphone_collect.txt" 2>&1 \
    || echo "iPhone log collection failed, see iphone_collect.txt"
fi

cd ~/Desktop && zip -rq "$(basename "$OUT").zip" "$(basename "$OUT")"
echo "Done: $OUT"
echo "Zip:  ~/Desktop/$(basename "$OUT").zip"
wc -l "$OUT"/*.log 2>/dev/null
