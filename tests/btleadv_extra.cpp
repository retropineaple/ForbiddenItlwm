#include "../BTLEAdvFixup/BTLEAdvExtraPatches.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>

int main() {
    using namespace BTLEAdv;
    for (unsigned index : {0U, 1U}) {
        const auto *set = extraPatchSet(index);
        assert(set);
        for (unsigned i = 0; i < 6; ++i) {
            unsigned char page[4096] = {};
            memcpy(page + 64, set[i].find, set[i].size);
            assert(applyExtraPatches(false, true, page, sizeof(page)) == 0);
            assert(memcmp(page + 64, set[i].find, set[i].size) == 0);
            unsigned char unknown[4096] = {};
            assert(applyExtraPatches(true, true, unknown, sizeof(unknown)) == 0);
        }
        unsigned char page[4096] = {};
        memcpy(page + 64, set[0].find, set[0].size);
        assert(applyExtraPatches(true, false, page, sizeof(page)) == 1);
        assert(applyExtraPatches(true, true, page, sizeof(page)) == 0);
        for (unsigned caller : {1U, 3U, 5U}) {
            const unsigned helper = caller == 1 ? 2 : 4;
            for (unsigned state = 0; state < 6; ++state) {
                unsigned char buffer[4096] = {};
                memcpy(buffer + 64, state & 1 ? set[caller].replace : set[caller].find, set[caller].size);
                if (state < 4)
                    memcpy(buffer + 512, state & 2 ? set[helper].replace : set[helper].find, set[helper].size);
                applyExtraPatches(true, true, buffer, sizeof(buffer));
                assert(memcmp(buffer + 64, state < 4 ? set[caller].replace : set[caller].find, set[caller].size) == 0);
            }
        }
    }
    assert(extraPatchSet(2) == nullptr);
    puts("BTLEAdv opt-in extras and helper dependencies: passed");
}
