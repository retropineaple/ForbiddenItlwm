#include <BTLEAdvHci.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace BTLEAdv;
static const uint8_t addr[6]={1,2,3,4,5,0xc6};
static void params(uint8_t *c,uint8_t handle,uint8_t own){memset(c,0,28);c[0]=0x36;c[1]=0x20;c[2]=25;c[3]=handle;c[13]=own;}
int main(){
    State s{};uint8_t c[28];
    // No address known yet: nothing to send.
    params(c,0,1);auto a=onCommand(s,c,28,true);assert(!a.sendSetAddress&&!s.addressedSets);
    // IntelBTPatcher's fixed address is never adopted.
    uint8_t fixed[9]={5,0x20,6,0x94,0x50,0x64,0xd0,0x78,0x6b};
    onCommand(s,fixed,9,true);assert(!s.haveAddress);
    // bluetoothd's address is, and the set gets it once, with exact bytes.
    uint8_t ra[9]={5,0x20,6};memcpy(ra+3,addr,6);onCommand(s,ra,9,true);assert(s.haveAddress);
    a=onCommand(s,c,28,true);assert(a.sendSetAddress&&a.outputLength==10);
    const uint8_t want[10]={0x35,0x20,7,0,1,2,3,4,5,0xc6};assert(!memcmp(a.output,want,10));
    a=onCommand(s,c,28,true);assert(!a.sendSetAddress);
    // Another handle gets its own.
    params(c,2,1);a=onCommand(s,c,28,true);assert(a.sendSetAddress&&a.output[3]==2);
    // Public own address: untouched.
    params(c,5,0);a=onCommand(s,c,28,true);assert(!a.sendSetAddress);
    // A failed attempt by bluetoothd before the set exists doesn't count as having one.
    uint8_t sa[10]={0x35,0x20,7,7};memcpy(sa+4,addr,6);onCommand(s,sa,10,true);assert(!(s.addressedSets&(1U<<7)));
    params(c,7,1);a=onCommand(s,c,28,true);assert(a.sendSetAddress&&a.output[3]==7);
    // A set's address never leaks to another set, which gets the general address.
    sa[9]=0xc7;onCommand(s,sa,10,true);params(c,8,1);a=onCommand(s,c,28,true);assert(a.output[9]==0xc6);
    // Removing a set allows it again; clear allows all; reset forgets everything.
    uint8_t rm[4]={0x3c,0x20,1,8};onCommand(s,rm,4,true);a=onCommand(s,c,28,true);assert(a.sendSetAddress);
    uint8_t cl[3]={0x3d,0x20,0};onCommand(s,cl,3,true);assert(!s.addressedSets);
    uint8_t rs[3]={3,0xc,0};onCommand(s,rs,3,true);assert(!s.haveAddress&&!s.addressedSets);
    // Out of range handles and short commands do nothing.
    onCommand(s,ra,9,true);params(c,40,1);a=onCommand(s,c,28,true);assert(!a.sendSetAddress);
    a=onCommand(s,c,5,true);assert(!a.sendSetAddress);a=onCommand(s,nullptr,0,true);assert(!a.sendSetAddress);
    // Event mask gets bit 12 once when extended scanning is in use.
    uint8_t em[11]={1,0x20,8,0x1f,0,0,0,0,0,0,0};
    a=onCommand(s,em,11,true);assert(a.sendEventMask&&a.outputLength==11&&a.output[4]==0x10&&a.output[3]==0x1f);
    a=onCommand(s,em,11,false);assert(!a.sendEventMask);
    em[4]=0x10;a=onCommand(s,em,11,true);assert(!a.sendEventMask);
    // The legacy scan workaround runs once per reset, and doesn't touch the advertising address.
    State l{};uint8_t sp[10]={0x0b,0x20,7};
    assert(wantsLegacyScanAddress(l,sp,10));assert(!wantsLegacyScanAddress(l,sp,10));assert(!l.haveAddress);
    assert(!wantsLegacyScanAddress(l,em,11));
    onCommand(l,rs,3,true);assert(wantsLegacyScanAddress(l,sp,10));
    // The startup order from a real trace: each set ends up with its own address.
    {
        State r{};uint8_t p[28];
        const uint8_t a0[6]={0x10,0,0,0,0,0x71},a2[6]={0x20,0,0,0,0,0x5a},a3[6]={0x30,0,0,0,0,0xc4};
        auto setAddr=[&](uint8_t h,const uint8_t *x){uint8_t m[10]={0x35,0x20,7,h};memcpy(m+4,x,6);return onCommand(r,m,10,true);};
        auto expect=[&](uint8_t h,const uint8_t *x){params(p,h,1);auto o=onCommand(r,p,28,true);
            assert(o.sendSetAddress&&o.output[3]==h&&!memcmp(o.output+4,x,6));};
        setAddr(0,a0);setAddr(2,a2);                 // Rejected by the controller: no sets yet.
        uint8_t g[9]={5,0x20,6};memcpy(g+3,a0,6);onCommand(r,g,9,true);
        expect(3,a0);                                // Set 3 has none of its own yet, so general.
        setAddr(3,a3);                               // bluetoothd then gives it its own.
        expect(0,a0);expect(2,a2);                   // Not set 3's address, the old bug.
        params(p,3,1);assert(!onCommand(r,p,28,true).sendSetAddress);
        // A set known only by its own address works before any general address arrives.
        State e{};uint8_t m[10]={0x35,0x20,7,4};memcpy(m+4,a2,6);onCommand(e,m,10,true);
        params(p,4,1);auto o=onCommand(e,p,28,true);assert(o.sendSetAddress&&!memcmp(o.output+4,a2,6));
        // Removing a set forgets its own address: a set made later with that handle gets the
        // general address, never the one from before the rotation.
        {State q{};uint8_t ra[9]={5,0x20,6};memcpy(ra+3,a0,6);onCommand(q,ra,9,true);
         uint8_t sa[10]={0x35,0x20,7,5};memcpy(sa+4,a3,6);onCommand(q,sa,10,true);assert(q.knownSetAddresses&(1U<<5));
         uint8_t rm[4]={0x3c,0x20,1,5};onCommand(q,rm,4,true);assert(!(q.knownSetAddresses&(1U<<5)));
         params(p,5,1);auto v=onCommand(q,p,28,true);assert(v.sendSetAddress&&!memcmp(v.output+4,a0,6));
         onCommand(q,sa,10,true);uint8_t clr[3]={0x3d,0x20,0};onCommand(q,clr,3,true);
         assert(!q.knownSetAddresses&&!q.addressedSets);
         // An out-of-range handle changes nothing.
         onCommand(q,sa,10,true);uint8_t bad[4]={0x3c,0x20,1,40};onCommand(q,bad,4,true);assert(q.knownSetAddresses&(1U<<5));}
        // Reset forgets per-set addresses too.
        uint8_t rst[3]={3,0xc,0};onCommand(r,rst,3,true);assert(!r.knownSetAddresses);
    }
    // LE Data Length Change (OpenIntelWireless#514): two extra copies of LE Read Remote
    // Features, and the two duplicate completions become PHY Update, then Data Length Change.
    {
        DataLengthState d{};uint8_t readFeatures[3]={0x16,0x20,0x02};
        assert(extraRemoteReads(d,readFeatures,3)==2&&d.outstanding==2&&d.stage==0);
        // Only that command, and only whole ones.
        assert(extraRemoteReads(d,em,11)==0&&d.outstanding==2);
        assert(extraRemoteReads(d,readFeatures,2)==0&&d.outstanding==2);
        assert(extraRemoteReads(d,nullptr,0)==0);
        // A copy that couldn't be sent produces no event, so it isn't waited for.
        forgetRemoteRead(d);assert(d.outstanding==1);
        // A controller start forgets a command still in flight.
        uint8_t rst[3]={0x03,0x0c,0x00};
        assert(extraRemoteReads(d,rst,3)==0&&!d.outstanding&&!d.stage);
        forgetRemoteRead(d);assert(!d.outstanding);

        // The real completion (status 0, handle 0x0053, all-zero features) passes through.
        const uint8_t real[14]={0x3e,12,0x04,0x00,0x53,0x00,0x01,0,0,0,0,0,0,0};
        uint8_t ev[14];memcpy(ev,real,14);
        assert(extraRemoteReads(d,readFeatures,3)==2);
        assert(rewriteEvent(d,ev,14)==14&&!memcmp(ev,real,14)&&d.stage==1&&d.outstanding==2);
        // Anything else is left alone while a duplicate is expected.
        const uint8_t other[8]={0x3e,6,0x0c,0x00,0x53,0x00,0x01,0x01};
        memcpy(ev,other,8);assert(rewriteEvent(d,ev,8)==8&&!memcmp(ev,other,8));
        const uint8_t disconn[7]={0x05,3,0x16,0x00,0x53,0x00,0x13};
        memcpy(ev,disconn,7);assert(rewriteEvent(d,ev,7)==7&&!memcmp(ev,disconn,7));
        // First duplicate: PHY Update Complete, both directions 2M, 8 bytes.
        memcpy(ev,real,14);
        assert(rewriteEvent(d,ev,14)==8&&d.stage==2&&d.outstanding==1);
        const uint8_t phy[8]={0x3e,6,0x0c,0x00,0x53,0x00,0x02,0x02};assert(!memcmp(ev,phy,8));
        // Second: LE Data Length Change, 27 octets and 328 us each way, 13 bytes.
        memcpy(ev,real,14);
        assert(rewriteEvent(d,ev,14)==13&&!d.stage&&!d.outstanding);
        const uint8_t len[13]={0x3e,11,0x07,0x53,0x00,0x1b,0x00,0x48,0x01,0x1b,0x00,0x48,0x01};
        assert(!memcmp(ev,len,13));
        // Nothing expected any more: the same completion is a plain pass-through.
        memcpy(ev,real,14);assert(rewriteEvent(d,ev,14)==14&&!memcmp(ev,real,14));
        // Short and null events are never touched.
        uint8_t tiny[5]={0x3e,3,0x04,0x00,0x53};
        extraRemoteReads(d,readFeatures,3);
        assert(rewriteEvent(d,tiny,5)==5&&rewriteEvent(d,nullptr,14)==14);
    }
    puts("BTLEAdvFixup HCI decisions: passed (host only)");
}
