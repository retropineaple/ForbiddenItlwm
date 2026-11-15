/*
 * Continuity Camera: why does the family wait 3 s for EnsembleUMIStatusDone (event 9)?
 *
 * Known (trace_umi.d, 2026-11-15 17:44): handleEnsembleTrafficReg calls sendRealTimeUMI (umi 0xb000000), logs
 * "addPeerToUmiChain: Not Adding Peer to UMI chain, waitingOnChain:1", sets setEnsembleUMIStatus(1, 0x3e60), then
 * waits 3000 ms for event 9. No UMI is ever sent to the driver (no AWDL_UMI_TX_PEER, no setAWDLUMIToPeer).
 * This shows who marks the peer as waiting on the chain, whether the UMI timer is armed, and whether the
 * send / status handlers run, in order, with one line per call and a short stack for the state changes.
 *
 *   sudo dtrace -Z -e -s tools/diag/trace_umi_chain.d                       # compile check
 *   sudo dtrace -Z -q -s tools/diag/trace_umi_chain.d -o /tmp/pc/chain.out  # run, start the camera, Ctrl-C
 * Foreground only (a background shell ignores Ctrl-C).
 */
#pragma D option quiet
#pragma D option stackframes=8

fbt:com.apple.iokit.IO80211Family:*IO80211AWDLPeer17setWaitingOnChain*:entry
{
    printf("%Y setWaitingOnChain(%d)\n", walltimestamp, (int)arg1);
    stack(8);
}

fbt:com.apple.iokit.IO80211Family:*IO80211AWDLPeer17addPeerToUmiChain*:entry
{
    printf("%Y addPeerToUmiChain\n", walltimestamp);
    stack(6);
}

fbt:com.apple.iokit.IO80211Family:*IO80211AWDLPeer16isWaitingOnChain*:return
{
    printf("%Y isWaitingOnChain -> %d\n", walltimestamp, (int)arg1);
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeerManager20setEnsembleUMIStatus*:entry
{
    printf("%Y setEnsembleUMIStatus(%d, 0x%x)\n", walltimestamp, (int)arg1, (uint32_t)arg2);
    stack(8);
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeerManager20getEnsembleUMIStatus*:return
{
    printf("%Y getEnsembleUMIStatus -> %d\n", walltimestamp, (int)arg1);
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeerManager20setUMITimerTimeoutMS*:entry
{
    printf("%Y setUMITimerTimeoutMS(%d)\n", walltimestamp, (int)arg1);
    stack(6);
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeerManager16setAWDLUMIToPeer*:entry,
fbt:com.apple.iokit.IO80211Family:*apple80211setAWDL_UMI_TX_PEER*:entry,
fbt:com.apple.iokit.IO80211Family:*AWDLPeerManager27processPeerAirDropUMIStatus*:entry,
fbt:com.apple.iokit.IO80211Family:*AWDLPeerManager27processPeerProModeUMIStatus*:entry,
fbt:com.apple.iokit.IO80211Family:*AWDLPeerManager15sendRealTimeUMI*:entry
{
    printf("%Y %s\n", walltimestamp, probefunc);
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeerManager15sendRealTimeUMI*:return
{
    printf("%Y sendRealTimeUMI -> %d\n", walltimestamp, (int)arg1);
}

fbt:com.apple.iokit.IO80211Family:*waitForPeerManagerEvent*:entry
{
    printf("%Y WAIT event=%d timeout=%d\n", walltimestamp, (int)arg1, (int)arg2);
}

fbt:com.apple.iokit.IO80211Family:*signalPeerManagerEvent*:entry
{
    printf("%Y SIGNAL event=%d\n", walltimestamp, (int)arg1);
    stack(8);
}
