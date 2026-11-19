/*
 * How big is the IPv6 packet that ip6_output_list rejects with EMSGSIZE for sharingd?
 *
 * The previous trace named udp6_output -> ip6_output_list. Every MTU the stack uses for awdl0 was 1484
 * (ifconfig, ndp linkmtu, route mtu), so a rejected packet was longer than 1484 bytes. With the driver's awdl0 at
 * 1500 (2026-11-18: tools/diag/airdrop_check.py --sizes shows the stack accepts up to 1500 and refuses 1501) the sends
 * still died with unexpected-40, so the rejected packets are now longer than 1500: this prints how much longer. If those lengths are
 * 1485-1500 (UDP payload 1437-1452), QUIC sized its datagrams from en0's 1500-byte MTU (the connection log
 * shows the QUIC path over en0) while the packets leave on awdl0.
 *
 * The mbuf's packet length offset is not certain, so this prints several candidate words; the one that
 * shows values near 1484-1500 on the failing calls (and a spread up to 1484 in the histogram) is the length.
 *
 *   sudo dtrace -Z -e -s tools/diag/trace_emsgsize_size.d                        # compile check
 *   sudo dtrace -Z -q -s tools/diag/trace_emsgsize_size.d -o emsgsize_size.out   # run, then Ctrl-C
 *
 * Offsets are for 24H232 (x86_64): the packet length is the int at mbuf+48 and the first mbuf's length the int at
 * +24; both agreed (1494-1500) on the rejected packets. On another build check that the two columns still match
 * before trusting them. Don't run it during a bulk send: at about 18,000 packets a second it overloaded dtrace
 * ("dynamic variable drops"), so a rejection could be missed; dynvarsize below is meant to prevent that but wasn't
 * re-run after being added.
 */
#pragma D option quiet
#pragma D option dynvarsize=256m

fbt:mach_kernel:ip6_output_list:entry
/ execname == "sharingd" /
{
    self->t = 1;
    self->mlen = *(int32_t *)(arg0 + 24);
    self->w44 = *(int32_t *)(arg0 + 44);
    self->w48 = *(int32_t *)(arg0 + 48);
    self->w52 = *(int32_t *)(arg0 + 52);
    self->w56 = *(int32_t *)(arg0 + 56);
    @all48 = quantize(self->w48);
}

fbt:mach_kernel:ip6_output_list:return
/ self->t && arg1 == 40 /
{
    printf("%Y EMSGSIZE: m_len=%d  @44=%d  @48=%d  @52=%d  @56=%d\n", walltimestamp,
        self->mlen, self->w44, self->w48, self->w52, self->w56);
    @bad[self->w48] = count();
}

fbt:mach_kernel:ip6_output_list:return
/ self->t /
{
    self->t = 0;
}

END
{
    printf("\n--- length candidate @48 for every ip6 output from sharingd\n");
    printa(@all48);
    printf("\n--- candidate @48 of rejected packets\n");
    printa("%d bytes  x%@d\n", @bad);
}
