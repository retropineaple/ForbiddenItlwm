"""Actual bridge builder with modeled packet-header allocation semantics.

Tests construction/cleanup, not real IOKit, mbuf allocation or firmware.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'ForbiddenItlwm/AirportItlwmV2.cpp').read_text()
start=source.index('IOReturn AirportItlwm::buildAWDLSyncProbe(')
body=source[start:source.index('\n#if defined(ITL_AWDL_NATIVE)',start+100)]
program=r'''
#include <AWDLLocalSchedule.hpp>
#include <AWDLPeerProbe.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
static unsigned itlAWDLFixedChannel() { return 6; }
static unsigned itlAWDLChannelClass() { return 0; }
static bool itlAWDLTwoStreams() { return false; }
using IOReturn=unsigned;
enum { kIOReturnSuccess=0,kIOReturnBadArgument=1,kIOReturnNotReady=2,
       kIOReturnNoMemory=3,kIOReturnUnsupported=4,kIOReturnError=5,MBUF_DONTWAIT=0 };
#define IONew(T,n) new T[n]
#define IODelete(p,T,n) delete[] p
struct Packet { uint8_t bytes[2048]; size_t length=0,total=0; bool header=false; unsigned frees=0; };
using mbuf_t=Packet*;
static Packet storage; static bool failAlloc;
static int mbuf_getpacket(int,mbuf_t *p) {
    if(failAlloc) return 1; storage={};storage.header=true;*p=&storage;return 0;
}
static size_t mbuf_maxlen(mbuf_t) { return 2048; }
static void *mbuf_data(mbuf_t p) { return p->bytes; }
static void mbuf_setlen(mbuf_t p,size_t n) { p->length=n; }
static void mbuf_pkthdr_setlen(mbuf_t p,size_t n) { assert(p->header);p->total=n; }
static void mbuf_freem(mbuf_t p) { assert(p && !p->frees);++p->frees; }
struct Loop { bool gated=true; bool inGate() { return gated; } };
static Loop loop; static Loop *_fWorkloop=&loop;
static bool awdlExtendFrames=false;
struct ItlAWDLInterface {
    struct FrameConfiguration {
        struct { unsigned availability_window_length=16,availability_window_period=16; } parameters;
        bool hasRequestedTiming=true; uint8_t address[6]={2,0,0,0,0,1};
        size_t templateLength=110; uint8_t staticTLVs[1828]={};
    };
    unsigned references=0; bool ready=true,malformed=false;
    bool copyMasterIndication(uint8_t*,size_t,size_t*) { return false; }
    bool hasMasterIndication() const { return false; }
    void retain() { ++references; } void release() { assert(references);--references; }
    IOReturn copyFrameConfiguration(FrameConfiguration *p) {
        if(!ready) return kIOReturnNotReady;
        *p={}; p->staticTLVs[0]=malformed?18:254;p->staticTLVs[1]=107;return 0;
    }
};
struct AirportItlwm {
    uint16_t awdlProbeSequence=0;
    bool awdlInterfaceTest=true,awdlManagementInputEnabled=true;
    ItlAWDLInterface *fAWDLInterface=nullptr;
    template<typename... T> void setProperty(T...) {}
    IOReturn buildAWDLSyncProbe(mbuf_t*,uint64_t);
};
'''
program+=body
program+=r'''
int main() {
    for(unsigned mode=0;mode<4;++mode) {
        AirportItlwm controller;ItlAWDLInterface interface;controller.fAWDLInterface=&interface;
        interface.ready=mode!=1;interface.malformed=mode==2;failAlloc=mode==3;
        storage={};mbuf_t packet=nullptr;
        const auto status=controller.buildAWDLSyncProbe(&packet,10000000);
        assert(!interface.references);
        if(mode==0) {
            assert(status==0 && packet && packet->header && packet->length==226 && packet->total==226);
            ItlAWDL::ActionFrame decoded;
            assert(ItlAWDL::parseAction(packet->bytes,packet->length,&decoded)==ItlAWDL::ParseResult::Valid);
            assert(decoded.hasSync && decoded.masterChannel==6);
            mbuf_freem(packet);assert(storage.frees==1);
            packet=nullptr;
            assert(controller.buildAWDLSyncProbe(&packet,11000000)==0);
            assert(ItlAWDL::read16(packet->bytes+22)==16);
            mbuf_freem(packet);
        } else assert(status!=0 && !packet && !storage.frees);
    }
    puts("native sync bridge construction/header ownership: passed (host mocks only)");
}
'''
with tempfile.TemporaryDirectory(prefix='awdl-bridge-') as directory:
    src=Path(directory)/'test.cpp';src.write_text(program);exe=Path(directory)/'test'
    subprocess.run(['xcrun','clang++','-std=c++14','-DAIRPORT_WCL','-D__IO80211_TARGET=__MAC_26_0','-DITL_AWDL_NATIVE=1',
        '-I'+str(root/'include'),'-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
