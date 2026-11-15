/*
 * Continuity Camera, second completion: after the peer cache is answered, IO80211 sends a real-time "UMI"
 * (sendRealTimeUMI, umi 0xb000000) and logs "Ensemble start Failure, waiting for UMIStatus", then
 * "failed to add SideCar to RTG ret:0xe00002ca". This shows which peer-manager event it waits for, what
 * signals peer-manager events, and which family functions mention the UMI, with their callers.
 *
 *   sudo dtrace -l -n 'fbt:com.apple.iokit.IO80211Family:*UMI*:entry'     # what exists
 *   sudo dtrace -Z -q -s tools/diag/trace_umi.d -o /tmp/pc/umi.out        # run, start the camera, Ctrl-C
 * Foreground only (a background shell ignores Ctrl-C).
 */
#pragma D option quiet
#pragma D option stackframes=10

fbt:com.apple.iokit.IO80211Family:*waitForPeerManagerEvent*:entry
{
    printf("%Y WAIT   %s event=%d timeout=%d\n", walltimestamp, probefunc, (int)arg1, (int)arg2);
}

fbt:com.apple.iokit.IO80211Family:*signalPeerManagerEvent*:entry
{
    printf("%Y SIGNAL %s event=%d\n", walltimestamp, probefunc, (int)arg1);
    stack(10);
}

fbt:com.apple.iokit.IO80211Family:*UMI*:entry,
fbt:com.apple.iokit.IO80211Family:*RealTimeUMI*:entry
{
    printf("%Y UMI    %s a1=%x a2=%x\n", walltimestamp, probefunc, (uint32_t)arg1, (uint32_t)arg2);
    stack(6);
}

fbt::*postMessage*:entry
/ execname == "kernel_task" || execname == "kernel" /
{
    printf("%Y POST   %s msg=%d len=%d\n", walltimestamp, probefunc, (int)arg1, (int)arg3);
}
