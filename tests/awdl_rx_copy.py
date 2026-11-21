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
    uint64_t rxUnicastPackets=0,rxUnicastBytes=0,rxGroupPackets=0;
    bool dataDeliverEnabled=false;
    uint8_t localFrameAddress[6]={2,0,0,0,0,1};
    Loop loop;Loop *queueWorkLoop=&loop;
    void observeDataFrame(const uint8_t*,size_t);
    unsigned queueNotes=0;
    void noteQueueState(const char*) { ++queueNotes; }
    // llw0: what reaches it, recorded instead of queued.
    bool llwRegistered=false;void *llwRxPool=nullptr;void *llwQueues[4]={};
    unsigned llwDelivered=0,llwDeliverFailed=0,llwCalls=0;uint8_t llwLastDestinationIP=0;
    bool deliverEthernet(void*,void*,void*,const uint8_t*,const uint8_t*,const uint8_t *ip,size_t n) {
        assert(n>=40);++llwCalls;llwLastDestinationIP=ip[24];return true;
    }
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
    // Counted for IO80211 as its own data path counts them: these were all group frames.
    assert(deliver.rxGroupPackets==12 && !deliver.rxUnicastPackets && !deliver.rxUnicastBytes);
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
    // With llw0: a unique local destination is llw0's alone, a group frame goes to both, the rest to awdl0.
    {
        ItlAWDLInterface both;both.dataDeliverEnabled=true;both.llwRegistered=true;
        uint8_t local[84];memcpy(local,frame,84);
        uint8_t ula[40]={0x60};ula[24]=0xfd;size_t n;
        assert(ItlAWDL::buildIPv6DataPayload(ula,40,1,local+24,56,&n)==ItlAWDL::BuildResult::Ready);
        local[4]=2;local[5]=0;local[6]=0;local[7]=0;local[8]=0;local[9]=1;
        const unsigned queued=enqueues;
        both.observeDataFrame(local,84);
        assert(both.llwCalls==1 && both.llwDelivered==1 && both.llwLastDestinationIP==0xfd && enqueues==queued);
        both.observeDataFrame(frame,84);   // Group: llw0's copy and awdl0's.
        assert(both.llwCalls==2 && enqueues==queued+1 && both.dataDelivered==1);
        both.rx.allocated=both.rx.returned=0;both.rx.packet={};
        uint8_t plain[84];memcpy(plain,local,84);
        assert(ItlAWDL::buildIPv6DataPayload(ip,40,1,plain+24,56,&n)==ItlAWDL::BuildResult::Ready);
        both.observeDataFrame(plain,84);   // Link-local unicast: awdl0 only.
        assert(both.llwCalls==2 && enqueues==queued+2 && both.dataDelivered==2);
        // awdl0's own deliveries only (not llw0's): one group, one unicast of 14 + 40 bytes.
        assert(both.rxGroupPackets==1 && both.rxUnicastPackets==1 && both.rxUnicastBytes==54);
    }
    puts("bounded real-frame RX copy/delivery/cleanup: passed (host mocks only)");
}
'''
with tempfile.TemporaryDirectory(prefix='awdl-rx-copy-') as directory:
    p=Path(directory)/'test.cpp';p.write_text(program);e=Path(directory)/'test'
    subprocess.run(['xcrun','clang++','-std=c++14','-I'+str(root/'include'),'-fsanitize=address,undefined',str(p),'-o',str(e)],check=True)
    subprocess.run([str(e)],check=True)
