/*
 * Why does ip6_output_list refuse a 1497-byte packet on awdl0 whose MTU is 1500?
 *
 * Read from the 24H232 x86_64 kernel (oc_sequoia_awdl_dump/01_x86_kernel/kernel_x86_64, ip6_output_list at
 * 0xffffff80007577c0, ip6_getpmtu at 0xffffff800075b6b0). The EMSGSIZE exit is 0xffffff800075a523 (errno 40 stored at
 * -0x2c(%rbp), event 0x2030017), reached only from the compare at 0xffffff8000759eb9:
 *
 *     len    = *(u32 *)(m + 0x30)                      the packet length
 *     mtu    = ip6_getpmtu(): the route's rmx_mtu when 0 < rmx_mtu < linkmtu, else the link MTU
 *     linkmtu= IN6_LINKMTU(ifp): nd_ifinfo linkmtu if 0 < it < if_mtu, else nd_ifinfo maxmtu if 0 < it < if_mtu, else if_mtu
 *     error when len > mtu and the interface has no TSO bit for the packet (if_hwassist 0x400000 / 0x80), or when
 *     len > linkmtu on the other branch.
 *
 * So four numbers decide it, and the kernel reads them from fixed offsets in this build:
 *
 *     ro      = arg3 (struct route_in6 *)           rt   = *(ro)
 *     rt_rmx.rmx_mtu = *(u32 *)(rt + 0xd4)          rt_rmx.rmx_locks = *(u32 *)(rt + 0xd0)
 *     ifp     = *(rt + 0x88)                        if_mtu = *(u32 *)(ifp + 0xe8)        if_hwassist = *(u32 *)(ifp + 0x1f8)
 *     ndi     = *(ifp + 0x7f0)                      linkmtu = *(u32 *)(ndi + 0x22c)      maxmtu = *(u32 *)(ndi + 0x230)
 *
 * This prints them for every packet over 1480 bytes that sharingd (or wifip2pd) sends and that comes back EMSGSIZE, plus a
 * count of the sizes that were accepted. A stale route MTU (rmx_mtu below 1497) or a maxmtu left at 1484 shows at once.
 *
 *   sudo dtrace -Z -q -s tools/diag/trace_emsgsize_mtu.d -o /var/tmp/emsgsize_mtu.out     # start, then send an AirDrop; Ctrl-C
 *
 * Offsets are for 24H232 only; on another build re-derive them from the disassembly as in docs/DEBUGGING.md.
 */
#pragma D option quiet
#pragma D option dynvarsize=128m

fbt:mach_kernel:ip6_output_list:entry
/ (execname == "sharingd" || execname == "wifip2pd") && *(uint32_t *)(arg0 + 0x30) > 1480 /
{
    self->t = 1;
    self->len = *(uint32_t *)(arg0 + 0x30);
    self->ro = arg3;
    self->rt = arg3 ? *(uint64_t *)(arg3) : 0;
    self->rmtu = self->rt ? *(uint32_t *)(self->rt + 0xd4) : 0;
    self->rlocks = self->rt ? *(uint32_t *)(self->rt + 0xd0) : 0;
    self->ifp = self->rt ? *(uint64_t *)(self->rt + 0x88) : 0;
    self->ifmtu = self->ifp ? *(uint32_t *)(self->ifp + 0xe8) : 0;
    self->hwa = self->ifp ? *(uint32_t *)(self->ifp + 0x1f8) : 0;
    self->ifn = self->ifp ? stringof((char *)*(uint64_t *)(self->ifp + 0x20)) : "-";   /* if_xname, DWARF offset 0x20 */
    self->ndi = self->ifp ? *(uint64_t *)(self->ifp + 0x7f0) : 0;
    self->lmtu = self->ndi ? *(uint32_t *)(self->ndi + 0x22c) : 0;
    self->mmtu = self->ndi ? *(uint32_t *)(self->ndi + 0x230) : 0;
}

fbt:mach_kernel:ip6_output_list:return
/ self->t /
{
    @result[execname, self->ifn, arg1 == 40 ? "EMSGSIZE" : "ok", self->len, self->rmtu, self->ifmtu, self->lmtu, self->mmtu] = count();
    self->t = 0;
}

END
{
    printf("%-9s %-6s %-9s %5s %6s %6s %6s %6s  %s\n", "proc", "if", "result", "len", "rtmtu", "ifmtu", "lmtu", "maxmtu", "count");
    printa("%-9s %-6s %-9s %5d %6d %6d %6d %6d  %@d\n", @result);
}
