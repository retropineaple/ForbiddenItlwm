#include <AWDLBlockAck.hpp>
#include <AWDLServiceMerge.hpp>
#include <AWDLDataFrame.hpp>
#include <AWDLObservation.hpp>
#include <AWDLPeerProbe.hpp>
#include <AWDLProbePolicy.hpp>
#include <cassert>
#include <utility>
#include <vector>
#include <cstdio>
#include <cstring>
#include <initializer_list>
using namespace ItlAWDL;
static const uint8_t own[6]={6,0xd5,0xd2,0x49,0x15,0xa7},peer[6]={2,1,2,3,4,5},zero[6]={};
static const uint8_t group[6]={0x33,0x33,0,0,0,0xfb};
static void action(uint8_t *f,const uint8_t *receiver,const uint8_t *source) {
    memset(f,0,120);f[0]=0xd0;memcpy(f+4,receiver,6);memcpy(f+10,source,6);
}
static DataFrameView data(const uint8_t *ip,size_t length,const uint8_t *source,const uint8_t *destination) {
    DataFrameView v={};v.ip.bytes=ip;v.ip.length=length;v.source=source;v.destination=destination;return v;
}
static void testBlockAck() {
    uint8_t q[40];
    memset(q,0,sizeof(q));
    q[0]=0xd0;memcpy(q+4,own,6);memcpy(q+10,peer,6);q[16]=0x77;
    q[24]=3;q[25]=0;q[26]=9;
    // Immediate, A-MSDU allowed, TID 5, window 256.
    const uint16_t params=uint16_t(1|2|5<<2|256<<6);
    q[27]=uint8_t(params);q[28]=uint8_t(params>>8);
    const uint16_t ssc=uint16_t(100<<4);
    q[31]=uint8_t(ssc);q[32]=uint8_t(ssc>>8);
    AddbaRequest r;
    assert(parseAddbaRequest(q,33,own,&r) && r.token==9 && r.tid==5 && r.immediate && r.ssn==100);
    assert(r.window==kBaWindowMax && !memcmp(r.peer,peer,6) && r.bssid[0]==0x77);
    // Too short, to someone else, or another action.
    assert(!parseAddbaRequest(q,32,own,&r) && !parseAddbaRequest(q,33,peer,&r));
    q[25]=1;assert(!parseAddbaRequest(q,33,own,&r));q[25]=0;
    q[24]=0x7f;assert(!parseAddbaRequest(q,33,own,&r));q[24]=3;
    // A TID that isn't a user priority.
    q[27]=uint8_t((params&~(15U<<2))|8U<<2);assert(!parseAddbaRequest(q,33,own,&r));q[27]=uint8_t(params);
    // A small window is kept, and zero means the largest.
    q[27]=uint8_t(2|5<<2|8<<6);q[28]=uint8_t((2|5<<2|8<<6)>>8);
    assert(parseAddbaRequest(q,33,own,&r) && r.window==8);
    q[27]=uint8_t(2|5<<2);q[28]=0;
    assert(parseAddbaRequest(q,33,own,&r) && r.window==kBaWindowMax);
    uint8_t a[kAddbaResponseLength];
    assert(!buildAddbaResponse(a,sizeof(a)-1,r,own,1));
    assert(buildAddbaResponse(a,sizeof(a),r,own,7)==33);
    // The send path's check takes this and nothing near it.
    assert(isAddbaFrame(a,33) && !isAddbaFrame(a,32) && !isAddbaFrame(nullptr,33));
    {
        // DELBA for an idle receive session: recipient side (initiator bit clear), TID 5, reason 39, and our
        // own parser reads it back. Too little room or a bad TID writes nothing.
        uint8_t d[kDelbaLength]; const uint8_t bss[6]={0,0x25,0,0xff,0x94,0x73};
        assert(!buildDelba(d,sizeof(d)-1,peer,own,bss,5,false,39,3) && !buildDelba(d,sizeof(d),peer,own,bss,8,false,39,3));
        assert(buildDelba(d,sizeof(d),peer,own,bss,5,false,39,3)==30 && isDelbaFrame(d,30) && !isDelbaFrame(d,33) && !isAddbaFrame(d,30));
        assert(d[0]==0xd0 && !memcmp(d+4,peer,6) && !memcmp(d+10,own,6) && d[24]==3 && d[25]==2);
        assert((d[26]|d[27]<<8)==(5<<12) && (d[28]|d[29]<<8)==39 && (d[22]|d[23]<<8)==3<<4);
        bool wasSender=true; uint8_t dt=0;
        assert(parseDelba(d,30,peer,&wasSender,&dt) && !wasSender && dt==5);
        assert(buildDelba(d,sizeof(d),peer,own,bss,2,true,37,4)==30 && ((d[26]|d[27]<<8)&(1U<<11)));
    }
    assert(a[0]==0xd0 && !memcmp(a+4,peer,6) && !memcmp(a+10,own,6) && a[16]==0x77);
    assert((a[22]|a[23]<<8)==7<<4 && a[24]==3 && a[25]==1 && a[26]==9 && !a[27] && !a[28]);
    const unsigned answer=a[29]|a[30]<<8;
    assert(!(answer&1) && (answer&2) && ((answer>>2)&15)==5 && answer>>6==kBaWindowMax && !a[31] && !a[32]);
}
static void testPendingAddbaRequests() {
    PendingAddbaRequests pending;
    AddbaRequest first{}, next{}, result{};
    memcpy(first.peer, peer, sizeof(first.peer));
    first.immediate = true;
    first.tid = 2;
    first.token = 7;
    next = first;
    next.tid = 5;
    next.token = 9;

    assert(pending.note(first) && pending.note(next));
    first.token = 8;
    assert(pending.note(first));
    next.immediate = false;
    assert(!pending.note(next));
    next.immediate = true;
    memcpy(next.peer, own, sizeof(next.peer));
    assert(!pending.note(next));

    // TID 2 waits for a DELBA. TID 5 is processed without losing TID 2.
    assert(pending.take(1U << 2, result) && result.tid == 5 && result.token == 9);
    assert(pending.has(2) && !pending.take(1U << 2, result));
    assert(pending.take(0, result) && result.tid == 2 && result.token == 8 && pending.empty());
    assert(pending.note(first));
    pending.discard(2);
    assert(pending.empty());
    assert(pending.note(first));
    pending.keepPeer(own);
    assert(pending.empty() && pending.note(next));
}

static void testBlockAckSend() {
    const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};
    uint8_t q[kAddbaResponseLength];
    assert(!buildAddbaRequest(q,sizeof(q)-1,peer,own,bssid,1,0,64,0,1));
    assert(!buildAddbaRequest(q,sizeof(q),peer,own,bssid,1,8,64,0,1));
    assert(buildAddbaRequest(q,sizeof(q),peer,own,bssid,0x5a,3,64,100,7)==33 && isAddbaFrame(q,33));
    assert(q[0]==0xd0 && !memcmp(q+4,peer,6) && !memcmp(q+10,own,6) && !memcmp(q+16,bssid,6));
    assert(q[24]==3 && !q[25] && q[26]==0x5a && (q[22]|q[23]<<8)==7<<4);
    const unsigned p=q[27]|q[28]<<8;
    assert(!(p&1) && (p&2) && ((p>>2)&15)==3 && p>>6==64 && !q[29] && !q[30] && (q[31]|q[32]<<8)==100<<4);
    // Read as the peer would, it's our request.
    AddbaRequest r;
    assert(parseAddbaRequest(q,33,peer,&r) && r.token==0x5a && r.tid==3 && r.window==64 && r.ssn==100 && r.immediate);
    // The peer's answer, which is the same shape as ours.
    uint8_t a[kAddbaResponseLength];
    assert(buildAddbaResponse(a,sizeof(a),r,peer,1)==33);
    AddbaAnswer ans;
    assert(parseAddbaAnswer(a,33,own,&ans) && ans.token==0x5a && !ans.status && ans.tid==3 && ans.window==64);
    a[27]=37;assert(parseAddbaAnswer(a,33,own,&ans) && ans.status==37);
    // Policy bit: immediate is read; clearing it (delayed Block Ack) shows as not immediate.
    assert(ans.immediate); a[29]&=uint8_t(~2U); assert(parseAddbaAnswer(a,33,own,&ans) && !ans.immediate); a[29]|=2U;
    {
        // One TX response that reclaims a sync frame and two management frames (slots 10..12 of 256): the status
        // goes only to the slot the header index names; no index match means no slot (the caller falls back).
        assert(reclaimStatusSlot(10,3,256,11)==11 && reclaimStatusSlot(10,3,256,12)==12);
        assert(reclaimStatusSlot(10,3,256,13)==-1 && reclaimStatusSlot(10,3,256,9)==-1);
        // Wrapping past the end, and a ring larger than 8 bits takes the newest match.
        assert(reclaimStatusSlot(254,4,256,1)==1 && reclaimStatusSlot(250,2,512,250)==250);
        assert(reclaimStatusSlot(0,0,256,0)==-1 && reclaimStatusSlot(0,257,256,0)==-1 && reclaimStatusSlot(5,1,0,5)==-1);
    }
    assert(!parseAddbaAnswer(a,33,peer,&ans) && !parseAddbaAnswer(a,32,own,&ans) && !parseAddbaAnswer(q,33,peer,&ans));
    // Ending a session.
    a[25]=2;
    bool sender=false;uint8_t dt=9;
    // The initiator bit and TID 5.
    a[26]=0;a[27]=uint8_t((1U<<3)|(5U<<4));
    assert(parseDelba(a,33,own,&sender,&dt) && sender && dt==5);
    a[27]=uint8_t(5U<<4);
    assert(parseDelba(a,33,own,&sender,&dt) && !sender && dt==5);
    assert(!parseDelba(a,33,peer,&sender,&dt) && !parseDelba(a,27,own,&sender,&dt));
    // Data to QoS: 2 bytes longer, the body moved along.
    uint8_t d[30],o2[40];
    memset(d,0,sizeof(d));d[0]=0x08;d[24]=0xaa;d[29]=0xbb;
    assert(buildQosData(d,30,5,0x123,o2,sizeof(o2))==32);
    assert(o2[0]==0x88 && (o2[22]|o2[23]<<8)==0x123<<4 && o2[24]==5 && !o2[25] && o2[26]==0xaa && o2[31]==0xbb);
    assert(!buildQosData(d,30,5,0,o2,31) && !buildQosData(d,23,5,0,o2,40));
    d[0]=0x88;assert(!buildQosData(d,30,5,0,o2,40));
}
static void testNoFcsOuterHeader() {
    // A QoS data frame with the outer header and no FCS, as the firmware gives it with the filter off.
    uint8_t f[26+8+16+48];
    memset(f,0,sizeof(f));
    f[0]=0x88;memcpy(f+4,own,6);memcpy(f+10,peer,6);
    const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};memcpy(f+16,bssid,6);
    const uint8_t outer[6]={0xaa,0xaa,3,0,0,0};memcpy(f+26,outer,6);
    f[33]=16+48;
    const uint8_t llc[16]={0xaa,0xaa,3,0,0x17,0xf2,8,0,3,4,0,0,0,0,0x86,0xdd};memcpy(f+34,llc,16);
    f[50]=0x60;f[55]=8;f[56]=17;f[57]=64;
    DataFrameView v;
    // Taken as having an FCS, the outer length no longer matches, which is why the retry covers this.
    assert(parseDataFrame(f,sizeof(f),true,&v)==ParseResult::Unsupported);
    assert(parseDataFrame(f,sizeof(f),false,&v)==ParseResult::Valid && v.ip.length==48);
    // A longer length with the FCS flag reads the same bytes, and doesn't touch the extra 4.
    assert(parseDataFrame(f,sizeof(f)+4,true,&v)==ParseResult::Valid && v.ip.length==48);
}
static void testServiceDigest() {
    uint8_t f[80];memset(f,0,sizeof(f));
    // Two TLV 2 entries and one other, from byte 40.
    f[40]=2;f[41]=2;f[43]=0xaa;f[44]=0xbb;
    f[45]=7;f[46]=1;f[48]=0x11;
    f[49]=2;f[50]=1;f[52]=0xcc;
    uint32_t d=0,b=0;
    assert(digestActionTLVs(f,53,2,&d,&b)==2 && b==3 && d);
    const uint32_t first=d;
    f[52]=0xcd;assert(digestActionTLVs(f,53,2,&d,&b)==2 && d!=first);
    // None of that type, and a TLV running past the end stops the walk.
    assert(!digestActionTLVs(f,53,9,&d,&b) && !d && !b);
    f[50]=40;assert(digestActionTLVs(f,53,2,&d,&b)==1 && b==2);
    // The log keeps changes only, for each side.
    static TlvChangeLog log;
    log.note(1,false,1,10,5);log.note(2,false,1,10,5);log.note(3,true,1,10,5);log.note(4,false,2,20,6);
    assert(log.total==3 && log.entries[1].fromPeer && log.entries[2].ms==4);
    for(unsigned i=0;i<200;++i) log.note(10+i,i&1,1,1,i);
    assert(log.total==203);
    // Snapshots: only frames with TLV 2, and only on a change, with each value's length first.
    static ServiceSnapshotLog snap;
    uint8_t g[80];memset(g,0,sizeof(g));
    g[40]=7;g[41]=1;
    snap.note(1,true,g,44);
    assert(!snap.total);
    g[44]=2;g[45]=2;g[47]=0x5f;g[48]=0x61;
    snap.note(2,true,g,49);snap.note(3,true,g,49);
    assert(snap.total==1 && snap.dirty && snap.entries[0].fromPeer && snap.entries[0].count==1);
    assert(snap.entries[0].length==4 && snap.entries[0].bytes[0]==2 && snap.entries[0].bytes[2]==0x5f && snap.entries[0].bytes[3]==0x61);
    snap.note(4,false,g,49);
    assert(snap.total==2 && !snap.entries[1].fromPeer);
    for(unsigned i=0;i<40;++i){g[48]=uint8_t(i);snap.note(10+i,true,g,49);}
    assert(snap.total==42);
}
// Builds an action frame from TLVs: type, then value bytes.
static size_t svcFrame(uint8_t *f,const uint8_t *from,std::initializer_list<std::pair<uint8_t,std::vector<uint8_t>>> tlvs) {
    memset(f,0,40);f[0]=0xd0;memcpy(f+10,from,6);
    size_t o=40;
    for(auto &t:tlvs){f[o]=t.first;f[o+1]=uint8_t(t.second.size());f[o+2]=0;memcpy(f+o+3,t.second.data(),t.second.size());o+=3+t.second.size();}
    return o;
}
static void testServiceMerge() {
    const std::vector<uint8_t> sui100={0,0,0,100,0,0,0,0,0},sui101={0,0,0,101,0,0,0,0,0};
    const std::vector<uint8_t> a={1,2,3},b={4,5,6,7},c={8};
    uint8_t f1[200],f2[200],out[400];
    uint16_t s=0;
    size_t n1=svcFrame(f1,peer,{{4,{9,9}},{6,sui100},{2,a},{2,b}});
    assert(readServiceUpdateIndicator(f1,n1,&s) && s==100);
    static PeerServiceCache c1;c1.clear();
    c1.note(peer,f1,n1);
    assert(c1.valid && c1.sui==100 && c1.records==2);
    // A later frame with the same SUI and a different subset: the missing records go after its TLVs.
    size_t n2=svcFrame(f2,peer,{{6,sui100},{2,c}});
    c1.note(peer,f2,n2);
    assert(c1.records==3);
    unsigned added=0;
    size_t m=c1.merge(peer,f2,n2,out,sizeof(out),&added);
    assert(added==2 && m==n2+(3+3)+(3+4) && !memcmp(out,f2,n2));
    assert(out[n2]==2 && out[n2+1]==3 && out[n2+3]==1 && out[n2+6]==2 && out[n2+7]==4);
    // The merged frame parses to its end, with TLV 6 first and the SUI unchanged.
    assert(readServiceUpdateIndicator(out,m,&s) && s==100);
    { uint32_t d=0,b2=0; assert(digestActionTLVs(out,m,2,&d,&b2)==3); }
    // A frame that already has everything gets nothing.
    size_t n3=svcFrame(f2,peer,{{6,sui100},{2,a},{2,b},{2,c}});
    assert(!c1.merge(peer,f2,n3,out,sizeof(out),&added) && !added);
    // Another peer, another SUI, no TLV 6, or no room: nothing.
    assert(!c1.merge(own,f1,n1,out,sizeof(out),&added));
    size_t n4=svcFrame(f2,peer,{{6,sui101},{2,c}});
    assert(!c1.merge(peer,f2,n4,out,sizeof(out),&added));
    size_t n5=svcFrame(f2,peer,{{2,c}});
    assert(!c1.merge(peer,f2,n5,out,sizeof(out),&added));
    size_t n6=svcFrame(f2,peer,{{6,sui100},{2,c}});
    assert(!c1.merge(peer,f2,n6,out,n6+3,&added));
    // A new SUI starts again.
    n4=svcFrame(f2,peer,{{6,sui101},{2,c}});
    c1.note(peer,f2,n4);
    assert(c1.sui==101 && c1.records==1 && c1.resets==1);
    // A frame whose TLVs don't parse to the end isn't touched.
    size_t n7=svcFrame(f2,peer,{{6,sui101},{2,a}});
    f2[n7-5]=40;
    assert(!c1.merge(peer,f2,n7,out,sizeof(out),&added));
}
static void testBaTxBulk() {
    BaTxBulkState b{};
    const uint64_t s=kBaFilterCheckUS;
    uint32_t n=0;
    assert(!noteBaTxTraffic(b,s,n) && !b.bulk);
    // Connection setup: a few dozen frames a second stays off.
    n+=60;assert(!noteBaTxTraffic(b,2*s,n) && !b.bulk);
    n+=kBaTxBulkFrames-1;assert(!noteBaTxTraffic(b,3*s,n) && !b.bulk);
    // A bulk second turns it on, and a single slow second doesn't turn it off.
    n+=kBaTxBulkFrames;assert(noteBaTxTraffic(b,4*s,n) && b.bulk);
    n+=5;assert(!noteBaTxTraffic(b,5*s,n) && b.bulk);
    n+=kBaTxQuietFrames;assert(!noteBaTxTraffic(b,6*s,n) && b.bulk);
    // Two quiet seconds in a row turn it off.
    n+=1;assert(!noteBaTxTraffic(b,7*s,n) && b.bulk);
    n+=1;assert(noteBaTxTraffic(b,8*s,n) && !b.bulk);
    // Nothing changes between checks.
    n+=5000;assert(!noteBaTxTraffic(b,8*s+10,n) && !b.bulk);
    BaTxBulkState fast{};
    assert(!noteBaTxTraffic(fast,s,0,true));
    assert(!noteBaTxTraffic(fast,s+s/4-1,50,true));
    assert(noteBaTxTraffic(fast,s+s/4,50,true) && fast.bulk);
    assert(!noteBaTxTraffic(fast,s+s/2,50,true) && fast.quietChecks==0);
    assert(!noteBaTxTraffic(fast,2*s+s/4,50,true) && fast.bulk);
    assert(noteBaTxTraffic(fast,3*s+s/4,50,true) && !fast.bulk);
}
static void testBaFilter() {
    BaFilterState f{};
    const uint64_t s=kBaFilterCheckUS;
    uint32_t n=0;
    // The first call only starts the count, and nothing changes between checks.
    assert(!noteBaFilterTraffic(f,s,n) && !f.off);
    n+=500;assert(!noteBaFilterTraffic(f,s+1,n) && !f.off);
    // Light traffic, like setting up a send, leaves the filter on.
    f=BaFilterState{};n=0;noteBaFilterTraffic(f,s,n);
    n+=kBaFilterBusyFrames-1;assert(!noteBaFilterTraffic(f,2*s,n) && !f.off);
    // A busy second turns it off.
    n+=kBaFilterBusyFrames;assert(noteBaFilterTraffic(f,3*s,n) && f.off);
    // It stays off through a short gap, as the peer leaves the channel.
    assert(!noteBaFilterTraffic(f,4*s,n) && f.off);
    assert(!noteBaFilterTraffic(f,5*s,n) && f.off);
    n+=kBaFilterQuietFrames;assert(!noteBaFilterTraffic(f,6*s,n) && f.off);
    // Quiet checks in a row turn it back on.
    assert(!noteBaFilterTraffic(f,7*s,n) && !noteBaFilterTraffic(f,8*s,n) && f.off);
    assert(noteBaFilterTraffic(f,9*s,n) && !f.off);
    // It works again after that, and the frame count can wrap.
    f=BaFilterState{};n=0xfffffff0U;assert(!noteBaFilterTraffic(f,10*s,n));
    n+=200;assert(noteBaFilterTraffic(f,11*s,n) && f.off);
}
static void testRxSequences() {
    static RxSequenceTracker t;
    // New, then the same number again, on one TID.
    assert(t.note(0,10,false,100) && !t.note(0,10,true,100));
    assert(t.counts.frames==2 && t.counts.fresh==1 && t.counts.repeats==1 && t.counts.retryBit==1 && !t.counts.retryBitFresh);
    assert(t.counts.allBytes==200 && t.counts.freshBytes==100);
    // Another TID keeps its own record. A retry can be the first copy we hear.
    assert(t.note(1,10,true,50) && t.counts.retryBitFresh==1);
    // A TID out of range doesn't write outside the table.
    assert(t.note(200,11,false,1));
    // Moving half the range on forgets the number, so wrapping round reads as new.
    assert(t.note(0,10+2048,false,1) && t.note(0,10,false,1));
    // Sequence numbers above the range wrap.
    assert(t.note(3,4095,false,1) && !t.note(3,uint16_t(4095+4096),false,1));
    // The toggle marks each aggregate.
    t.noteAmpdu(false);t.noteAmpdu(false);t.noteAmpdu(true);t.noteAmpdu(true);t.noteAmpdu(false);
    assert(t.counts.ampduFrames==5 && t.counts.aggregates==3);
}
static void testBaHandover() {
    // The holder is quiet only after half a second without a data frame either way.
    assert(baHolderIdle(0, 5));
    assert(!baHolderIdle(1000000, 1000000 + kBaHandoverIdleUS - 1));
    assert(baHolderIdle(1000000, 1000000 + kBaHandoverIdleUS));
    assert(!baHolderIdle(2000000, 1000000));  // A clock that went back is not idleness.

    BaHandover h{};
    assert(!h.active(1) && !h.overdue(1));
    AddbaRequest r{};
    memcpy(r.peer, peer, 6); r.immediate = true; r.tid = 0; r.token = 3;
    h.wantRequest(r, 1000);
    assert(h.active(1000) && h.hasRequest && !memcmp(h.peer, peer, 6) && h.request.token == 3);
    // Repeated requests keep it wanted, and the wait counts from the first one.
    r.token = 4;
    h.wantRequest(r, 1000 + kBaHandoverWaitUS - 10);
    assert(h.request.token == 4 && h.overdue(1000 + kBaHandoverWaitUS) && h.active(1000 + kBaHandoverWaitUS));
    // Another peer replaces it and starts its own wait, without the first peer's request.
    h.want(own, 5000000);
    assert(!memcmp(h.peer, own, 6) && !h.hasRequest && !h.overdue(5000000 + kBaHandoverWaitUS - 1));
    // A delayed (non-immediate) request is not kept.
    AddbaRequest delayed = r; memcpy(delayed.peer, own, 6); delayed.immediate = false;
    h.wantRequest(delayed, 5000001);
    assert(!h.hasRequest);
    assert(!h.active(5000001 + kBaHandoverWaitUS));
    h.clear();
    assert(!h.active(5000002));

    // Payload runs: short frames elsewhere don't count, a gap or another destination restarts the run.
    PayloadTracker t{};
    for (unsigned i = 0; i < kPayloadSendFrames - 1; ++i)
        t.note(peer, 1000 + i * 1000);
    assert(!t.sending(20000) && t.recentTo(peer, 20000) && !t.recentTo(own, 20000));
    t.note(peer, 20000);
    assert(t.sending(20000) && !t.sending(20000 + kPayloadGapUS + 1));
    t.note(own, 21000);
    assert(!t.sending(21000) && t.frames == 1 && t.recentTo(own, 21000));
    t.note(own, 21000 + kPayloadGapUS + 1);
    assert(t.frames == 1);
    assert(t.recentTo(own, 21000 + kPayloadGapUS + 1 + 1500000, 2000000));
}

int main() {
    testBlockAck();
    testRxSequences();
    testNoFcsOuterHeader();
    testBlockAckSend();
    testPendingAddbaRequests();
    testBaHandover();
    testBaFilter();
    testServiceDigest();
    testServiceMerge();
    testBaTxBulk();
    uint8_t f[120];const uint8_t broadcast[6]={255,255,255,255,255,255};
    ObservedCounts c={};
    action(f,broadcast,peer);
    assert(!observeAction(c,f,120,own) && !c.actionMentions && !c.actionToUs);
    // The address in a frame header is not evidence; only the body counts.
    action(f,own,peer);
    assert(!observeAction(c,f,120,own) && c.actionToUs==1 && !c.actionMentions);
    // First byte of the body, last possible position, and one byte too far.
    action(f,broadcast,peer);memcpy(f+24,own,6);
    assert(observeAction(c,f,120,own) && c.actionMentions==1);
    action(f,broadcast,peer);memcpy(f+114,own,6);
    assert(observeAction(c,f,120,own) && c.actionMentions==2);
    assert(!observeAction(c,f,119,own) && c.actionMentions==2);
    // A partial match must not count.
    action(f,broadcast,peer);memcpy(f+40,own,5);
    assert(!observeAction(c,f,120,own) && c.actionMentions==2);
    // Our own echo is never a peer reaction, even when it names us.
    action(f,broadcast,own);memcpy(f+40,own,6);
    assert(!observeAction(c,f,120,own) && c.selfEchoes==1 && c.actionMentions==2);
    // Unknown own address and short or null input count nothing.
    const auto before=c;
    assert(!observeAction(c,f,120,zero) && !observeAction(c,f,23,own));
    assert(!observeAction(c,nullptr,120,own) && !observeAction(c,f,120,nullptr));
    assert(!memcmp(&before,&c,sizeof(c)));

    ObservedCounts d={};
    uint8_t ip[48]={0x60};ip[6]=17;ip[42]=5353>>8;ip[43]=5353&255;
    observeData(d,data(ip,48,peer,group),own);
    assert(d.dataMulticast==1 && d.dataMDNS==1);
    observeData(d,data(ip,48,peer,own),own);
    assert(d.dataToUs==1 && d.dataMDNS==2);
    observeData(d,data(ip,47,peer,broadcast+0),own);   // too short for a UDP port
    assert(d.dataMulticast==2 && d.dataMDNS==2);
    ip[6]=6;observeData(d,data(ip,48,peer,peer),own);  // TCP, not mDNS
    assert(d.dataToOther==1 && d.dataMDNS==2);ip[6]=17;
    ip[43]=0;observeData(d,data(ip,48,peer,group),own);// other UDP port
    assert(d.dataMDNS==2);ip[43]=5353&255;
    observeData(d,data(ip,48,own,group),own);
    assert(d.selfEchoes==1 && d.dataMulticast==3);
    observeData(d,data(ip,48,peer,own),zero);          // own address not known yet
    assert(d.dataToOther==2 && d.dataToUs==1);

    // Peer-gated train: bounded, silent without a peer, no catch-up after failure.
    assert(probeLimit(false,true)==1536 && probeLimit(true,false)==64 && probeLimit(false)==8);
    assert(peerRecentlyHeard(true,8000000,8000000) && peerRecentlyHeard(true,16000000,8000000));
    assert(!peerRecentlyHeard(true,16000001,8000000) && !peerRecentlyHeard(false,1,0));
    assert(!peerRecentlyHeard(true,1,2));              // clock order is never trusted
    assert(peerTrainDelayUS(0,true,false,false)==6000000);
    assert(peerTrainDelayUS(1,true,false,false)==112640);
    assert(peerTrainDelayUS(1,true,true,true)==112640);
    assert(peerTrainDelayUS(1,false,false,false)==1000000);
    assert(peerTrainDelayUS(1,false,true,true)==112640);
    assert(peerTrainDelayUS(1,true,false,true)==6000000);
    assert(peerTrainDelayUS(1536,true,false,false)==6000000);
    // Unicast ACK probe: 1 in 16 frames each, control always, peer only if known.
    assert(ackProbeFor(0,true)==AckProbe::None && ackProbeFor(2,true)==AckProbe::None);
    assert(ackProbeFor(1,true)==AckProbe::PeerAWDL && ackProbeFor(17,true)==AckProbe::PeerAWDL);
    assert(ackProbeFor(5,true)==AckProbe::PeerHardware && ackProbeFor(9,true)==AckProbe::ControlAWDL);
    assert(ackProbeFor(13,true)==AckProbe::ControlHardware);
    assert(ackProbeFor(1,false)==AckProbe::None && ackProbeFor(9,false)==AckProbe::ControlAWDL);
    unsigned probes=0;for(unsigned i=0;i<1536;++i) probes+=ackProbeFor(i,true)!=AckProbe::None;
    assert(probes==384);
    assert(ackProbeIsPeer(AckProbe::PeerHardware) && !ackProbeIsPeer(AckProbe::ControlAWDL));
    assert(ackProbeUsesHardwareSource(AckProbe::ControlHardware) && !ackProbeUsesHardwareSource(AckProbe::PeerAWDL));
    AckCounts a={};
    recordAck(a,0,1,2);recordAck(a,3,0x83,16);recordAck(a,-1,1,5);recordAck(a,4,1,5);
    assert(a.acked[0]==1 && a.retries[0]==2 && a.failed[3]==1 && a.lastFailure[3]==0x83 && a.retries[3]==16);
    assert(!a.failed[0] && !a.acked[3]);
    // Raw ACKs and retried unicasts, by address.
    const uint8_t hw[6]={2,0xaa,0xbb,0xcc,0xdd,0xee};
    AirCounts air={};
    uint8_t ack[14]={0xd4};memcpy(ack+4,own,6);
    observeAir(air,ack,14,own,hw);assert(air.ackToAWDL==1);
    memcpy(ack+4,hw,6);observeAir(air,ack,14,own,hw);assert(air.ackToHardware==1);
    memcpy(ack+4,peer,6);observeAir(air,ack,14,own,hw);assert(air.ackToAWDL==1 && air.ackToHardware==1);
    observeAir(air,ack,9,own,hw);observeAir(air,nullptr,14,own,hw);observeAir(air,ack,14,zero,hw);
    assert(air.ackToAWDL==1 && air.ackToHardware==1);
    action(f,own,peer);observeAir(air,f,120,own,hw);assert(air.unicastToUs==1 && !air.unicastToUsRetried);
    f[1]=8;observeAir(air,f,120,own,hw);assert(air.unicastToUs==2 && air.unicastToUsRetried==1);
    action(f,peer,own);f[1]=8;observeAir(air,f,120,own,hw);assert(air.unicastToUs==2);
    action(f,own,peer);f[0]=0xb4;observeAir(air,f,120,own,hw);assert(air.unicastToUs==2); // other control frame
    action(f,own,peer);observeAir(air,f,23,own,hw);assert(air.unicastToUs==2);
    // Per-peer table: separate rows, latest RSSI, full table drops new peers safely.
    PeerProbe table[2];unsigned used=0;
    const uint8_t third[6]={2,7,7,7,7,7};
    recordPeerProbe(table,2,used,peer,true,60);recordPeerProbe(table,2,used,peer,false,61);
    assert(used==1 && table[0].acked==1 && table[0].tries==2 && table[0].rssi==61);
    recordPeerProbe(table,2,used,own,false,20);recordPeerProbe(table,2,used,third,true,30);
    assert(used==2 && !table[1].acked && table[1].tries==1);
    // Elicit mode swaps only the hardware-source slots.
    assert(ackProbeFor(1,true,true)==AckProbe::PeerAWDL && ackProbeFor(9,true,true)==AckProbe::ControlAWDL);
    assert(ackProbeFor(5,true,true)==AckProbe::PeerNeighbor && ackProbeFor(13,true,true)==AckProbe::PeerEcho);
    assert(ackProbeFor(5,false,true)==AckProbe::None && ackProbeFor(13,false,true)==AckProbe::None);
    assert(ackProbeIsPeer(AckProbe::PeerEcho) && ackProbeIsPeer(AckProbe::PeerNeighbor));
    assert(!ackProbeUsesHardwareSource(AckProbe::PeerNeighbor) && !ackProbeUsesHardwareSource(AckProbe::PeerEcho));
    AckCounts e={};recordAck(e,5,1,0);recordAck(e,4,0x83,0);recordAck(e,6,1,0);
    assert(e.acked[5]==1 && e.failed[4]==1);

    // Reply classification.
    ObservedCounts rc={};uint8_t reply[48]={0x60};reply[6]=58;
    reply[40]=129;observeData(rc,data(reply,48,peer,own),own);assert(rc.dataEchoReply==1 && rc.dataToUs==1);
    reply[40]=136;observeData(rc,data(reply,48,peer,own),own);assert(rc.dataNeighbor==1);
    reply[40]=135;observeData(rc,data(reply,48,peer,group),own);assert(rc.dataSolicit==1 && rc.dataMulticast==1);
    reply[40]=128;observeData(rc,data(reply,48,peer,own),own);assert(rc.dataEchoReply==1 && rc.dataNeighbor==1);
    reply[40]=129;observeData(rc,data(reply,43,peer,own),own);assert(rc.dataEchoReply==1);

    // Link-local derivation (EUI-64 with the universal/local bit flipped), on a made-up address.
    const uint8_t mac[6]={0x06,0x11,0x22,0x33,0x44,0x55};
    const uint8_t expectIP[16]={0xfe,0x80,0,0,0,0,0,0,0x04,0x11,0x22,0xff,0xfe,0x33,0x44,0x55};
    uint8_t ll[16];linkLocalFromAddress(mac,ll);assert(!memcmp(ll,expectIP,16));

    // A valid packet's checksum verifies to zero over the whole packet. The neighbor
    // solicitation value below was computed separately, in Python, for these addresses.
    uint8_t peerIP[16];linkLocalFromAddress(peer,peerIP);
    for (auto request : {PeerRequest::Neighbor,PeerRequest::Echo}) {
        uint8_t out[200];size_t n=0;
        assert(buildPeerRequest(request,own,peer,peerIP,0x0102,out,sizeof(out),&n)==BuildResult::Ready);
        const size_t icmp=peerRequestICMPBytes(request);
        assert(n==24+16+40+icmp);
        DataFrameView v;
        assert(parseDataFrame(out,n,false,&v)==ParseResult::Valid);
        assert(!memcmp(v.source,own,6) && !memcmp(v.destination,peer,6) && v.ip.sequence==0x0102 && v.wifiSequence==0x102);
        assert(out[0]==0x08 && !(out[1]&0xc7));
        const uint8_t *ip=v.ip.bytes;
        assert(ip[6]==58 && ip[7]==255 && !memcmp(ip+24,peerIP,16));
        assert(icmpv6Checksum(ip,40+icmp)==0);
        if(request==PeerRequest::Neighbor) assert(((ip[42]<<8)|ip[43])==0x9783);
        uint8_t copy[80];memcpy(copy,ip,40+icmp);
        const uint16_t stored=uint16_t((copy[42]<<8)|copy[43]);
        copy[42]=copy[43]=0;
        assert(icmpv6Checksum(copy,40+icmp)==stored);
        copy[50]^=1;assert(icmpv6Checksum(copy,40+icmp)!=stored);
        if(request==PeerRequest::Neighbor) {
            assert(ip[40]==135 && ip[41]==0 && !memcmp(ip+48,peerIP,16));
            assert(ip[64]==1 && ip[65]==1 && !memcmp(ip+66,own,6));
        } else assert(ip[40]==128 && ip[46]==1 && ip[47]==2);
        // Every short buffer is refused, without writing.
        for(size_t cap=0;cap<n;++cap) {
            uint8_t guard[200];memset(guard,0x5a,sizeof(guard));size_t w=99;
            assert(buildPeerRequest(request,own,peer,peerIP,1,guard,cap,&w)!=BuildResult::Ready && w==99);
            if(cap<24+16+40+icmp) for(auto b:guard) assert(b==0x5a);
        }
    }
    uint8_t scratch[200];size_t none=0;
    assert(buildPeerRequest(PeerRequest::Echo,zero,peer,peerIP,1,scratch,200,&none)==BuildResult::Invalid);
    assert(buildPeerRequest(PeerRequest::Echo,own,group,peerIP,1,scratch,200,&none)==BuildResult::Invalid);

    // Peer TLVs: exact layout, ordering and refusal without writes.
    uint8_t tl[400]={};size_t tlen=0;
    const uint8_t first[4]={6,1,0,0x11};memcpy(tl,first,4);tlen=4;
    tl[tlen]=24;tl[tlen+1]=40;tl[tlen+2]=0;
    tl[tlen+3+24]=65;tlen+=43;
    uint8_t tlvsBefore[400];memcpy(tlvsBefore,tl,400);const size_t oldLen=tlen;
    assert(extendWithPeerTLVs(tl,&tlen,sizeof(tl),own) && tlen==oldLen+80);
    assert(tl[0]==5 && tl[1]==21 && !memcmp(tl+8,own,6) && tl[14]==65 && tl[18]==65);
    const uint8_t *cs=tl+24;
    assert(cs[0]==18 && cs[1]==41 && cs[3]==15 && cs[4]==3 && cs[6]==3 && cs[7]==0xff);
    for(unsigned i=0;i<16;++i) assert(cs[9+2*i]==6 && cs[10+2*i]==0x51);
    {
        uint8_t alt[400];memcpy(alt,tlvsBefore,400);size_t altLen=oldLen;
        assert(extendWithPeerTLVs(alt,&altLen,sizeof(alt),own,149));
        for(unsigned i=0;i<16;++i) assert(alt[24+9+2*i]==149 && alt[24+10+2*i]==124);
        uint8_t low[400];memcpy(low,tlvsBefore,400);size_t lowLen=oldLen;
        assert(extendWithPeerTLVs(low,&lowLen,sizeof(low),own,36) && lowLen==oldLen+80);
        for(unsigned i=0;i<16;++i) assert(low[24+9+2*i]==36 && low[24+10+2*i]==115);
        // The network's radar channel, met there while associated: its own 20 MHz class, or 80 MHz.
        uint8_t radar[400];memcpy(radar,tlvsBefore,400);size_t radarLen=oldLen;
        assert(extendWithPeerTLVs(radar,&radarLen,sizeof(radar),own,112) && radarLen==oldLen+80);
        for(unsigned i=0;i<16;++i) assert(radar[24+9+2*i]==112 && radar[24+10+2*i]==121);
        uint8_t radar80[400];memcpy(radar80,tlvsBefore,400);size_t radar80Len=oldLen;
        assert(extendWithPeerTLVs(radar80,&radar80Len,sizeof(radar80),own,52,128) && radar80Len==oldLen+80);
        for(unsigned i=0;i<16;++i) assert(radar80[24+9+2*i]==52 && radar80[24+10+2*i]==128);
        // A number that isn't a channel is refused, and an explicit class must fit the channel.
        memcpy(radar,tlvsBefore,400);radarLen=oldLen;
        assert(!extendWithPeerTLVs(radar,&radarLen,sizeof(radar),own,68) && radarLen==oldLen && !memcmp(radar,tlvsBefore,400));
        assert(!extendWithPeerTLVs(radar,&radarLen,sizeof(radar),own,6,128) && radarLen==oldLen);
        assert(!extendWithPeerTLVs(radar,&radarLen,sizeof(radar),own,36,124) && radarLen==oldLen);
        uint8_t wide[400];memcpy(wide,tlvsBefore,400);size_t wideLen=oldLen;
        assert(extendWithPeerTLVs(wide,&wideLen,sizeof(wide),own,149,128) && wideLen==oldLen+80);
        for(unsigned i=0;i<16;++i) assert(wide[24+9+2*i]==149 && wide[24+10+2*i]==128);
        assert(!memcmp(wide,alt,24+9) && !memcmp(wide+24+41+3,alt+24+41+3,wideLen-24-44));
    }
    assert(!memcmp(tl+68,tlvsBefore,oldLen));
    const uint8_t *ht=tl+68+oldLen;assert(ht[0]==7 && ht[1]==9 && ht[5]==0x6f && ht[7]==0x1b && ht[8]==0xff);
    size_t walk=0;unsigned tlvCount=0;
    while(walk<tlen) { walk+=3+size_t(tl[walk+1])+(size_t(tl[walk+2])<<8);++tlvCount; }
    assert(walk==tlen && tlvCount==5);
    {
        // macOS's own TLV 7 (with -itlawdlcaps) is kept byte for byte and ours isn't added: exact capacity
        // works, one byte short is refused with nothing written, a truncated TLV 7 is refused, and a duplicate
        // native TLV 7 is kept as it is (the builder doesn't judge macOS's rates).
        const uint8_t rates[12]={7,9,0,0,0,0x6f,0,0x1b,0xff,0xff,0,0};
        uint8_t nat[400]={};memcpy(nat,tlvsBefore,oldLen);memcpy(nat+oldLen,rates,12);size_t natLen=oldLen+12;
        uint8_t natBefore[400];memcpy(natBefore,nat,400);
        size_t shortLen=natLen;
        assert(!extendWithPeerTLVs(nat,&shortLen,natLen-1,own) && shortLen==natLen && !memcmp(nat,natBefore,400));
        assert(!extendWithPeerTLVs(nat,&shortLen,natLen+68-1,own) && shortLen==natLen && !memcmp(nat,natBefore,400));
        assert(extendWithPeerTLVs(nat,&natLen,oldLen+12+68,own) && natLen==oldLen+12+68);
        assert(nat[0]==5 && nat[24]==18 && !memcmp(nat+68,natBefore,oldLen+12));
        uint8_t dup[400]={};memcpy(dup,tlvsBefore,oldLen);memcpy(dup+oldLen,rates,12);memcpy(dup+oldLen+12,rates,12);
        size_t dupLen=oldLen+24;
        assert(extendWithPeerTLVs(dup,&dupLen,sizeof(dup),own) && dupLen==oldLen+24+68);
        uint8_t bad[400]={};memcpy(bad,tlvsBefore,oldLen);bad[oldLen]=7;bad[oldLen+1]=20;size_t badLen=oldLen+5;
        uint8_t badBefore[400];memcpy(badBefore,bad,400);
        assert(!extendWithPeerTLVs(bad,&badLen,sizeof(bad),own) && badLen==oldLen+5 && !memcmp(bad,badBefore,400));
    }
    // Refused: already extended, no election TLV, truncated TLV, too little room.
    size_t again=tlen;assert(!extendWithPeerTLVs(tl,&again,sizeof(tl),own) && again==tlen);
    uint8_t bare[64]={};memcpy(bare,first,4);size_t bareLen=4;
    assert(!extendWithPeerTLVs(bare,&bareLen,64,own) && bareLen==4);
    uint8_t cut[64]={24,40,0};size_t cutLen=10;assert(!extendWithPeerTLVs(cut,&cutLen,64,own));
    uint8_t room[120];memcpy(room,tlvsBefore,oldLen);size_t roomLen=oldLen;
    assert(!extendWithPeerTLVs(room,&roomLen,oldLen+79,own) && !memcmp(room,tlvsBefore,oldLen));
    // Foreign control and frame-type counts.
    assert(ackProbeFor(1,true,true,true)==AckProbe::PeerForeign && ackProbeFor(1,false,true,true)==AckProbe::None);
    assert(ackProbeFor(5,true,true,true)==AckProbe::PeerNeighbor && ackProbeFor(1,true,true,false)==AckProbe::PeerAWDL);
    assert(ackProbeIsPeer(AckProbe::PeerForeign) && !ackProbeUsesHardwareSource(AckProbe::PeerForeign));
    AckCounts fc={};recordAck(fc,6,0x83,0);recordAck(fc,7,1,0);assert(fc.failed[6]==1 && !fc.acked[6]);
    AirCounts fa={};
    action(f,foreignAddress(),peer);observeAir(fa,f,120,own,hw);assert(fa.unicastToForeign==1 && !fa.unicastToForeignRetried && !fa.unicastToUs);
    f[1]=8;observeAir(fa,f,120,own,hw);assert(fa.unicastToForeign==2 && fa.unicastToForeignRetried==1);
    uint8_t fk[14]={0xd4};memcpy(fk+4,foreignAddress(),6);observeAir(fa,fk,14,own,hw);assert(fa.ackToForeign==1);
    action(f,own,peer);f[0]=0xc8;observeAir(fa,f,120,own,hw);assert(fa.typesToUs[0xc8>>2]==1 && fa.unicastToUs==1);
    f[0]=0xd0;observeAir(fa,f,120,own,hw);assert(fa.typesToUs[0xd0>>2]==1);
    f[0]=0x08;observeAir(fa,f,120,own,hw);assert(fa.typesToUs[2]==1 && fa.unicastToUs==3);
    // Ethernet to AWDL data frame: exact framing, round trip, and refusal without writes.
    uint8_t eth[14+48]={};memcpy(eth,group,6);memcpy(eth+6,own,6);eth[12]=0x86;eth[13]=0xdd;
    eth[14]=0x60;eth[19]=8;eth[20]=58;eth[21]=255;for(unsigned i=0;i<8;++i) eth[54+i]=uint8_t(0xc0+i);
    uint8_t airFrame[200];size_t airLength=0;
    assert(buildDataFrameFromEthernet(eth,sizeof(eth),own,0x0abc,airFrame,sizeof(airFrame),&airLength)==BuildResult::Ready);
    assert(airLength==24+16+48 && airFrame[0]==0x08 && !airFrame[1]);
    DataFrameView parsed;
    assert(parseDataFrame(airFrame,airLength,false,&parsed)==ParseResult::Valid);
    assert(!memcmp(parsed.destination,group,6) && !memcmp(parsed.source,own,6) && parsed.ip.length==48);
    assert(parsed.ip.sequence==0x0abc && parsed.wifiSequence==0xabc && !memcmp(parsed.ip.bytes,eth+14,48));
    uint8_t rebuilt[80];size_t rebuiltLength=0;
    assert(copyDataEthernet(parsed,rebuilt,sizeof(rebuilt),&rebuiltLength)==BuildResult::Ready);
    assert(rebuiltLength==sizeof(eth) && !memcmp(rebuilt,eth,sizeof(eth)));
    // Unicast keeps its destination; sequence wraps safely.
    memcpy(eth,peer,6);
    assert(buildDataFrameFromEthernet(eth,sizeof(eth),own,0xffff,airFrame,sizeof(airFrame),&airLength)==BuildResult::Ready);
    assert(!memcmp(airFrame+4,peer,6));
    memcpy(eth,group,6);
    auto refused=[&](const uint8_t *in,size_t n,const uint8_t *who,size_t cap,BuildResult want){
        uint8_t guard[200];memset(guard,0x5a,sizeof(guard));size_t w=99;
        assert(buildDataFrameFromEthernet(in,n,who,1,guard,cap,&w)==want && w==99);
        if(want!=BuildResult::NoSpace) for(auto b:guard) assert(b==0x5a);
    };
    refused(nullptr,62,own,200,BuildResult::Invalid);
    refused(eth,53,own,200,BuildResult::Invalid);          // too short for IPv6
    refused(eth,61,own,200,BuildResult::Invalid);          // declared IPv6 length disagrees
    refused(eth,62,peer,200,BuildResult::Invalid);         // not our source
    refused(eth,62,own,87,BuildResult::NoSpace);
    uint8_t arp[62];memcpy(arp,eth,62);arp[12]=8;arp[13]=6;refused(arp,62,own,200,BuildResult::Invalid);
    uint8_t noDest[62];memcpy(noDest,eth,62);memset(noDest,0,6);refused(noDest,62,own,200,BuildResult::Invalid);
    uint8_t notSix[62];memcpy(notSix,eth,62);notSix[14]=0x40;refused(notSix,62,own,200,BuildResult::Invalid);
    // Master indication TLVs: services kept byte for byte, ours replace the rest.
    {
        uint8_t mi[200]={};size_t n=0;
        auto add=[&](uint8_t type,size_t len,uint8_t fill){mi[n]=type;mi[n+1]=uint8_t(len);mi[n+2]=0;memset(mi+n+3,fill,len);n+=3+len;};
        add(24,40,0);mi[3+24]=65;
        add(4,5,0x44);add(18,6,0x18);add(5,4,0x55);add(7,3,0x77);
        const size_t kept=n;add(16,10,0xa1);add(6,9,0x66);add(21,2,0x21);
        uint8_t out[400];size_t len=0;
        assert(buildMasterIndicationTLVs(mi,n,own,out,sizeof(out),&len));
        unsigned count[256]={};size_t offset=0;
        while(offset<len){const size_t c=out[offset+1]|(size_t(out[offset+2])<<8);++count[out[offset]];offset+=3+c;}
        assert(offset==len);
        assert(!count[4] && count[18]==1 && count[5]==1 && count[7]==1 && count[24]==1);
        assert(count[16]==1 && count[6]==1 && count[21]==1);
        assert(len==(43+13+12+5)+peerTLVPrefixBytes+peerTLVSuffixBytes);
        const uint8_t *cs=out+24;assert(cs[0]==18 && cs[9]==6 && cs[10]==0x51);   // ours, channel 6
        assert(out[0]==5 && out[14]==65);                                         // metric from TLV 24
        const uint8_t *body=out+peerTLVPrefixBytes;                               // services unchanged
        assert(body[0]==24 && body[43]==16 && body[46]==0xa1 && body[56]==6 && body[59]==0x66);
        (void)kept;
        // Kept only when well formed: a complete TLV run with election v2.
        assert(masterIndicationWellFormed(mi,n));
        assert(!masterIndicationWellFormed(mi,n-1) && !masterIndicationWellFormed(mi,n+1));
        assert(!masterIndicationWellFormed(mi,2) && !masterIndicationWellFormed(nullptr,n) && !masterIndicationWellFormed(mi,0));
        {uint8_t noV2[20]={16,1,0,1,4,2,0,9,9};assert(!masterIndicationWellFormed(noV2,9));}
        {uint8_t shortV2[40]={24,27,0};assert(!masterIndicationWellFormed(shortV2,30));}
        {uint8_t junk[64];for(size_t i=0;i<sizeof(junk);++i)junk[i]=uint8_t(0xa5^i);assert(!masterIndicationWellFormed(junk,sizeof(junk)));}
        // Refusals: no election v2, truncation, too small, null.
        uint8_t noElection[20]={16,1,0,1};size_t w=9;
        uint8_t scratch[400];
        assert(!buildMasterIndicationTLVs(noElection,4,own,scratch,sizeof(scratch),&w) && w==9);
        assert(!buildMasterIndicationTLVs(mi,n-1,own,scratch,sizeof(scratch),&w) && w==9);
        assert(!buildMasterIndicationTLVs(mi,n,own,scratch,len-1,&w) && w==9);
        assert(!buildMasterIndicationTLVs(nullptr,n,own,scratch,400,&w) && !buildMasterIndicationTLVs(mi,0,own,scratch,400,&w));
        // Sized for the real template: 1200 in, and our builder's 1828 limit still holds.
        assert(0x4b0+peerTLVPrefixBytes+peerTLVSuffixBytes<=1828);
        // The result is a valid frame body, with our sync TLV added.
        uint8_t sync[73]={};uint8_t frame[1024];size_t fl=0;
        LocalFrame lf={{2,0,0,0,0,1},{255,255,255,255,255,255},3,1,1,1,sync,73,out,len};
        assert(buildLocalFrame(lf,frame,sizeof(frame),&fl)==BuildResult::Ready);
        ActionFrame af;assert(parseAction(frame,fl,&af)==ParseResult::Valid && af.subtype==3);
    }
    // TLV counting: whole stream, and a truncated stream keeps what it counted.
    {
        uint32_t counts[256]={};uint8_t fr[60]={};
        fr[40]=6;fr[41]=2;fr[45]=16;fr[46]=1;fr[49]=2;fr[50]=1;
        countActionTLVs(counts,fr,52);                 // type 6, 16, then 2 is cut short
        assert(counts[6]==1 && counts[16]==1 && !counts[2]);
        fr[50]=0;countActionTLVs(counts,fr,52);        // 2 now fits with no value
        assert(counts[6]==2 && counts[16]==2 && counts[2]==1);
        uint32_t none[256]={};countActionTLVs(none,fr,39);countActionTLVs(nullptr,fr,60);countActionTLVs(none,nullptr,60);
        for(auto v:none) assert(!v);
    }
    {
        // Each reason a data frame to us can fail to become an IPv6 packet.
        uint8_t q[64]={};q[0]=0x88;const uint8_t awdlBssid[6]={0,0x25,0,0xff,0x94,0x73};memcpy(q+16,awdlBssid,6);
        DataSkipCounts skips{};
        assert(dataSkipReason(q,64,true,ParseResult::Valid)==DataSkip::Chained);
        assert(dataSkipReason(q,64,false,ParseResult::Valid)==DataSkip::None);
        assert(dataSkipReason(q,64,false,ParseResult::Malformed)==DataSkip::None);
        assert(dataSkipReason(q,64,false,ParseResult::Unsupported)==DataSkip::None);
        assert(dataSkipReason(q,64,false,ParseResult::NotAWDL)==DataSkip::Llc);
        assert(dataSkipReason(q,20,false,ParseResult::NotAWDL)==DataSkip::None);
        q[16]=9;assert(dataSkipReason(q,64,false,ParseResult::NotAWDL)==DataSkip::Bssid);q[16]=0;
        q[0]=0xc8;assert(dataSkipReason(q,64,false,ParseResult::NotAWDL)==DataSkip::FrameType);q[0]=0x88;
        noteDataSkip(skips,DataSkip::None,q,64);assert(!skips.llc&&!skips.skippedLengths[0]);
        noteDataSkip(skips,DataSkip::Llc,q,1500);assert(skips.llc==1&&skips.skippedLengths[11]==1&&!skips.retried);
        q[1]=8;noteDataSkip(skips,DataSkip::Chained,q,100000);assert(skips.chained==1&&skips.retried==1&&skips.skippedLengths[15]==1);
    }
    {
        RxRateEntry rates[2]={};uint32_t over=0;
        noteRxRate(rates,2,7,over);noteRxRate(rates,2,7,over);noteRxRate(rates,2,0,over);noteRxRate(rates,2,9,over);
        assert(rates[0].rate==7&&rates[0].count==2&&rates[1].rate==0&&rates[1].count==1&&over==1);
    }
    {
        uint8_t f[100] = {};
        const uint8_t tlv[] = {18, 38, 0, 15, 3, 0, 3, 255, 255, 36, 128, 36, 128, 149, 128, 0, 0, 0, 0, 0, 0, 0, 0, 36, 128, 6, 81, 36, 128, 149, 128, 36, 128, 0, 0, 0, 0, 0, 0, 44, 128};
        memcpy(f + 40, tlv, sizeof(tlv));
        PeerSlots p{};
        assert(peerSlotsFromAction(f, 40 + sizeof(tlv), &p));
        assert(p.channel6 == 1 && p.channel149 == 2 && p.channel36 == 5 && p.channel44 == 1 && !p.other && p.off == 7);
        assert(!peerSlotsFromAction(f, 40 + sizeof(tlv) - 1, &p));
        f[41] = 37; assert(!peerSlotsFromAction(f, 40 + sizeof(tlv), &p)); f[41] = 38;
        f[40] = 19; assert(!peerSlotsFromAction(f, 40 + sizeof(tlv), &p)); f[40] = 18;
        assert(!peerSlotsFromAction(nullptr, 80, &p) && !peerSlotsFromAction(f, 39, &p) && !peerSlotsFromAction(f, 80, nullptr));
        assert(flightBin(0) == 0 && flightBin(249) == 0 && flightBin(250) == 1 && flightBin(99999) == 8 && flightBin(100000) == 9);
        assert(airBin(99) == 0 && airBin(100) == 1 && airBin(6399) == 6 && airBin(6400) == 7);
        TxServiceStats t{};
        // First completion: no gap, since nothing was known to be waiting before it.
        t.note(1000, true, 150, 0, 0, 0, 0x4107);
        assert(t.frames == 1 && t.gapFrames == 0 && t.airBins[1] == 1 && t.airSumUS == 150);
        // Second, already waiting: a 900 us gap with 300 us on the air and one retry.
        t.note(1900, false, 300, 1, 0, 0, 0x4107);
        assert(t.gapFrames == 1 && t.gapSumUS == 900 && t.gapAirSumUS == 300 && t.gapBins[2] == 1);
        assert(t.failureFrames == 1 && t.framesWithRetries == 1 && t.rates[0].rate == 0x4107 && t.rates[0].count == 2);
        // Third, after the firmware had nothing waiting: not a service gap.
        t.note(50000, false, 120, 0, 2, 1, 0x4108);
        assert(t.gapFrames == 1 && t.frames == 3 && t.failureRts == 2 && t.btKills == 1 && t.rates[1].count == 1);
        // A clock that went backwards isn't counted.
        t.lastBusy = true; t.note(10, false, 100, 0, 0, 0, 0x4107);
        assert(t.gapFrames == 1);
        // Received frames are split by gap length.
        TxServiceStats r{};
        r.note(1000, true, 200, 0, 0, 0, 1, 10, 4);
        r.note(2500, true, 200, 0, 0, 0, 1, 16, 7);   // slow: 6 heard, 3 from the peer
        r.note(2700, true, 200, 0, 0, 0, 1, 17, 7);   // fast: 1 heard, none from the peer
        assert(r.slowGaps == 1 && r.slowRxAll == 6 && r.slowRxPeer == 3 && r.slowWithPeer == 1);
        assert(r.fastGaps == 1 && r.fastRxAll == 1 && r.fastRxPeer == 0 && r.fastWithPeer == 0);
        // The timeline starts at the first frame that was waiting, and stops when full.
        static TxTrace tr{};
        tr.notePeerFrame(5);  // Not recording yet.
        tr.noteCompletion(1000, 900, false, true, false, 150, 4);
        assert(tr.count == 0 && tr.peerCount == 0);
        tr.noteCompletion(2000, 1200, true, true, true, 70000, 3);
        assert(tr.count == 1 && tr.entries[0].doneUS == 2000 && tr.entries[0].flightUS == 800 &&
               tr.entries[0].airUS == 0xffff && tr.entries[0].flags == 7 && tr.entries[0].inFlight == 3);
        tr.notePeerFrame(2100);
        assert(tr.peerCount == 1 && tr.peerUS[0] == 2100);
        tr.noteCompletion(2200, 3000, false, false, false, 100, 1);  // Clock behind submit: flight 0.
        assert(tr.count == 2 && tr.entries[1].flightUS == 0 && tr.entries[1].flags == 0);
        tr.count = TxTrace::kEntries;
        tr.noteCompletion(9000, 8000, true, true, false, 100, 2); tr.notePeerFrame(9100);
        assert(tr.count == TxTrace::kEntries && tr.peerCount == 1);
        static BaTrace ba{};
        ba.note(5000, 1200, 0x4119, 300, 18, 2, 70, 300, 0x5);
        assert(ba.count == 1 && ba.entries[0].doneUS == 5000 && ba.entries[0].wirelessTime == 1200 &&
               ba.entries[0].sent == 300 && ba.entries[0].acked == 18 && ba.entries[0].retries == 2 &&
               ba.entries[0].queuedBefore == 70 && ba.entries[0].queuedAfter == 255 && ba.entries[0].flags == 5);
        ba.count = BaTrace::kEntries; ba.note(1, 0, 0, 0, 0, 0, 0, 0, 0);
        assert(ba.count == BaTrace::kEntries);
        assert(depthBin(0) == 0 && depthBin(1) == 1 && depthBin(2) == 2 && depthBin(3) == 2 && depthBin(4) == 3 && depthBin(128) == 8 && depthBin(100000) == 8);
    }
    puts("AWDL heard-us classification and peer-gated train policy: passed (offline only)");
}
