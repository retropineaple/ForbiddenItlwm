"""Exercise exact production peer-selection block and hint lifecycle with firmware injection."""
from pathlib import Path
import subprocess,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text();base=(T/'fixtures/control-peer-before.cpp').read_text()
a=s.index('    if (!awdlCoexBaPeerAdded) {',s.index('// Detect outbound payload bursts'));b=s.index('    if (awdlCoexBaTxState == kCoexBaTxOff)',a);block=s[a:b]
a=base.index('    if (!awdlCoexBaPeerAdded) {',base.index('// Detect outbound payload bursts'));b=base.index('    if (awdlCoexBaTxState == kCoexBaTxOff)',a);old=base[a:b]
h=r'''
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <AWDLControlPeer.hpp>
static bool enabled,known,awdlCoexBaPeerAdded,awdlCoexBaPeerAccepted,current;
static ItlAWDL::ControlPeerHint awdlCoexControlPeerHint;
static uint32_t awdlCoexBaPeerEpoch=11;
static uint32_t awdlCoexBaTxPeerAdds,awdlCoexControlPeerErrors,awdlCoexControlPeerAdds;
static uint8_t awdlCoexTxDest[6],awdlCoexBaPeer[6],selected[6];
static struct {bool bulk;} awdlCoexBaTxBulk;
static unsigned calls,removes,aggReached;static int failure;static uint64_t now;static int gen;static uint32_t tok;
static bool awdlControlPeerEnabled(){return enabled;}
static bool awdlCoexPeerKnown(const uint8_t*,uint64_t){return known;}
static int awdlCoexBaAddPeer(void*,const uint8_t*p){++calls;memcpy(selected,p,6);if(failure)return failure;awdlCoexBaPeerAdded=true;awdlCoexBaPeerAccepted=false;memcpy(awdlCoexBaPeer,p,6);return 0;}
static int awdlCoexBaRemovePeer(void*){++removes;awdlCoexBaPeerAdded=false;return 0;}
static void task(){void*sc=nullptr;auto stillOurs=[](){return current;};
'''+block+r'''
 ++aggReached;
}
static void baseline(){void*sc=nullptr;auto stillOurs=[](){return current;};
'''+old+r'''
 ++aggReached;
}
static const uint8_t phone[6]={2,3,4,5,6,7},other[6]={4,3,4,5,6,8};
static void reset(){enabled=known=current=true;awdlCoexBaPeerAdded=awdlCoexBaPeerAccepted=false;awdlCoexControlPeerHint={};awdlCoexBaTxPeerAdds=awdlCoexControlPeerErrors=awdlCoexControlPeerAdds=calls=removes=aggReached=0;memcpy(awdlCoexTxDest,phone,6);memset(awdlCoexBaPeer,0,6);memset(selected,0,6);awdlCoexBaTxBulk.bulk=false;failure=0;now=10000000;gen=7;tok=9;}
int main(){unsigned cases=0;
 // Invalid, stale, wrong epoch, rolled-back clock and unknown peers never reach firmware.
 for(unsigned reason=0;reason<8;++reason){reset();assert(awdlCoexControlPeerHint.note(phone,now,gen,tok,awdlCoexBaPeerEpoch));
  switch(reason){case 0:enabled=false;break;case 1:known=false;break;case 2:now+=2000001;break;case 3:--now;break;case 4:++gen;break;case 5:++tok;break;case 6:awdlCoexControlPeerHint.clear();break;case 7:awdlCoexBaPeerAdded=true;memcpy(awdlCoexBaPeer,other,6);break;}
  task();assert(!calls&&!aggReached&&!awdlCoexControlPeerAdds);++cases;
 }
 // First known control creates only the station. It does not synthesize peer acceptance or aggregation.
 reset();assert(awdlCoexControlPeerHint.note(phone,now,gen,tok,awdlCoexBaPeerEpoch));task();assert(calls==1&&awdlCoexBaPeerAdded&&!awdlCoexBaPeerAccepted&&!aggReached&&awdlCoexControlPeerAdds==1&&!awdlCoexControlPeerHint.valid);++cases;
 for(int error:{1,5}){reset();awdlCoexControlPeerHint.note(phone,now,gen,tok,awdlCoexBaPeerEpoch);failure=error;task();assert(calls==1&&!aggReached&&awdlCoexControlPeerErrors==1&&!awdlCoexControlPeerAdds);++cases;}
 reset();awdlCoexControlPeerHint.note(phone,now,gen,tok,awdlCoexBaPeerEpoch);current=false;task();assert(calls==1&&removes==1&&!aggReached&&!awdlCoexControlPeerAdds);++cases;
 // Feature-off must match production baseline for the existing bulk/known/current/error guards.
 for(unsigned mask=0;mask<32;++mask){reset();enabled=false;known=mask&1;awdlCoexBaTxBulk.bulk=mask&2;current=mask&4;failure=mask&8?5:0;if(mask&16)memset(awdlCoexTxDest,0,6);
  task();auto c=calls,r=removes,a=aggReached,n=awdlCoexBaTxPeerAdds;uint8_t p[6];memcpy(p,selected,6);
  reset();enabled=false;known=mask&1;awdlCoexBaTxBulk.bulk=mask&2;current=mask&4;failure=mask&8?5:0;if(mask&16)memset(awdlCoexTxDest,0,6);baseline();
  assert(c==calls&&r==removes&&a==aggReached&&n==awdlCoexBaTxPeerAdds&&!memcmp(p,selected,6));++cases;
 }
 // One bounded slot coalesces the same target, refuses another current target, then expires.
 reset();assert(!awdlCoexControlPeerHint.note(nullptr,now,gen,tok,awdlCoexBaPeerEpoch));uint8_t invalid[6]{};assert(!awdlCoexControlPeerHint.note(invalid,now,gen,tok,awdlCoexBaPeerEpoch));invalid[0]=1;assert(!awdlCoexControlPeerHint.note(invalid,now,gen,tok,awdlCoexBaPeerEpoch));++cases;
 assert(awdlCoexControlPeerHint.note(phone,now,gen,tok,awdlCoexBaPeerEpoch));assert(awdlCoexControlPeerHint.note(phone,now+1,gen,tok,awdlCoexBaPeerEpoch));assert(!awdlCoexControlPeerHint.note(other,now+2,gen,tok,awdlCoexBaPeerEpoch));assert(awdlCoexControlPeerHint.current(now+2000001,gen,tok,awdlCoexBaPeerEpoch));assert(!awdlCoexControlPeerHint.current(now+2000002,gen,tok,awdlCoexBaPeerEpoch));assert(awdlCoexControlPeerHint.note(other,now+2000002,gen,tok,awdlCoexBaPeerEpoch));++cases;
 printf("%u actual control-peer selection/hint cases passed\n",cases);
}
'''
p=T/'test-control-peer.cpp';p.write_text(h)
with tempfile.TemporaryDirectory() as tmp:
 exe=Path(tmp)/'test';subprocess.run(['/usr/bin/clang++','-std=c++14','-fsanitize=address,undefined','-I'+str(R/'include'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
