// awdl0's MTU choice: Apple's 1500 unless the boot-arg gives a value in range.
#include <AWDLMtu.hpp>
#include <cassert>
#include <cstdio>
using namespace ItlAWDL;
int main()
{
    assert(awdlMtu(0) == 1500 && awdlMtu(-1) == 1500);          // no boot-arg: Apple's value
    assert(awdlMtu(1484) == 1484 && awdlMtu(1280) == 1280 && awdlMtu(1500) == 1500);
    assert(awdlMtu(1279) == 1500 && awdlMtu(1501) == 1500 && awdlMtu(9000) == 1500);   // out of range: ignored, not clamped
    assert(kAwdlMtuApple == 0x5dc && kAwdlMtuFamilyDefault == 0x5cc);                   // the two immediates in the asm
    puts("awdl0 MTU: passed");
    return 0;
}
