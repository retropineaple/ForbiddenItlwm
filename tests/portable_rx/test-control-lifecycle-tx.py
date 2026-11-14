"""Actual sysctl/lifecycle and actual TX staging fragments, including mbuf ownership."""
from pathlib import Path
import subprocess,tempfile
T=Path(__file__).resolve().parent;R=T.parents[1];s=(R/'itlwm/hal_iwx/ItlIwx.cpp').read_text()
def func(signature):
 a=s.index(signature);b=s.index('{',a);n=1;e=b+1
 while n:n+=(s[e]=='{')-(s[e]=='}');e+=1
 return s[a:e]
functions='\n'.join(func(x) for x in ['static bool awdlControlPeerEnabled()','static int awdlControlPeerSysctl SYSCTL_HANDLER_ARGS','static int awdlControlRxSysctl SYSCTL_HANDLER_ARGS','static void awdlSingleControlAttach(ItlIwx *owner) {','static void awdlSingleControlDetach(ItlIwx *owner) {'])
a=s.index('        // Copy before iwx_tx takes ownership.');b=s.index('        auto *node =',a);pre=s[a:b]
a=s.index('        if (controlPeerCandidate && awdlControlPeerEnabled()');b=s.index(';',a)+1;post=s[a:b]
assert s.index('        if (err) {',s.index('const int err = iwx_tx(sc, next,'))<a
assert 'static int awdlCoexControlPeerEnabled = 0;' in s
h=r'''
#include <cassert>
#include <cstdio>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <AWDLControlPeer.hpp>
#include <AWDLControlReceiveProbe.hpp>
#include <AWDLPeerHandoff.hpp>
struct ItlIwx{};struct sysctl_oid{};struct sysctl_req{void*newptr;int value,error;};
#define SYSCTL_HANDLER_ARGS (sysctl_oid*oidp,void*arg1,int arg2,sysctl_req*req)
static int sysctl_handle_int(sysctl_oid*,int*p,int,sysctl_req*r){if(r->error)return r->error;if(r->newptr)*p=r->value;return 0;}
static int awdlCoexControlRxEnabled;
static bool awdlCoexSendingControlRx,awdlCoexControlRxRestorePending;
static ItlAWDL::ControlReceiveProbe awdlCoexControlRxProbe;
static int sysctl__debug_forbidden_awdl_control_rx;
static int awdlCoexControlPeerEnabled,awdlCoexSingleLegacy=1,awdlCoexPeerHandoffEnabled=1;
static ItlIwx*awdlSingleControlOwner;
static ItlAWDL::PeerHandoff awdlCoexPeerHandoff;
static ItlAWDL::ControlPeerHint awdlCoexControlPeerHint;
static int sysctl__debug_forbidden_awdl_control_peer,sysctl__debug_forbidden_awdl_single_legacy,sysctl__debug_forbidden_awdl_peer_handoff,registered;
static void sysctl_register_oid(int*){++registered;}static void sysctl_unregister_oid(int*){--registered;}
static bool awdlCoexAdded=true,awdlCoexBaPeerAdded;
static uint8_t awdlCoexBaPeer[6]{};
static uint32_t awdlCoexBaPeerEpoch=11,awdlCoexToken=9,awdlCoexControlPeerRequests;static uint64_t awdlCoexLastDataUS=100;
'''+functions+r'''
struct MBuf{uint8_t bytes[600]{};size_t length=120;bool chain=false;};
static MBuf*mbuf_next(MBuf*m){return m->chain?m:nullptr;}static size_t mbuf_len(MBuf*m){return m->length;}
static void send(unsigned reason){
 MBuf*next=new MBuf;next->bytes[0]=0x08;next->bytes[4]=2;next->bytes[9]=3;
 switch(reason){case 1:awdlCoexControlPeerEnabled=0;break;case 2:awdlCoexBaPeerAdded=true;memcpy(awdlCoexBaPeer,next->bytes+4,6);break;case 3:next->bytes[4]=1;break;case 4:next->chain=true;break;case 5:next->bytes[0]=0x88;break;case 6:next->length=513;break;case 7:next->length=23;break;case 8:next->length=512;break;case 16:awdlCoexBaPeerAdded=true;awdlCoexBaPeer[0]=4;break;}
 const uint8_t*nextBytes=next->bytes;const bool unicast=next->length>=24&&!(nextBytes[4]&1);const int gen=7;const uint32_t controlPeerToken=awdlCoexToken;const uint32_t controlPeerEpoch=awdlCoexBaPeerEpoch;struct{int sc_generation=7;}hardware;auto*sc=&hardware;
'''+pre+r'''
 // iwx_tx owns/frees this memory; subsequent staging must use only the copied address.
 delete next;
 if(reason==9)return; // transport failure: real caller continues before the staging point.
 switch(reason){case 10:awdlCoexControlPeerEnabled=0;break;case 11:awdlCoexAdded=false;break;case 12:++sc->sc_generation;break;case 13:++awdlCoexToken;break;case 14:awdlCoexBaPeerAdded=true;++awdlCoexBaPeerEpoch;break;case 15:++awdlCoexBaPeerEpoch;break;}
'''+post+r'''
}
int main(){unsigned cases=0;ItlIwx a,b;awdlCoexControlRxEnabled=0;awdlCoexControlPeerEnabled=1;awdlSingleControlAttach(&a);assert(registered==4&&awdlCoexControlRxEnabled==1&&awdlCoexControlPeerEnabled==0&&awdlCoexSingleLegacy==1&&awdlCoexPeerHandoffEnabled==1);++cases;
 sysctl_oid oid;sysctl_req req{nullptr,1,0};assert(!awdlControlPeerSysctl(&oid,nullptr,0,&req)&&awdlCoexControlPeerEnabled==0);req.newptr=&req;
 for(int value:{0,1}){req.value=value;assert(!awdlControlPeerSysctl(&oid,nullptr,0,&req)&&awdlCoexControlPeerEnabled==value);++cases;}
 for(int value:{-1,2,INT32_MIN,INT32_MAX}){req.value=value;assert(awdlControlPeerSysctl(&oid,nullptr,0,&req)==EINVAL&&awdlCoexControlPeerEnabled==1);++cases;}
 req.error=EFAULT;req.value=0;assert(awdlControlPeerSysctl(&oid,nullptr,0,&req)==EFAULT&&awdlCoexControlPeerEnabled==1);++cases;
 awdlSingleControlAttach(&b);assert(registered==4&&awdlCoexControlPeerEnabled==1);awdlSingleControlDetach(&b);assert(registered==4);awdlCoexControlRxEnabled=1;const uint8_t rxPeer[6]={2,3,4,5,6,7};assert(awdlCoexControlRxProbe.arm(rxPeer,100,7,9,149));awdlCoexControlRxRestorePending=true;awdlSingleControlDetach(&a);assert(!awdlCoexControlRxEnabled&&!awdlCoexControlRxProbe.armed&&!awdlCoexControlRxRestorePending&&!awdlCoexSendingControlRx);assert(registered==0&&awdlCoexControlPeerEnabled==0&&!awdlCoexControlPeerHint.valid);++cases;
 for(unsigned reason=0;reason<17;++reason){awdlCoexControlPeerHint={};awdlCoexControlPeerEnabled=1;awdlCoexBaPeerAdded=false;awdlCoexAdded=true;awdlCoexToken=9;awdlCoexBaPeerEpoch=11;memset(awdlCoexBaPeer,0,6);awdlCoexControlPeerRequests=0;send(reason);bool expected=reason==0||reason==8||reason==16;assert(awdlCoexControlPeerHint.valid==expected&&awdlCoexControlPeerRequests==unsigned(expected));++cases;}
 req.error=0;req.newptr=nullptr;req.value=1;awdlCoexControlRxEnabled=0;assert(!awdlControlRxSysctl(&oid,nullptr,0,&req)&&awdlCoexControlRxEnabled==0);++cases;req.newptr=&req;
 for(int v:{0,1}){req.value=v;assert(!awdlControlRxSysctl(&oid,nullptr,0,&req)&&awdlCoexControlRxEnabled==v);++cases;}
 for(int v:{-1,2,INT32_MIN,INT32_MAX}){req.value=v;assert(awdlControlRxSysctl(&oid,nullptr,0,&req)==EINVAL&&awdlCoexControlRxEnabled==1);++cases;}
 req.error=EFAULT;req.value=0;assert(awdlControlRxSysctl(&oid,nullptr,0,&req)==EFAULT&&awdlCoexControlRxEnabled==1);++cases;
 printf("%u actual control-peer sysctl/lifecycle/TX ownership cases passed\n",cases);
}
'''
p=T/'test-control-lifecycle-tx.cpp';p.write_text(h)
with tempfile.TemporaryDirectory() as tmp:
 exe=Path(tmp)/'test';subprocess.run(['/usr/bin/clang++','-std=c++14','-fsanitize=address,undefined','-I'+str(R/'include'),str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
