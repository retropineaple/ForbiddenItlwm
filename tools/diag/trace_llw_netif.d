/*
 * What the kernel does with a Continuity Camera attempt over llw0, on the Skywalk netif side.
 *
 * Known (2026-11-17, Sequoia 15.8.2, build with -itlawdlllw): llw0 exists and is up, the media stack's outbound
 * connection over llw0 becomes "ready" and the kernel logs "netif flowstats (add): if llw0, nx_port 2", yet llw0
 * counts 0 packets in and out, our transmit callback is never called, and nothing from the iPhone arrives.
 * This shows whether the flow is added and classified, whether the low latency receive path is ever entered, and
 * whether anything reaches the driver's queues. Rare events print with a short stack; frequent ones are counted
 * and printed every 5 s.
 *
 * Run it through tools/diag/llw_attempt.sh (it adds packet captures and Skywalk's own tables), or alone:
 *   sudo dtrace -Z -q -s tools/diag/trace_llw_netif.d -c "sleep 50" -o /var/tmp/llw_dtrace.txt
 */
#pragma D option quiet
#pragma D option dynvarsize=64m
#pragma D option stackframes=8

BEGIN
{
    printf("%Y tracing the llw0 netif path\n", walltimestamp);
}

/* Rare, informative: print with a stack. */
fbt::nx_netif_flow_add:entry,
fbt::nx_netif_flow_remove:entry,
fbt::netif_vp_na_create:entry,
fbt::netif_vp_na_channel_event:entry,
fbt::nx_netif_llink_init:entry,
fbt::nx_netif_llink_add:entry,
fbt::kern_nexus_netif_llink_add:entry,
fbt::nx_netif_llw_detach_notify:entry
{
    printf("%Y %s (%s)\n", walltimestamp, probefunc, execname);
    stack(6);
}

/* Frequent: counted. */
fbt::nx_netif_flow_classify:entry,
fbt::nx_netif_flow_match:entry,
fbt::netif_llw_rx_notify_default:entry,
fbt::netif_llw_rx_notify_fast:entry,
fbt::nx_netif_demux:entry,
fbt::nx_netif_host_output:entry
{
    @kern[probefunc] = count();
}

/*
 * The nexus assignment for a flow on a netif nexus (macOS asks the interface's netagent to assign one). Its second
 * argument is the flow request; the 16-bit word at +0x46 says what kind of flow it is, and the netif agent only
 * accepts requests carrying 0x10 (the low latency kind), answering ENOTSUP (45) to the rest and EINVAL (22) to bad
 * combinations. A listener that gets no nexus ("assigned returned false") should show up here with its flags.
 */
fbt::nx_netif_netagent_flow_add:entry
{
    self->req = arg1;
    printf("%Y nx_netif_netagent_flow_add (%s) flags=0x%04x\n", walltimestamp, execname,
        *(uint16_t *)(self->req + 0x46));
}

fbt::nx_netif_netagent_flow_add:return
/ self->req /
{
    printf("%Y   -> returns %d%s\n", walltimestamp, (int)arg1, arg1 == 0 ? " (assigned)" : " (refused)");
    self->req = 0;
}

fbt::netagent_assign_nexus:return,
fbt::necp_client_assign_from_socket:return,
fbt::necp_create_nexus_assign_message:return
{
    printf("%Y %s returns %d (%s)\n", walltimestamp, probefunc, (int)arg1, execname);
}

/* Our driver's llw0 class: its transmit and receive-supply callbacks and everything else it runs. */
fbt::*ItlLLWInterface*:entry
{
    @llw[probefunc] = count();
}

/* IOSkywalkFamily: the queues every Skywalk interface shares (awdl0's traffic is in here too). */
fbt:com.apple.iokit.IOSkywalkFamily:*TxSubmissionQueue*:entry,
fbt:com.apple.iokit.IOSkywalkFamily:*TxCompletionQueue*:entry,
fbt:com.apple.iokit.IOSkywalkFamily:*RxCompletionQueue*:entry
{
    @sky[probefunc] = count();
}

tick-5s
{
    printf("--- %Y\n", walltimestamp);
    printa("  kernel  %-36s %@d\n", @kern);
    printa("  llw     %-60s %@d\n", @llw);
    printa("  skywalk %-60s %@d\n", @sky);
}

END
{
    printf("--- end %Y\n", walltimestamp);
    printa("  kernel  %-36s %@d\n", @kern);
    printa("  llw     %-60s %@d\n", @llw);
    printa("  skywalk %-60s %@d\n", @sky);
}
