// Read-only decoder for nonaggregated AWDL IPv6 data frames. No peer admission.
#pragma once
#include <AWDLDataPayload.hpp>
namespace ItlAWDL {
struct DataFrameView {
    IPv6PayloadView ip;
    const uint8_t *source;
    const uint8_t *destination;
    uint16_t wifiSequence;
};
// Caller-owned, disjoint output. This reconstructs Ethernet framing only; it
// does not authorize delivery or take ownership of the borrowed packet view.
inline BuildResult copyDataEthernet(const DataFrameView &p,uint8_t *out,size_t capacity,size_t *written) {
    if(!out || !written || !p.source || !p.destination || !validIPv6Payload(p.ip.bytes,p.ip.length))
        return BuildResult::Invalid;
    if(capacity<14+p.ip.length) return BuildResult::NoSpace;
    for(unsigned i=0;i<6;++i) { out[i]=p.destination[i];out[6+i]=p.source[i]; }
    out[12]=0x86;out[13]=0xdd;
    for(size_t i=0;i<p.ip.length;++i) out[14+i]=p.ip.bytes[i];
    *written=14+p.ip.length;return BuildResult::Ready;
}
// Input excludes radiotap. FCS presence is explicit; bytes are never modified.
// Protected, fragmented, four-address, HT-control and aggregated frames remain
// unsupported. A valid result is framing evidence, not an authorized peer.
inline ParseResult parseDataFrame(const uint8_t *f,size_t captured,bool includesFCS,DataFrameView *out) {
    if (!f || !out || captured < (includesFCS?28U:24U)) return ParseResult::Malformed;
    const size_t length=captured-(includesFCS?4:0);
    if(f[0]!=0x08 && f[0]!=0x88) return ParseResult::NotAWDL;
    const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};
    for(unsigned i=0;i<6;++i) if(f[16+i]!=bssid[i]) return ParseResult::NotAWDL;
    if ((f[1]&0xc7) || (f[22]&15)) return ParseResult::Unsupported;
    bool src=false,dst=false;
    for(unsigned i=0;i<6;++i) { src|=f[10+i]!=0;dst|=f[4+i]!=0; }
    if(!src || !dst || (f[10]&1)) return ParseResult::Malformed;
    const size_t header=f[0]==0x88?26:24;
    if(length<header) return ParseResult::Malformed;
    if(header==26 && (f[24]&0x80)) return ParseResult::Unsupported;
    IPv6PayloadView ip;
    const auto result=parseIPv6DataPayload(f+header,length-header,&ip);
    if(result!=ParseResult::Valid) return result;
    *out={ip,f+10,f+4,uint16_t(read16(f+22)>>4)};
    return ParseResult::Valid;
}
// Why parseDataFrame doesn't accept a frame, for counting what a listener hears but can't hand on. It follows
// parseDataFrame's checks in the same order, so None means the frame parses (tests/awdl_data_reject.cpp checks
// that against the parser). Diagnostic only: nothing decides anything from it.
enum class DataReject : uint8_t {
    None, Short, QosNull, NullData, OtherSubtype, NotAwdlBssid, Protected, HtControl, Fragment, DsBits,
    Addresses, Amsdu, OuterLength, NotLlcSnap, NotIPv6, BadIPv6, Count
};
inline DataReject classifyDataFrame(const uint8_t *f,size_t captured,bool includesFCS) {
    if (!f || captured < (includesFCS?28U:24U)) return DataReject::Short;
    const size_t length=captured-(includesFCS?4:0);
    if (f[0]!=0x08 && f[0]!=0x88)
        return f[0]==0xc8 ? DataReject::QosNull : f[0]==0x48 ? DataReject::NullData : DataReject::OtherSubtype;
    const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};
    for(unsigned i=0;i<6;++i) if(f[16+i]!=bssid[i]) return DataReject::NotAwdlBssid;
    if (f[1]&0x40) return DataReject::Protected;
    if (f[1]&0x80) return DataReject::HtControl;
    if ((f[1]&0x04) || (f[22]&15)) return DataReject::Fragment;
    if (f[1]&0x03) return DataReject::DsBits;
    bool src=false,dst=false;
    for(unsigned i=0;i<6;++i) { src|=f[10+i]!=0;dst|=f[4+i]!=0; }
    if(!src || !dst || (f[10]&1)) return DataReject::Addresses;
    const size_t header=f[0]==0x88?26:24;
    if(length<header) return DataReject::Short;
    if(header==26 && (f[24]&0x80)) return DataReject::Amsdu;
    const uint8_t *p=f+header; size_t n=length-header;
    if(n<16) return DataReject::Short;
    if (hasOuterDataHeader(p,n) && (p[6]==0x86 && p[7]==0xdd))
        return validIPv6Payload(p+8,n-8) ? DataReject::None : DataReject::BadIPv6;
    if (hasOuterDataHeader(p,n)) {
        if (((size_t(p[6])<<8)|p[7])!=n-8) return DataReject::OuterLength;
        p+=8;n-=8;
        if (n<16) return DataReject::Short;
    }
    const uint8_t llc[8]={0xaa,0xaa,3,0,0x17,0xf2,8,0};
    for(unsigned i=0;i<8;++i) if(p[i]!=llc[i]) return DataReject::NotLlcSnap;
    if (read16(p+8)!=0x0403 || read16(p+12)!=0 || p[14]!=0x86 || p[15]!=0xdd) return DataReject::NotIPv6;
    if (!validIPv6Payload(p+16,n-16)) return DataReject::BadIPv6;
    return DataReject::None;
}
}
