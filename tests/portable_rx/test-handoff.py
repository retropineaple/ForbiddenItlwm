"""Exercise the production state helper and extracted task with mocked firmware."""
from pathlib import Path
import subprocess,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1]
s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text()
a=s.index('    // Peer handoff runs only in the task,')
b=s.index('    // Outbound control can need a reply',a)
block=s[a:b]
assert s.index('if (!awdlCoexData || !awdlCoexAdded',s.index('awdlCoexBaHandle(struct')) < a
assert 'awdlCoexPeerHandoff.note' in s[s.index('ItlAWDL::parseAddbaRequest(cf, coexLen'):]
h=r'''
#include <cassert>
#include <cstdio>
#include "AWDLPeerHandoff.hpp"
#define nitems(x) (sizeof(x)/sizeof((x)[0]))
struct Ring{int queued=0;};struct SC{Ring txq[4];int gen=7;};
static SC hardware;
static ItlAWDL::PeerHandoff awdlCoexPeerHandoff;
static ItlAWDL::PendingAddbaRequests awdlCoexBaRequestsPending;
static uint32_t awdlCoexToken=8,awdlCoexBaPeerEpoch=9,awdlCoexPeerHandoffErrors,awdlCoexPeerHandoffRetired;
static bool enabled=true,awdlCoexBaPeerAdded=true,known=true;
static int awdlCoexBaTxQueue=2,awdlCoexQueue=1,softwareQueued,awdlCoexBaAnswerLength;
static bool awdlCoexResumePending,awdlCoexBaQueueAllocPending;
static int awdlCoexDelbaWaitTid=-1,removeBehavior,removeCalls;
static uint64_t clockUS=10000000,awdlCoexBaPeerLastUS=6000000;
static bool awdlPeerHandoffEnabled(){return enabled;}
static unsigned itlAWDLTransmitQueued(void*){return softwareQueued;}
static void*getController(){return nullptr;}
static bool awdlCoexPeerKnown(const uint8_t*,uint64_t){return known;}
static uint64_t awdlUptimeUS(){return clockUS;}
static int awdlCoexBaRemovePeer(SC*sc){
 ++removeCalls;awdlCoexBaPeerAdded=false;awdlCoexBaRequestsPending.clear();
 if(removeBehavior==1)return 5;
 if(removeBehavior==2)++sc->gen;
 if(removeBehavior==3)++awdlCoexToken;
 if(removeBehavior==4)awdlCoexBaPeerAdded=true;
 if(removeBehavior==5)++awdlCoexBaPeerEpoch;
 if(removeBehavior==6)clockUS+=3000000;
 if(removeBehavior==7)known=false;
 if(removeBehavior==8)clockUS=1;
 if(removeBehavior==9)enabled=false;
 return 0;
}
static void task(){SC*sc=&hardware;const int gen=sc->gen;const uint32_t tok=awdlCoexToken;const uint64_t now=clockUS;
 auto stillOurs=[&](){return gen==sc->gen&&tok==awdlCoexToken;};
'''+block+r'''
}
static ItlAWDL::AddbaRequest request(){ItlAWDL::AddbaRequest r{};r.immediate=true;r.peer[0]=2;r.peer[5]=3;return r;}
static void reset(){
 hardware=SC{};enabled=known=awdlCoexBaPeerAdded=true;awdlCoexToken=8;awdlCoexBaPeerEpoch=9;
 awdlCoexPeerHandoff={};awdlCoexBaRequestsPending.clear();clockUS=10000000;awdlCoexBaPeerLastUS=6000000;
 softwareQueued=awdlCoexBaAnswerLength=0;awdlCoexResumePending=awdlCoexBaQueueAllocPending=false;
 awdlCoexDelbaWaitTid=-1;awdlCoexQueue=1;awdlCoexBaTxQueue=2;removeBehavior=removeCalls=0;awdlCoexPeerHandoffErrors=awdlCoexPeerHandoffRetired=0;
 assert(awdlCoexPeerHandoff.note(request(),clockUS,7,8,9));
}
int main(){unsigned cases=0;
 // Off, stale epochs, stale requests and clock reversal never call firmware.
 for(int state=0;state<8;++state){reset();
  switch(state){case 0:enabled=false;break;case 1:hardware.gen++;break;case 2:awdlCoexToken++;break;
   case 3:awdlCoexBaPeerEpoch++;break;case 4:clockUS+=2000001;break;case 5:clockUS--;break;
   case 6:known=false;break;case 7:awdlCoexBaPeerAdded=false;break;}
  task();assert(!removeCalls&&!awdlCoexPeerHandoff.valid&&awdlCoexBaRequestsPending.empty());++cases;
 }
 // Every queue and pending-control guard independently protects an old station.
 for(int guard=0;guard<12;++guard){reset();
  switch(guard){case 0:softwareQueued=1;break;case 1:hardware.txq[1].queued=1;break;
   case 2:hardware.txq[2].queued=1;break;case 3:awdlCoexBaAnswerLength=33;break;
   case 4:assert(awdlCoexBaRequestsPending.note(request()));break;case 5:awdlCoexResumePending=true;break;
   case 6:awdlCoexBaQueueAllocPending=true;break;case 7:awdlCoexDelbaWaitTid=0;break;
   case 8:awdlCoexBaPeerLastUS=clockUS;break;case 9:awdlCoexQueue=-1;break;case 10:awdlCoexBaTxQueue=4;break;case 11:awdlCoexBaTxQueue=-2;break;}
  task();assert(!removeCalls&&awdlCoexBaPeerAdded);++cases;
 }
 // Actual task post-wait logic: transport error, new generation/listener/owner,
 // stale request and lost peer never stage work for the wrong listener.
 for(int result=0;result<10;++result){reset();removeBehavior=result;task();assert(removeCalls==1);
  if(!result){assert(awdlCoexPeerHandoffRetired==1&&!awdlCoexPeerHandoffErrors);
   ItlAWDL::AddbaRequest r{};assert(awdlCoexBaRequestsPending.take(0,r));assert(!memcmp(r.peer,request().peer,6));}
  else{assert(awdlCoexPeerHandoffErrors==1&&!awdlCoexPeerHandoffRetired&&awdlCoexBaRequestsPending.empty());}
  ++cases;
 }
 for(uint64_t age:{0ULL,1999999ULL,2000000ULL,2000001ULL})for(int gen:{6,7})for(unsigned token:{8U,9U})for(unsigned epoch:{9U,10U}){
  reset();assert(awdlCoexPeerHandoff.current(clockUS+age,gen,token,epoch)==(age<=2000000&&gen==7&&token==8&&epoch==9));++cases;
 }
 for(uint64_t age:{0ULL,2999999ULL,3000000ULL,3000001ULL})for(bool queued:{false,true})for(bool busy:{false,true}){
  reset();assert(awdlCoexPeerHandoff.canRetire(clockUS,clockUS-age,queued,busy)==(age>=3000000&&!queued&&!busy));++cases;
 }
 reset();auto r=request();r.peer[5]++;assert(!awdlCoexPeerHandoff.note(r,clockUS,7,8,9));
 assert(awdlCoexPeerHandoff.note(r,clockUS+2000001,7,8,9));
 r.tid=8;assert(!awdlCoexPeerHandoff.note(r,clockUS+2000002,7,8,9));
 r.tid=0;r.immediate=false;assert(!awdlCoexPeerHandoff.note(r,clockUS+2000002,7,8,9));
 printf("PASS %u production helper/task cases plus challenger coalescing and input bounds\n",cases);
}
'''
# std::initializer_list is needed for bounded range-for fixture lists.
h='#include <initializer_list>\n'+h
(T/'test-handoff.cpp').write_text(h)
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'test'
 subprocess.run(['xcrun','clang++','-std=c++14','-fsanitize=address,undefined','-g','-I'+str(R/'include'),str(T/'test-handoff.cpp'),'-o',str(exe)],check=True)
 out=subprocess.check_output([str(exe)],text=True)
 (T/'test-handoff.log').write_text(out);print(out,end='')
