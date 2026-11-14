#include "../BTLEAdvFixup/BTLEAdvEventMask.hpp"
#include <cassert>
#include <initializer_list>
#include <cstdio>

int main() {
    using namespace BTLEAdv;
    for (const char *build : {"25G332", "25G342"}) {
        const auto *find = strcmp(build, "25G332") == 0 ? eventMaskFind2672 : eventMaskFind2673;
        const auto *replace = strcmp(build, "25G332") == 0 ? eventMaskReplace2672 : eventMaskReplace2673;
        uint8_t page[4096] = {};
        memcpy(page + 64, find, sizeof(eventMaskFind2672));
        assert(!applyEventMask(build, false, page, sizeof(page)));
        for (const char *unknown : {"25G241", "25G342-extra", "", static_cast<const char *>(nullptr)})
            assert(!applyEventMask(unknown, true, page, sizeof(page)));
#if defined(BTLEADV_SEQUOIA) || defined(BTLEADV_SONOMA)
        assert(!applyEventMask(build, true, page, sizeof(page)));
        assert(memcmp(page + 64, find, sizeof(eventMaskFind2672)) == 0);
#else
        assert(applyEventMask(build, true, page, sizeof(page)));
        assert(memcmp(page + 64, replace, sizeof(eventMaskFind2672)) == 0);
        assert(!applyEventMask(build, true, page, sizeof(page)));
        assert(page[63] == 0 && page[84] == 0);
#endif
    }
    puts("BTLEAdv event mask build/scanning gates: passed");
}
