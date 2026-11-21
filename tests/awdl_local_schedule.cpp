#include <AWDLLocalSchedule.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
int main()
{
    using namespace ItlAWDL;
    {
        SyncPrefix five = {149,149,0,0,16,110,0x1000,16,16,0,3,3,3,3,{2,1,2,3,4,5}};
        uint8_t out[73];size_t n;
        assert(buildLocalFixedSchedule(five,0,0,0,out,73,&n)==BuildResult::Ready && out[0]==149 && out[3]==149);
        for(unsigned i=0;i<16;++i) assert(out[39+2*i]==0x1b && out[40+2*i]==149);
        five.masterChannel=6; assert(buildLocalFixedSchedule(five,0,0,0,out,73,&n)==BuildResult::Invalid);
        // Wi-Fi's channel, for AWDL beside a station: any non-radar channel, with its band's slot flags.
        five.nextChannel=five.masterChannel=36; assert(buildLocalFixedSchedule(five,0,0,0,out,73,&n)==BuildResult::Ready);
        for(unsigned i=0;i<16;++i) assert(out[39+2*i]==0x1b && out[40+2*i]==36);
        five.nextChannel=five.masterChannel=1; assert(buildLocalFixedSchedule(five,0,0,0,out,73,&n)==BuildResult::Ready);
        for(unsigned i=0;i<16;++i) assert(out[39+2*i]==0x2b && out[40+2*i]==1);
        // A radar channel is encoded too (the network's channel, met while associated there); sending on it
        // is decided at send time.
        for (uint8_t dfs : {52, 64, 100, 112, 144}) {
            five.nextChannel=five.masterChannel=dfs; assert(buildLocalFixedSchedule(five,0,0,0,out,73,&n)==BuildResult::Ready);
            for(unsigned i=0;i<16;++i) assert(out[39+2*i]==0x1b && out[40+2*i]==dfs);
        }
        for (uint8_t bad : {0, 14, 40+2, 68, 96, 98, 146, 169}) {
            five.nextChannel=five.masterChannel=bad; assert(buildLocalFixedSchedule(five,0,0,0,out,73,&n)==BuildResult::Invalid);
        }
        assert(globalOperatingClass20(6)==81 && globalOperatingClass20(36)==115 && globalOperatingClass20(48)==115 &&
               globalOperatingClass20(52)==118 && globalOperatingClass20(64)==118 && globalOperatingClass20(100)==121 &&
               globalOperatingClass20(112)==121 && globalOperatingClass20(144)==121 &&
               globalOperatingClass20(149)==124 && globalOperatingClass20(161)==124 && globalOperatingClass20(165)==125 &&
               globalOperatingClass20(68)==0);
        // The network's channel mode: entered at 8 of 16 entries, kept above 3.
        const uint8_t sidecar[16] = {112,112,149,112,112,112,112,112,112,112,149,112,112,112,112,112};
        const uint8_t idle[16] = {112,0,149,0,0,44,0,0,6,0,44,0,0,44,0,0};
        assert(entriesOn(sidecar, 112)==14 && entriesOn(idle, 112)==1 && entriesOn(idle, 0)==0);
        assert(infraFollowWanted(false, 14) && !infraFollowWanted(false, 7) && infraFollowWanted(false, 8));
        assert(infraFollowWanted(true, 4) && !infraFollowWanted(true, 3) && !infraFollowWanted(true, 1));
    }
    SyncPrefix p = {6,6,0,0,16,110,0x1000,16,16,0,3,3,3,3,{2,1,2,3,4,5}};
    const uint64_t epoch=7000000;
    struct Case { uint64_t us; unsigned aw, down, remaining; };
    const Case cases[] = {{0,0,64,16},{1023,0,64,16},{1024,0,63,15},
        {16383,0,49,1},{16384,1,48,0},{65535,3,1,0},{65536,4,64,16},
        {uint64_t(65535)*16384,65535,16,0},{uint64_t(65536)*16384,0,64,16}};
    for (auto c:cases) {
        uint8_t payload[75]; memset(payload,0xa5,sizeof(payload)); size_t n=999;
        assert(buildLocalFixedSchedule(p,epoch,epoch+c.us,0,payload+1,73,&n)==BuildResult::Ready);
        assert(n==73 && payload[0]==0xa5 && payload[74]==0xa5);
        const auto *s=payload+1;
        assert(read16(s+1)==c.down && read16(s+15)==c.remaining && read16(s+29)==c.aw);
        assert(s[27]==4 && s[28]==0 && read16(s+31)==0);
        const uint8_t header[]={15,1,0,3,255,255};
        assert(!memcmp(s+33,header,6) && s[71]==0 && s[72]==0);
        for (unsigned i=0;i<16;++i) assert(s[39+2*i]==0x2b && s[40+2*i]==6);
        LocalFrame f={{2,1,2,3,4,5},{255,255,255,255,255,255},3,0,10,10,s,73,nullptr,0};
        uint8_t packet[116]; size_t length; ActionFrame parsed;
        assert(buildLocalFrame(f,packet,sizeof(packet),&length)==BuildResult::Ready && length==116);
        assert(parseAction(packet,length,&parsed)==ParseResult::Valid && parsed.hasSync);
        assert(parsed.txDownCounter==c.down && parsed.remainingAW==c.remaining);
    }
    uint8_t out[73]; size_t n=999;
    for (unsigned cap=0;cap<73;++cap) {
        memset(out,0xa5,sizeof(out));
        assert(buildLocalFixedSchedule(p,epoch,epoch,0,out,cap,&n)==BuildResult::NoSpace && n==999);
        for (auto b:out) assert(b==0xa5);
    }
    assert(buildLocalFixedSchedule(p,epoch,epoch-1,0,out,73,&n)==BuildResult::Invalid);
    p.masterChannel=149;
    assert(buildLocalFixedSchedule(p,epoch,epoch,0,out,73,&n)==BuildResult::Invalid);
    p.masterChannel=6; p.awPeriod=16384;
    assert(buildLocalFixedSchedule(p,epoch,epoch,0,out,73,&n)==BuildResult::Invalid);
    p.awPeriod=16;
    assert(buildLocalFixedSchedule(p,0,UINT64_MAX,65535,out,73,&n)==BuildResult::Invalid);
    assert(buildLocalFixedSchedule(p,0,UINT64_MAX,65532,out,73,&n)==BuildResult::Ready);
    puts("AWDL local channel-6 schedule: passed (offline; no timer or peer synchronization)");
}
