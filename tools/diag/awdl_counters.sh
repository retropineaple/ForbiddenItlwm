#!/bin/bash
# Log the AWDL driver's registry counters and awdl0's byte counters, one line per sample, to find out what the
# link was doing at a given moment. Read-only, needs no sudo.
#
#   bash tools/diag/awdl_counters.sh [seconds=300] [interval=1.5] > series.txt
#
# Each line: clock time, then name=value pairs (ITLAWDLCoex* keys without the prefix), then awdl0's packet and
# byte counters. docs/DEBUGGING.md explains each key. Line up the clock times with `log show` output and with a
# packet capture (tools/diag/awdl_flow.py prints clock times too).
set -u
DURATION=${1:-300}
INTERVAL=${2:-1.5}
END=$(( $(date +%s) + DURATION ))
KEYS='BaFilterOff|PromiscLive|BaPeerAdded|BaTxStateName|DataTx|DataRxUcast|DataRxGroup|UcastOK|UcastFailed|GroupOK|GateHolds|GateForced|GateAwayTicks|GateDozeTicks|PeerSyncFrames|PeerPauses|PeerSequenceChanges|BaTxFrames|BaTxAcked|BaSessions|BaRxIdleTeardowns|BaFilterChanges|MgmtFailed|BaAskFailed'
while [ "$(date +%s)" -lt "$END" ]; do
    VALUES=$(ioreg -l -w0 | grep -oE "\"ITLAWDLCoex($KEYS)\" = [A-Za-z0-9\"]+" | sed 's/"ITLAWDLCoex//; s/" = /=/' | sort -u | tr '\n' ' ')
    NET=$(netstat -I awdl0 -b 2>/dev/null | awk 'NR==2 { print "Ipkts=" $5 " Ibytes=" $7 " Opkts=" $8 " Obytes=" $10 }')
    echo "$(date +%T) $VALUES $NET"
    sleep "$INTERVAL"
done
