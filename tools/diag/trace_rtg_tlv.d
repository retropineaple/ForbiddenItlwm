/*
 * Where does the iPhone's real-time group TLV (type 31) get lost? Follows every AWDL action frame from this driver into the
 * family, using the kext symbols of IO80211Family 24H232 (kext base 0xffffff8002111000, the addresses in oc_sequoia_awdl_dump).
 *
 *   1  ours   ItlAWDLInterface::receiveManagementFrame   frames this driver hands the family (the iPhone's PSF/MIF, ...)
 *   2  family IO80211AWDLPeerManager::actionFrameInput   frames the peer manager takes in
 *   3  family IO80211AWDLPeer::actionFrameReport         frames a peer object processes
 *   4  family IO80211AWDLPeer::parseAwdlSyncParamsTlv    ... that carried a sync TLV (4)
 *   5  family IO80211AWDLPeer::parseAWDLRealTimeGroupTLV ... that carried a real-time group TLV (31): the family only calls
 *                                                        this when the frame has one
 *   6  family IO80211AWDLPeer::shouldRemovePeerFromNDP   returns "remove", with the time
 *   7  family IO80211AWDLPeerManager::removePeerFromRealTimeGroup, IO80211AWDLPeer::setPeerNotReachable (with the time)
 *
 * How to read it: 1 > 2 means frames are lost between the driver and the family; 3 > 4 > 5 shows how many reach the parser, and
 * 5 = 0 with 3 in the hundreds means the family itself sees no TLV 31 in the iPhone's frames (so the loss is on the air or in
 * the iPhone, not in our RX path); 5 > 0 with the "has no RTG TLV" warning continuing means something between the TLV and
 * the check is wrong.
 *
 *   sudo dtrace -Z -q -s tools/diag/trace_rtg_tlv.d -o /var/tmp/rtg_tlv.out      # start, run Continuity Camera ~60 s, Ctrl-C
 *
 * If a driver probe still shows 0, list what dtrace can attach to:
 *   sudo dtrace -l -n 'fbt:org.forbiddenitlwm.ForbiddenItlwm::entry' | grep -iE 'receive|action|mgmt'
 * -Z lets it start if a probe is missing (another build); the summary then shows 0 for it. Counts are per probe, not per peer.
 */
#pragma D option quiet
#pragma D option dynvarsize=64m

BEGIN
{
    start = timestamp;
}

/* The driver side. On the 2026-11-20 boot dtrace had no fbt module for this kext (`dtrace -l -n 'fbt:org.forbiddenitlwm.ForbiddenItlwm::entry'`
 * matched nothing), so these never attach; the driver's own counts come from the registry instead (tools/diag/../ the helper
 * /var/tmp/izzy-ours.sh, run before and after the trace). Kept for builds where the module does appear. */
fbt:org.forbiddenitlwm.ForbiddenItlwm:__Z20itlAWDLReceiveAction*:entry
{
    @n["0a ours:   itlAWDLReceiveAction (from the radio RX path)"] = count();
    @air = quantize(arg2);
}

fbt:org.forbiddenitlwm.ForbiddenItlwm:__ZN12AirportItlwm22receiveAWDLActionFrame*:entry
{
    @n["0b ours:   AirportItlwm::receiveAWDLActionFrame"] = count();
}

fbt:org.forbiddenitlwm.ForbiddenItlwm:__ZN16ItlAWDLInterface22receiveManagementFrame*:entry
{
    @n["1  ours:   ItlAWDLInterface::receiveManagementFrame"] = count();
    @len = quantize(arg3);
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeerManager*actionFrameInput*:entry
{
    @n["2 family: AWDLPeerManager::actionFrameInput"] = count();
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeer*actionFrameReport*:entry
{
    @n["3 family: AWDLPeer::actionFrameReport"] = count();
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeer*parseAwdlSyncParamsTlv*:entry
{
    @n["4 family: parseAwdlSyncParamsTlv (sync TLV 4)"] = count();
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeer*parseAWDLRealTimeGroupTLV*:entry
{
    @n["5 family: parseAWDLRealTimeGroupTLV (RTG TLV 31)"] = count();
    printf("%9d ms  parseAWDLRealTimeGroupTLV\n", (timestamp - start) / 1000000);
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeer*shouldRemovePeerFromNDP*:return
/arg1/
{
    @n["6 family: shouldRemovePeerFromNDP -> remove"] = count();
    printf("%9d ms  shouldRemovePeerFromNDP -> remove\n", (timestamp - start) / 1000000);
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeer*setPeerNotReachable*:entry
{
    @n["7 family: setPeerNotReachable"] = count();
    printf("%9d ms  setPeerNotReachable\n", (timestamp - start) / 1000000);
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeerManager*removePeerFromRealTimeGroup*:entry
{
    @n["7 family: removePeerFromRealTimeGroup"] = count();
    printf("%9d ms  removePeerFromRealTimeGroup\n", (timestamp - start) / 1000000);
}


/*
 * Where the family really reads the RTG TLV (IO80211AWDLPeer::actionFrameReport, 24H232, read from the dump with REA's call
 * references and the disassembly): parseAWDLRealTimeGroupTLV has exactly two call sites.
 *   0xffffff80021507ce  in the loop over the frame's TLVs: a type 31 TLV, which sets the "frame has an RTG TLV" flag.
 *   0xffffff800214fd2e  "parsing the RTG TLV in UMI": the frame's data path TLV (type 12) is found with
 *                       findTLVCursorForType, getUnicastOptionsFromDataPathTLV returns its 32-bit options, and only when
 *                       options & 0xfd000000 is 0x09000000, 0x0d000000, 0x10000000, 0x40000000 or 0x11000000 does the family
 *                       parse the RTG information carried inside it.
 * So the iPhone's RTG information may arrive in its unicast UMI frames, not in the sync frames, and the driver's own census
 * (top-level TLV 31 only) would never see it. This histogram shows the options the iPhone's frames carry.
 */
fbt:com.apple.iokit.IO80211Family:*AWDLPeer32getUnicastOptionsFromDataPathTLV*:entry
{
    @n["8 family: getUnicastOptionsFromDataPathTLV (frame had a data path TLV)"] = count();
}

fbt:com.apple.iokit.IO80211Family:*AWDLPeer32getUnicastOptionsFromDataPathTLV*:return
{
    @opt[arg1 & 0xfd000000] = count();
}

END
{
    printf("\nprobe counts\n");
    printa("  %-58s %@d\n", @n);
    printf("\ndata path TLV options & 0xfd000000 (RTG-in-UMI is parsed for 0x09000000, 0x0d000000, 0x10000000, 0x40000000, 0x11000000)\n");
    printa("  0x%08x  %@d\n", @opt);
    printf("\nframe length from the radio path (itlAWDLReceiveAction)\n");
    printa(@air);
    printf("\nbytes handed to the family per frame (receiveManagementFrame)\n");
    printa(@len);
}
