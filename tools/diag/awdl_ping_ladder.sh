#!/bin/bash
# Is there a packet size below awdl0's MTU that the link silently drops? Pings the iPhone with growing payloads, in
# shuffled order over several rounds, so a size-dependent limit can be told apart from a short outage (the iPhone
# is only on our channel part of the time, so a single ordered ladder can lose a few steps to timing alone).
#
#   bash tools/diag/awdl_ping_ladder.sh fe80::xxxx:xxxx:xxxx:xxxx%awdl0      (use `ndp -an | grep awdl0` for the address)
#
# Payload + 48 is the IPv6 packet size (40 header + 8 ICMPv6). 1436 is the largest payload that fits awdl0's 1484.
# Run it with the iPhone awake. ping6 allows fragmentation, so sizes above 1436 pass; they don't test the MTU.
# A real size limit shows as the same sizes failing in every round. Failing sizes that move around are timing.
# Needs bash, not zsh: the size lists below are split into words.
PEER=${1:?usage: awdl_ping_ladder.sh <iphone link-local address>%awdl0}
ROUNDS=("1436 1380 1400 1420 1200 1430 56" "1400 1430 56 1436 1420 1380 1200" "1380 1200 1436 1400 56 1420 1430")
RESULTS=$(mktemp)
ping6 -c 3 -i 0.5 -I awdl0 "$PEER" > /dev/null 2>&1   # wake the link first
for ROUND in "${ROUNDS[@]}"; do
    for SIZE in $ROUND; do
        GOT=$(ping6 -c 8 -i 0.25 -s "$SIZE" -I awdl0 "$PEER" 2>&1 | grep -oE '[0-9]+ packets received' | awk '{ print $1 }')
        echo "$SIZE ${GOT:-0}" >> "$RESULTS"
    done
    echo round >> "$RESULTS"
done
echo "payload  IP size   replies of 8, one column per round"
for SIZE in 56 1200 1380 1400 1420 1430 1436; do
    printf '%5d  %5d    ' "$SIZE" $((SIZE + 48))
    awk -v s="$SIZE" '$1 == s { printf "%s  ", $2 }' "$RESULTS"
    echo
done
rm -f "$RESULTS"
