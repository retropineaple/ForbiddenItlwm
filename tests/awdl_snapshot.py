"""Check the actual snapshot method with missing/present native timing."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'ForbiddenItlwm/AirportItlwmV2.cpp').read_text()
start = source.index('IOReturn ItlAWDLInterface::copyFrameConfiguration(')
end=source.index('{',start)+1; depth=1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}'); end+=1
body=source[start:end]
program = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
using IOReturn = unsigned;
enum { kIOReturnSuccess=0, kIOReturnBadArgument=1, kIOReturnNotReady=2 };
struct apple80211_awdl_sync_params { uint32_t version, availability_window_length,
    availability_window_period, extension_length, synchronization_frame_period, non_election_master; };
static_assert(sizeof(apple80211_awdl_sync_params) == 24, "the native 24-byte record");
static unsigned depth;
static void IOLockLock(void*) { assert(!depth); ++depth; }
static void IOLockUnlock(void*) { assert(depth==1); --depth; }
class ItlAWDLInterface {
public:
    struct FrameConfiguration { apple80211_awdl_sync_params parameters;
        bool hasRequestedTiming; uint8_t address[6]; size_t templateLength; uint8_t staticTLVs[32]; };
    void *configurationLock = this; bool nativeStarted=true;
    uint8_t localFrameAddress[6] = {2,1,2,3,4,5};
    struct Requested {
        bool present=false;
        IOReturn get(apple80211_awdl_sync_params *p) {
            assert(depth==1); if (!present) return kIOReturnNotReady;
            *p={1,16,64,16,110,7}; return kIOReturnSuccess;
        }
    } requested_AWDL_SYNC_PARAMS;
    struct Template {
        size_t length=3;
        size_t size() { assert(depth==1); return length; }
        IOReturn copy(uint8_t *p,size_t capacity) {
            assert(depth==1 && capacity>=length); memset(p,0xab,length); return 0;
        }
    } frameTemplate;
    IOReturn copyFrameConfiguration(FrameConfiguration*);
};
'''
program += body
program += r'''
int main() {
    ItlAWDLInterface obj;
    ItlAWDLInterface::FrameConfiguration out;
    memset(&out,0xa5,sizeof(out));
    assert(obj.copyFrameConfiguration(&out)==0 && !depth);
    assert(!out.hasRequestedTiming && !out.parameters.availability_window_period);
    assert(!out.parameters.version && !out.parameters.synchronization_frame_period);
    assert(out.templateLength==3 && out.staticTLVs[2]==0xab && out.address[0]==2);
    obj.requested_AWDL_SYNC_PARAMS.present=true;
    assert(obj.copyFrameConfiguration(&out)==0 && !depth && out.hasRequestedTiming);
    assert(out.parameters.availability_window_period==64);
    assert(out.parameters.non_election_master==7);   // the trailing word survives the snapshot
    obj.frameTemplate.length=0;
    auto before=out;
    assert(obj.copyFrameConfiguration(&out)==kIOReturnNotReady && !depth);
    assert(!memcmp(&out,&before,sizeof(out)));
    obj.nativeStarted=false;
    assert(obj.copyFrameConfiguration(&out)==kIOReturnNotReady && !depth);
    assert(obj.copyFrameConfiguration(nullptr)==kIOReturnBadArgument);
    puts("native snapshot optional timing: passed (host mocks only)");
}
'''
with tempfile.TemporaryDirectory(prefix='awdl-snapshot-') as directory:
    src=Path(directory)/'test.cpp';src.write_text(program); exe=Path(directory)/'test'
    subprocess.run(['xcrun','clang++','-std=c++14','-fsanitize=address,undefined',str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
