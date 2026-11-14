"""Exact task block: idle/drain/known-peer/owner guards and post-firmware invalidation."""
from pathlib import Path
import subprocess,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text();a=s.index('    // Outbound control can need a reply');b=s.index('    // Keep the station',a);block=s[a:b]
h=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <AWDLControlPeer.hpp>
#include <AWDLPeerHandoff.hpp>
#define nitems(x) (sizeof(x)/sizeof((x)[0]))
struct SC {int sc_generation=7;struct{int queued=0;}txq[4];};static SC hw;
static ItlAWDL::ControlPeerHint awdlCoexControlPeerHint;
static ItlAWDL::PeerHandoff awdlCoexPeerHandoff;
static ItlAWDL::PendingAddbaRequests awdlCoexBaRequestsPending;
static bool enabled,known,awdlCoexBaPeerAdded,awdlCoexInFlight,awdlCoexResumePending,awdlCoexBaQueueAllocPending;
static unsigned awdlCoexToken,awdlCoexBaPeerEpoch,awdlCoexControlPeerRetired,awdlCoexControlPeerErrors;
static uint8_t awdlCoexBaPeer[6];static int awdlCoexBaTxQueue,awdlCoexQueue,awdlCoexBaAnswerLength,awdlCoexDelbaWaitTid;
static uint64_t nowUS,awdlCoexBaPeerLastUS;static unsigned softwareQueued,removeCalls;static int behavior;
static bool awdlControlPeerEnabled(){return enabled;}
static bool awdlCoexPeerKnown(const uint8_t*,uint64_t){return known;}
static uint64_t awdlUptimeUS(){return nowUS;}
static void*getController(){return nullptr;}
static unsigned itlAWDLTransmitQueued(void*){return softwareQueued;}
static int awdlCoexBaRemovePeer(SC*sc){++removeCalls;awdlCoexBaPeerAdded=false;awdlCoexControlPeerHint.clear();awdlCoexBaRequestsPending.clear();
 switch(behavior){case 1:return 5;case 2:++sc->sc_generation;break;case 3:++awdlCoexToken;break;case 4:awdlCoexBaPeerAdded=true;break;case 5:++awdlCoexBaPeerEpoch;break;case 6:nowUS+=2000001;break;case 7:known=false;break;case 8:nowUS=1;break;case 9:enabled=false;break;}return 0;}
static void task(){auto*sc=&hw;const int gen=sc->sc_generation;const uint32_t tok=awdlCoexToken;const uint64_t now=nowUS;auto stillOurs=[&](){return gen==sc->sc_generation&&tok==awdlCoexToken;};
'''+block+r'''
}
static const uint8_t target[6]={2,3,4,5,6,7},owner[6]={4,3,4,5,6,8};
static void reset(){hw={};enabled=known=awdlCoexBaPeerAdded=true;awdlCoexInFlight=awdlCoexResumePending=awdlCoexBaQueueAllocPending=false;awdlCoexToken=9;awdlCoexBaPeerEpoch=11;awdlCoexControlPeerRetired=awdlCoexControlPeerErrors=0;awdlCoexBaTxQueue=2;awdlCoexQueue=1;awdlCoexBaAnswerLength=0;awdlCoexDelbaWaitTid=-1;nowUS=10000000;awdlCoexBaPeerLastUS=6000000;softwareQueued=removeCalls=behavior=0;awdlCoexPeerHandoff={};awdlCoexBaRequestsPending.clear();awdlCoexControlPeerHint={};memcpy(awdlCoexBaPeer,owner,6);assert(awdlCoexControlPeerHint.note(target,nowUS,7,9,11));}
int main(){unsigned cases=0;
 for(unsigned guard=0;guard<6;++guard){reset();switch(guard){case 0:enabled=false;break;case 1:known=false;break;case 2:nowUS+=2000001;break;case 3:--nowUS;break;case 4:++hw.sc_generation;break;case 5:++awdlCoexBaPeerEpoch;break;}task();assert(!removeCalls&&!awdlCoexControlPeerHint.valid);++cases;}
 reset();++awdlCoexToken;task();assert(!removeCalls&&!awdlCoexControlPeerHint.valid);++cases;
 for(unsigned guard=0;guard<15;++guard){reset();switch(guard){case 0:softwareQueued=1;break;case 1:hw.txq[1].queued=1;break;case 2:hw.txq[2].queued=1;break;case 3:awdlCoexInFlight=true;break;case 4:awdlCoexBaAnswerLength=1;break;case 5:awdlCoexResumePending=true;break;case 6:awdlCoexBaQueueAllocPending=true;break;case 7:awdlCoexDelbaWaitTid=0;break;case 8:awdlCoexQueue=-1;break;case 9:awdlCoexQueue=4;break;case 10:awdlCoexBaTxQueue=-2;break;case 11:awdlCoexBaTxQueue=4;break;case 12:awdlCoexPeerHandoff.valid=true;break;case 13:{ItlAWDL::AddbaRequest r{};memcpy(r.peer,owner,6);r.immediate=true;assert(awdlCoexBaRequestsPending.note(r));break;}case 14:awdlCoexBaPeerLastUS=nowUS-2999999;break;}task();assert(!removeCalls&&awdlCoexControlPeerHint.valid&&!awdlCoexControlPeerRetired);++cases;}
 for(int action=1;action<=9;++action){reset();behavior=action;task();assert(removeCalls==1&&awdlCoexControlPeerErrors==1&&!awdlCoexControlPeerHint.valid&&!awdlCoexControlPeerRetired);++cases;}
 reset();awdlCoexBaPeerLastUS=nowUS-3000000;auto seen=awdlCoexControlPeerHint.seenUS;task();assert(removeCalls==1&&awdlCoexControlPeerRetired==1&&!awdlCoexControlPeerErrors&&awdlCoexControlPeerHint.valid&&awdlCoexControlPeerHint.seenUS==seen&&!awdlCoexBaPeerAdded&&awdlCoexBaRequestsPending.empty());assert(!memcmp(awdlCoexControlPeerHint.peer,target,6));++cases;
 reset();memcpy(awdlCoexBaPeer,target,6);task();assert(!removeCalls&&awdlCoexControlPeerHint.valid);++cases;
 reset();awdlCoexBaPeerAdded=false;task();assert(!removeCalls&&awdlCoexControlPeerHint.valid);++cases;
 printf("%u actual control-driven handoff cases passed\n",cases);
}
'''
p=T/'test-control-handoff.cpp';p.write_text(h)
with tempfile.TemporaryDirectory() as tmp:
 exe=Path(tmp)/'test';subprocess.run(['/usr/bin/clang++','-std=c++14','-fsanitize=address,undefined','-I'+str(R/'include'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
