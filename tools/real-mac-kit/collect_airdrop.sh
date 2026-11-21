#!/bin/zsh
# AirDrop reference collection from a real Mac (read-only except the capture files and the zip).
# Run it THREE times, in this order, each with the iPhone unlocked, Wi-Fi + Bluetooth on, 30 cm from the Mac:
#   zsh collect_airdrop.sh mac-to-iphone      # AirDrop sheet open on the Mac, send a photo Mac -> iPhone, accept on the iPhone
#   zsh collect_airdrop.sh iphone-to-mac      # send a photo from the iPhone to this Mac, accept on the Mac
#   zsh collect_airdrop.sh idle-discovery     # open the Mac's AirDrop sheet (Finder > AirDrop) and wait 40 s, send nothing
# Start the script FIRST, then do the transfer within the 40 s capture window.
set -u
NAME=${1:-mac-to-iphone}
OUT=~/Desktop/real-mac-airdrop-$NAME-$(date +%Y%m%d-%H%M%S)
mkdir -p "$OUT"
sudo -v || exit 1

{ sw_vers; sysctl -n hw.model; date; echo "iPhone model / iOS (fill in): "; echo "Contacts only or Everyone (fill in): "; echo "Same Apple ID? (fill in): "; } > "$OUT/system.txt"

# state before
{ ifconfig awdl0; ifconfig llw0; ifconfig en0; } > "$OUT/ifconfig_before.txt" 2>&1
ndp -an > "$OUT/ndp_before.txt" 2>&1
netstat -rn -f inet6 > "$OUT/routes6_before.txt" 2>&1
sudo wdutil info > "$OUT/wdutil_before.txt" 2>&1

# captures: awdl0 (what the stack sends/receives), llw0, and the Wi-Fi interface, all 40 s, headers + 256 bytes
sudo tcpdump -i awdl0 -s 256 -nn -B 8192 -w "$OUT/awdl0.pcap" -G 40 -W 1 > "$OUT/tcpdump_awdl0.txt" 2>&1 &
sudo tcpdump -i llw0 -s 256 -nn -B 8192 -w "$OUT/llw0.pcap" -G 40 -W 1 > "$OUT/tcpdump_llw0.txt" 2>&1 &
sudo tcpdump -i en0 -s 256 -nn -B 8192 -w "$OUT/en0.pcap" -G 40 -W 1 > "$OUT/tcpdump_en0.txt" 2>&1 &
sudo sh -c "while :; do date; ndp -an | grep -i awdl; sleep 1; done" > "$OUT/ndp_timeline.txt" 2>&1 &
NP=$!
echo "Capturing 40 s. DO THE TRANSFER NOW."
sleep 43
sudo kill $NP 2>/dev/null
sleep 1
sudo chmod a+r "$OUT"/*.pcap 2>/dev/null

# state after
{ ifconfig awdl0; ifconfig llw0; ifconfig en0; } > "$OUT/ifconfig_after.txt" 2>&1
ndp -an > "$OUT/ndp_after.txt" 2>&1
netstat -s -p tcp > "$OUT/netstat_tcp.txt" 2>&1
netstat -I awdl0 -b > "$OUT/netstat_awdl0.txt" 2>&1
netstat -I llw0 -b > "$OUT/netstat_llw0.txt" 2>&1
sudo ioreg -l -w0 2>/dev/null | grep -iE 'awdl|airdrop|sharing' > "$OUT/ioreg_awdl.txt"

echo "Logs (last 3 min)..."
/usr/bin/log show --last 3m --info --debug --predicate \
  '(process == "sharingd" OR process == "rapportd" OR process == "wifip2pd" OR process == "mDNSResponder" OR process == "bluetoothd" OR process == "nearbyd" OR process == "identityservicesd" OR process == "airportd")' \
  > "$OUT/daemons_all.log" 2>&1
/usr/bin/log show --last 3m --info --debug --predicate \
  'process == "kernel" AND (eventMessage CONTAINS "AWDL" OR eventMessage CONTAINS "awdl" OR eventMessage CONTAINS "Peer" OR eventMessage CONTAINS "ndp" OR eventMessage CONTAINS "NDP" OR eventMessage CONTAINS "presence" OR eventMessage CONTAINS "UMI" OR eventMessage CONTAINS "channel sequence" OR eventMessage CONTAINS "SYNC_INFO" OR eventMessage CONTAINS "TLV")' \
  > "$OUT/kernel_awdl.log" 2>&1

# a few cheap greps so I can see at a glance what the daemons did
{
  echo "== sharingd AirDrop lines";  grep -iE 'airdrop|SDAirDrop|Discover|Browse|Handshake|Ask|Upload|Accept' "$OUT/daemons_all.log" | grep -i sharingd | head -300
  echo "== rapportd pairing";        grep -iE 'PairVerify|Pairing|PAKE|PIN|AdHoc' "$OUT/daemons_all.log" | grep -i rapportd | head -200
  echo "== wifip2pd";                grep -i wifip2pd "$OUT/daemons_all.log" | head -300
  echo "== mDNSResponder awdl";      grep -iE 'awdl|airdrop' "$OUT/daemons_all.log" | grep -i mDNSResponder | head -200
} > "$OUT/summary.txt" 2>&1

cd ~/Desktop && zip -rq "$(basename "$OUT").zip" "$(basename "$OUT")"
echo "Done: $OUT"
echo "Zip:  ~/Desktop/$(basename "$OUT").zip"
ls -la "$OUT"/*.pcap
