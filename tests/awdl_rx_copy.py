"""Actual bounded RX-copy method with native-pool lifecycle mocks."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
# Reuse the existing preparation-sensitive pool fixtures without running them.
setup=(root/'tests/awdl_pool_lease.py').read_text().split('with tempfile.TemporaryDirectory')[0]
ns={'__file__':str(root/'tests/awdl_pool_lease.py')};exec(setup,ns)
program=ns['program'].split('int main()')[0]
program='#include <AWDLDataFrame.hpp>\n'+program
program=program.replace('kIOReturnUnsupported=1','kIOReturnUnsupported=1,kIOReturnNotReady=2')
program=program.replace('bytes[56]','bytes[2048]').replace('!allocated','allocated==returned')
program=program.replace('++allocated;*p=&packet','packet={};++allocated;*p=&packet')
program=program.replace('!returned','returned+1==allocated')
program+=r'''
struct Loop { bool inGate() { return true; } };
static IOReturn enqueueResult=0; static unsigned enqueues=0,headroomCalls=0,linkCalls=0,lastLink=0;
static ItlSkywalkPacketView *enqueued=nullptr;
static IOReturn itlPacketSetHeadroom(ItlSkywalkPacketView*,unsigned char h) { assert(!h);++headroomCalls;return 0; }
static IOReturn itlPacketSetLinkHeaderLength(ItlSkywalkPacketView*,unsigned char n) { ++linkCalls;lastLink=n;return 0; }
static IOReturn itlRxEnqueue(void *q,ItlSkywalkPacketView *const *p,unsigned n,unsigned o) {
    assert(q && p && *p && n==1 && !o);++enqueues;
    if(!enqueueResult) enqueued=*p;
    return enqueueResult;
}
struct ItlAWDLInterface:Owner {
    bool dataCopyProbeEnabled=true,nativeStarted=true;
    unsigned dataCopyAttempts=0,dataCopySuccess=0,dataDelivered=0,dataDeliverFailed=0;
    bool dataDeliverEnabled=false;
    uint8_t localFrameAddress[6]={2,0,0,0,0,1};
    Loop loop;Loop *queueWorkLoop=&loop;
    void observeDataFrame(const uint8_t*,size_t);
    unsigned queueNotes=0;
    void noteQueueState(const char*) { ++queueNotes; }
    bool peerAliveWanted=false;unsigned keepCalls=0;uint8_t keepLast[6]={};
    void keepPeerAlive(const uint8_t*peer) { ++keepCalls; memcpy(keepLast,peer,6); }
};
'''
source=(root/'ForbiddenItlwm/AirportItlwmV2.cpp').read_text()
a=source.index('void ItlAWDLInterface::observeDataFrame(')
b=source.index('\nIOReturn ItlAWDLInterface::',a)
program+=source[a:b]
program+=r'''
int main() {
    uint8_t frame[84]={8},ip[40]={0x60};
    memset(frame+4,255,6);frame[10]=2;frame[11]=1;
    const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};memcpy(frame+16,bssid,6);
    size_t length;
    assert(ItlAWDL::buildIPv6DataPayload(ip,40,1,frame+24,56,&length)==ItlAWDL::BuildResult::Ready);
    ItlAWDLInterface interface;failPreparation=false;
    interface.observeDataFrame(frame,10);assert(!interface.dataCopyAttempts);
    frame[4]=2;interface.observeDataFrame(frame,84);assert(!interface.dataCopyAttempts);frame[4]=255;
    for(unsigned i=0;i<12;++i) interface.observeDataFrame(frame,84);
    assert(interface.dataCopyAttempts==8 && interface.dataCopySuccess==8);
    assert(interface.rx.allocated==8 && interface.rx.returned==8 && !interface.rx.packet.prepared);
    for(auto b:interface.rx.packet.bytes) assert(!b);
    ItlAWDLInterface failed;failPreparation=true;
    failed.observeDataFrame(frame,84);
    assert(failed.dataCopyAttempts==1 && !failed.dataCopySuccess && failed.rx.returned==1);
    // Delivery: the queue takes the packet with its bytes intact, and the driver
    // neither completes nor returns it. No limit of eight applies.
    ItlAWDLInterface deliver;failPreparation=false;deliver.dataDeliverEnabled=true;
    for(unsigned i=1;i<=12;++i) {
        deliver.observeDataFrame(frame,84);
        assert(deliver.dataDelivered==i && enqueues==i && enqueued==&deliver.rx.packet);
        assert(lastLink==14 && headroomCalls==i && linkCalls==i);
        assert(!deliver.rx.returned && deliver.rx.packet.prepared && deliver.rx.packet.length==54);
        assert(deliver.rx.packet.bytes[12]==0x86 && deliver.rx.packet.bytes[13]==0xdd);
        // Stand in for the queue disposing the packet.
        deliver.rx.allocated=deliver.rx.returned=0;deliver.rx.packet={};
    }
    assert(!deliver.dataDeliverFailed);
    // A refused enqueue consumes nothing, so the normal cleanup still runs.
    enqueueResult=7;
    deliver.observeDataFrame(frame,84);
    assert(deliver.dataDeliverFailed==1 && deliver.dataDelivered==12 && deliver.rx.returned==1 && deliver.queueNotes==1);
    assert(!deliver.rx.packet.prepared && deliver.rx.packet.length==0);
    for(auto b:deliver.rx.packet.bytes) assert(!b);
    enqueueResult=0;
    // Frames to someone else are never delivered.
    ItlAWDLInterface other;other.dataDeliverEnabled=true;const unsigned before=enqueues;
    frame[4]=2;other.observeDataFrame(frame,84);assert(enqueues==before && !other.dataCopyAttempts);frame[4]=255;
    // -itlawdlpeeralive: only unicast to us from a peer (not multicast, not our own echoes) reaches keepPeerAlive, with the source.
    ItlAWDLInterface alive;alive.dataDeliverEnabled=true;alive.peerAliveWanted=true;
    alive.observeDataFrame(frame,84);assert(!alive.keepCalls);                     // multicast destination
    alive.rx.allocated=alive.rx.returned=0;alive.rx.packet={};                       // the queue disposed the packet
    memcpy(frame+4,alive.localFrameAddress,6);alive.observeDataFrame(frame,84);
    assert(alive.keepCalls==1 && !memcmp(alive.keepLast,frame+10,6));                // unicast to us
    ItlAWDLInterface off;off.dataDeliverEnabled=true;off.observeDataFrame(frame,84);assert(!off.keepCalls);
    puts("bounded real-frame RX copy/delivery/cleanup: passed (host mocks only)");
}
'''
with tempfile.TemporaryDirectory(prefix='awdl-rx-copy-') as directory:
    p=Path(directory)/'test.cpp';p.write_text(program);e=Path(directory)/'test'
    subprocess.run(['xcrun','clang++','-std=c++14','-I'+str(root/'include'),'-fsanitize=address,undefined',str(p),'-o',str(e)],check=True)
    subprocess.run([str(e)],check=True)
