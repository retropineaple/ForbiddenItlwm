/*
 * Who returns EMSGSIZE (40) while a big AirDrop QUIC send over awdl0 dies?
 *
 * Written for the 2026-11-15 investigation (docs/DEBUGGING.md, case 2). Evidence at the time (16:10-16:13): the Mac hands ~1.6 MB to awdl0 in one 0.3 s burst, the driver and the
 * iPhone's Block Ack deliver it (BaTxAcked 97%), and then sharingd reports NWError 40 (EMSGSIZE) about
 * 70 ms after the last packet. No UDP payload exceeds 1436 bytes (the 1484-byte MTU's limit) and no ICMPv6
 * "packet too big" arrives, so the error is raised inside macOS. This prints the function that returns 40
 * and its kernel stack, for sharingd and kernel threads only.
 *
 * Run (needs sudo; SIP allows dtrace on this Mac; foreground, stop with Ctrl-C):
 *   sudo dtrace -Z -e -s tools/diag/trace_emsgsize.d                 # compile check only
 *   sudo dtrace -Z -q -s tools/diag/trace_emsgsize.d -o emsgsize.out
 * then start the big send; after it fails press Ctrl-C in the dtrace terminal.
 */
#pragma D option quiet
#pragma D option stackframes=14

fbt:mach_kernel:udp6_output:return,
fbt:mach_kernel:udp_output:return,
fbt:mach_kernel:ip6_output*:return,
fbt:mach_kernel:in6_*:return,
fbt:mach_kernel:sosend*:return,
fbt:mach_kernel:sendmsg*:return,
fbt:mach_kernel:fsw_*:return,
fbt:mach_kernel:nx_netif_*:return,
fbt:mach_kernel:flow_*:return,
fbt:mach_kernel:ifnet_*:return,
fbt:mach_kernel:dlil_*:return,
fbt:mach_kernel:necp_*:return
/ arg1 == 40 && (execname == "sharingd" || execname == "kernel_task" || execname == "rapportd") /
{
    @who[probefunc, execname] = count();
    printf("%Y %s[%d] %s returned EMSGSIZE\n", walltimestamp, execname, pid, probefunc);
    stack(14);
    printf("\n");
}

END
{
    printf("--- EMSGSIZE returns by function and process\n");
    printa("%-44s %-14s %@d\n", @who);
}
