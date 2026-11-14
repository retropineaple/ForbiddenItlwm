"""Production parser/tag/window and exact completion staging under sanitizers."""
from pathlib import Path
import subprocess,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text();a=s.index('                // A completion may reclaim several descriptors;');b=s.index('                const bool sameEpoch',a);completion=s[a:b];assert 'iwx_send' not in completion
h=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <initializer_list>
#include <AWDLControlReceiveProbe.hpp>
static ItlAWDL::ControlReceiveTag awdlCoexSlotControlRx[256];
static ItlAWDL::ControlReceiveProbe awdlCoexControlRxProbe;
static unsigned awdlCoexChannel=149,awdlCoexToken=9,awdlCoexControlRxCompletions;
static bool enabled,known,quiet,awdlCoexAdded,awdlStopping,awdlCoexTxHalted;
static int awdlCoexGeneration=7;static uint64_t nowUS=10000000;
static bool awdlControlRxEnabled(){return enabled;}static bool awdlCoexPeerKnown(const uint8_t*,uint64_t){return known;}static bool awdlControlRxQuiet(uint64_t){return quiet;}static uint64_t awdlUptimeUS(){return nowUS;}
struct SC{int sc_generation=7;};
static void complete(int slot,bool own,unsigned status,unsigned kind){SC hw;auto*sc=&hw;const bool ok=status==1;
'''+completion+r'''
}
static const uint8_t peer[6]={2,3,4,5,6,7};
static std::vector<uint8_t>frame(unsigned payload=0,uint8_t flags=2){std::vector<uint8_t>f(100+payload);f[0]=8;f[4]=2;f[10]=4;const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};memcpy(f.data()+16,bssid,6);std::vector<uint8_t>ip(60+payload);ip[0]=0x60;ip[4]=(20+payload)>>8;ip[5]=20+payload;ip[6]=6;ip[52]=0x50;ip[53]=flags;size_t written=0;assert(ItlAWDL::buildIPv6DataPayload(ip.data(),ip.size(),0,f.data()+24,f.size()-24,&written)==ItlAWDL::BuildResult::Ready);return f;}
int main(){unsigned cases=0;
 auto good=frame();assert(ItlAWDL::shortTcpControlRequest(good.data(),good.size()));++cases;
 for(unsigned n=0;n<good.size();++n){std::vector<uint8_t>p(good.begin(),good.begin()+n);assert(!ItlAWDL::shortTcpControlRequest(p.data(),p.size()));++cases;}
 for(unsigned payload:{0U,1U,412U,413U})for(unsigned flags:{0U,2U,16U,18U,24U}){auto p=frame(payload,flags);bool expected=payload<=412&&((flags&2)||payload>0);assert(ItlAWDL::shortTcpControlRequest(p.data(),p.size())==expected);++cases;}
 for(unsigned change=0;change<8;++change){auto p=good;switch(change){case 0:p[4]|=1;break;case 1:p[0]=0x88;break;case 2:p[1]|=0x40;break;case 3:p[22]|=1;break;case 4:p[46]=17;break;case 5:p[92]=0x40;break;case 6:p[92]=0xf0;break;case 7:p[16]=1;break;}assert(!ItlAWDL::shortTcpControlRequest(p.data(),p.size()));++cases;}
 // Only exact attributed SUCCESS on a current descriptor may arm a lease.
 for(unsigned mode=0;mode<2;++mode)for(unsigned slot:{0U,255U})for(unsigned own=0;own<2;++own)for(unsigned status=0;status<256;++status){enabled=mode;known=quiet=awdlCoexAdded=true;awdlStopping=awdlCoexTxHalted=false;awdlCoexGeneration=7;awdlCoexToken=9;awdlCoexControlRxProbe={};awdlCoexControlRxCompletions=0;awdlCoexSlotControlRx[slot].record(true,peer,7,9,149);complete(slot,own,status,2);bool expected=mode&&own&&status==1;assert(awdlCoexControlRxProbe.arms==unsigned(expected)&&awdlCoexControlRxCompletions==unsigned(expected));assert(!awdlCoexSlotControlRx[slot].valid);++cases;}
 for(unsigned guard=0;guard<8;++guard){enabled=known=quiet=awdlCoexAdded=true;awdlStopping=awdlCoexTxHalted=false;awdlCoexGeneration=7;awdlCoexToken=9;awdlCoexControlRxProbe={};awdlCoexSlotControlRx[0].record(true,peer,7,9,149);switch(guard){case 0:known=false;break;case 1:quiet=false;break;case 2:awdlCoexAdded=false;break;case 3:awdlStopping=true;break;case 4:awdlCoexTxHalted=true;break;case 5:awdlCoexGeneration=8;break;case 6:++awdlCoexToken;break;case 7:awdlCoexSlotControlRx[0].generation=6;break;}complete(0,true,1,2);assert(!awdlCoexControlRxProbe.arms);++cases;}
 ItlAWDL::ControlReceiveProbe p;assert(p.arm(peer,10000000,7,9,149));assert(p.wants(true,true,true,149,149,10000000,7,9));assert(p.wants(true,true,true,149,149,10249999,7,9));assert(!p.wants(true,true,true,149,149,10250000,7,9));assert(!p.arm(peer,10000001,7,9,149));assert(!p.arm(peer,10999999,7,9,149));assert(p.arm(peer,11000000,7,9,149));++cases;
 for(unsigned flags=0;flags<8;++flags)for(unsigned own:{0U,6U,36U,44U,149U,165U})for(unsigned station:{0U,6U,36U,44U,149U,165U}){bool expected=flags==7&&own==149&&own==station;assert(p.wants(flags&1,flags&2,flags&4,own,station,11000001,7,9)==expected);++cases;}
 assert(!p.wants(true,true,true,149,149,10999999,7,9));assert(!p.wants(true,true,true,149,149,11000001,8,9));assert(!p.wants(true,true,true,149,149,11000001,7,10));assert(p.arm(peer,11000002,8,10,149));p.clear();assert(!p.wants(true,true,true,149,149,11000003,8,10));++cases;
 printf("%u production short-TCP/tag/completion/window cases passed\n",cases);
}
'''
p=T/'test-control-rx.cpp';p.write_text(h)
with tempfile.TemporaryDirectory() as tmp:
 exe=Path(tmp)/'test';subprocess.run(['/usr/bin/clang++','-std=c++14','-fsanitize=address,undefined','-I'+str(R/'include'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
