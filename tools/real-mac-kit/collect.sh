#!/bin/zsh
# Collects what is needed to compare a real Mac's Continuity Camera session with the hackintosh: kernel and daemon log lines
# about AWDL / RTG, a 20 s llw0 capture, and the Wi-Fi state. Read-only except for the capture file it writes.
#
#   1. On this Mac, open Photo Booth, pick the iPhone as the camera, and let the video run.
#   2. In Terminal:  zsh collect.sh run-A     (run-B for the second run, Wi-Fi on but not joined to a network)
#   3. Send me the folder it prints (it is also zipped).
set -u
NAME=${1:-run-A}
OUT=~/Desktop/real-mac-awdl-$NAME-$(date +%Y%m%d-%H%M%S)
HERE=${0:A:h}
mkdir -p "$OUT"

echo "Keep the camera streaming. Asking for sudo (needed for the capture and wdutil)."
sudo -v || exit 1

{
  echo "== sw_vers";        sw_vers
  echo "== hw.model";       sysctl -n hw.model
  echo "== cpu";            sysctl -n machdep.cpu.brand_string 2>/dev/null
  echo "== date";           date
  echo "== note: fill in below by hand and re-save if you like"
  echo "iPhone model / iOS: "
  echo "Same router as the iPhone? (yes/no): "
  echo "Also on Ethernet? (yes/no): "
} > "$OUT/system.txt"

echo "Capturing 20 s on llw0 (camera must be streaming)..."
sudo tcpdump -i llw0 -s 160 -nn -B 8192 -w "$OUT/llw0.pcap" -G 20 -W 1 > "$OUT/tcpdump.txt" 2>&1
sudo chmod a+r "$OUT/llw0.pcap" 2>/dev/null

ifconfig awdl0 llw0 > "$OUT/ifconfig.txt" 2>&1
netstat -I llw0 -b > "$OUT/netstat_llw0.txt" 2>&1
sudo wdutil info > "$OUT/wdutil_info.txt" 2>&1
system_profiler SPAirPortDataType > "$OUT/airport.txt" 2>&1

echo "Collecting the kernel log (10 minutes)..."
/usr/bin/log show --last 10m --info --debug --predicate \
  'process == "kernel" AND (eventMessage CONTAINS "RTG" OR eventMessage CONTAINS "RealTime" OR eventMessage CONTAINS "UltraLowLatency" OR eventMessage CONTAINS "CoPresence" OR eventMessage CONTAINS "AWDL ON" OR eventMessage CONTAINS "AWDLStateDump" OR eventMessage CONTAINS "Cached(" OR eventMessage CONTAINS "setScheduleState" OR eventMessage CONTAINS "awdlCATsPolicy" OR eventMessage CONTAINS "setAwdlCtl" OR eventMessage CONTAINS "composeRTG" OR eventMessage CONTAINS "channel sequence" OR eventMessage CONTAINS "actionFrameReport")' \
  > "$OUT/kernel_awdl.log" 2>&1

echo "Collecting daemon log lines..."
/usr/bin/log show --last 10m --info --debug --predicate \
  '(process == "wifip2pd" OR process == "rapportd" OR process == "sharingd" OR process == "mediaremoted" OR process == "avconferenced") AND (eventMessage CONTAINS[c] "awdl" OR eventMessage CONTAINS[c] "rtg" OR eventMessage CONTAINS[c] "ultra" OR eventMessage CONTAINS[c] "copresence" OR eventMessage CONTAINS[c] "continuity camera" OR eventMessage CONTAINS[c] "p2p")' \
  > "$OUT/daemons.log" 2>&1

ioreg -l -w0 2>/dev/null | grep -iE 'awdl|rtg' > "$OUT/ioreg_awdl.txt"

if [ -f "$HERE/llw0_rtp.py" ] && command -v python3 >/dev/null; then
  python3 -I "$HERE/llw0_rtp.py" "$OUT/llw0.pcap" > "$OUT/rtp_analysis.txt" 2>&1
fi

cd ~/Desktop && zip -rq "$(basename "$OUT").zip" "$(basename "$OUT")"
echo
echo "Done: $OUT"
echo "Zip:  ~/Desktop/$(basename "$OUT").zip"
echo "Log sizes:"; wc -l "$OUT"/*.log "$OUT"/rtp_analysis.txt 2>/dev/null
