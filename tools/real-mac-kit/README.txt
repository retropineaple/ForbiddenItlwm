REAL MAC REFERENCE KIT  (Continuity Camera + AirDrop)
Run everything on the REAL Mac, in Terminal, from this folder (cd into it first). Each script asks for sudo once.
Keep the iPhone unlocked, Wi-Fi + Bluetooth on, ~30 cm from the Mac, same position for every run.
Fill in system.txt in each output folder (iPhone model / iOS, Contacts-only or Everyone, same Apple ID?, same router as the iPhone?).

A. CONTINUITY CAMERA
 1. Open Photo Booth, pick the iPhone as camera, let video run ~30 s.
    zsh collect.sh run-A          (Wi-Fi joined to the router)
 2. Turn Wi-Fi on but do not join a network, camera running again:
    zsh collect.sh run-B
 3. Camera running; stop it, wait 10 s, start it again so a fresh start is inside the log window, wait 30 s:
    zsh collect2.sh               (optional: zsh collect2.sh "Your iPhone name"  - needs paired, unlocked, developer-mode phone)
 Also note: Mac model, Wi-Fi chip, router channel/width (system_profiler SPAirPortDataType is saved for you).

B. AIRDROP  (start the script FIRST, then do the transfer inside its 40 s window)
 1. zsh collect_airdrop.sh mac-to-iphone    AirDrop sheet open on the Mac, send a photo Mac -> iPhone, accept on the iPhone
 2. zsh collect_airdrop.sh iphone-to-mac    send a photo iPhone -> Mac, accept on the Mac
 3. zsh collect_airdrop.sh idle-discovery   open Finder > AirDrop, wait 40 s, send nothing

C. OVER-THE-AIR CAPTURE (needs a SECOND Mac as sniffer; most valuable item)
 On the second Mac: Wireless Diagnostics (hold Option, click the Wi-Fi menu > Open Wireless Diagnostics),
 menu Window > Sniffer, channel 161, width 80 MHz, Start. Do it again with channel 149.
 While it runs, on the real Mac: (1) start Continuity Camera cold, let it stream 20 s; (2) separately, do an AirDrop transfer.
 Stop the sniffer and send the .pcap files from /var/tmp (the Sniffer window shows the path).

Send back: every ~/Desktop/real-mac-*.zip plus the sniffer .pcap files.
