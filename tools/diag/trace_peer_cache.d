/*
 * Continuity Camera: what does the family ask of the driver for the peer cache, and what does it wait for?
 *
 * Evidence (2026-11-15 17:19:34): IO80211 calls setPEER_CACHE_CONTROL on awdl0, the driver answers
 * kIOReturnUnsupported (0xe00002c7), the family then waits 3000 ms for a "PeerCacheOperationCompleted" event,
 * logs "handleEnsembleTrafficReg: Peer cache operation failed", and the RemoteCamera traffic registration fails
 * with kIOReturnIOError. This prints the request words the driver receives and the family functions on the way.
 *
 *   sudo dtrace -l -n 'fbt::*setPEER_CACHE_CONTROL*:entry'            # probes must exist (loaded kext)
 *   sudo dtrace -Z -q -s tools/diag/trace_peer_cache.d                # run, start Continuity Camera, Ctrl-C
 * Run it in the foreground; a background shell ignores Ctrl-C.
 */
#pragma D option quiet
#pragma D option stackframes=12

fbt::*setPEER_CACHE_CONTROL*:entry
{
    this->p = (uint32_t *)arg1;
    printf("%Y DRIVER setPEER_CACHE_CONTROL data=%p\n", walltimestamp, arg1);
    printf("  w0..w7:  %08x %08x %08x %08x %08x %08x %08x %08x\n",
        this->p[0], this->p[1], this->p[2], this->p[3], this->p[4], this->p[5], this->p[6], this->p[7]);
    printf("  w8..w15: %08x %08x %08x %08x %08x %08x %08x %08x\n",
        this->p[8], this->p[9], this->p[10], this->p[11], this->p[12], this->p[13], this->p[14], this->p[15]);
    stack(12);
}

fbt::*getPEER_CACHE_MAXIMUM_SIZE*:entry
{
    printf("%Y DRIVER getPEER_CACHE_MAXIMUM_SIZE\n", walltimestamp);
}

fbt:com.apple.iokit.IO80211Family:*PeerCache*:entry,
fbt:com.apple.iokit.IO80211Family:*handleEnsembleTrafficReg*:entry,
fbt:com.apple.iokit.IO80211Family:*waitForPeerManagerEvent*:entry,
fbt:com.apple.iokit.IO80211Family:*PeerManagerEvent*:entry
{
    printf("%Y FAMILY %s\n", walltimestamp, probefunc);
}
