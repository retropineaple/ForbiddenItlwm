// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "BTLEAdvExtraPatches26_7.hpp"
#include "BTLEAdvExtraPatches26_7_3.hpp"
#include <string.h>

namespace BTLEAdv {
struct ExtraPatch {
    const uint8_t *find;
    const uint8_t *replace;
    size_t size;
    bool scanningOnly;
};
template <size_t N, size_t M>
constexpr ExtraPatch extraMake(const uint8_t (&find)[N], const uint8_t (&replace)[M], bool scanning = false) {
    static_assert(N == M, "patch size");
    return {find, replace, N, scanning};
}
#define EXTRA_SET(ns) \
    extraMake(ns::addressResendFind, ns::addressResendReplace), \
    extraMake(ns::rpaHookFind, ns::rpaHookReplace), \
    extraMake(ns::rpaSaveFind, ns::rpaSaveReplace), \
    extraMake(ns::falAddFind, ns::falAddReplace), \
    extraMake(ns::falHelperFind, ns::falHelperReplace), \
    extraMake(ns::falRemoveFind, ns::falRemoveReplace)

inline const ExtraPatch *extraPatchSet(const char *build) {
    static constexpr ExtraPatch p2672[] = {EXTRA_SET(BTLEAdvExtra2672)};
    static constexpr ExtraPatch p2673[] = {EXTRA_SET(BTLEAdvExtra2673)};
    if (build && strcmp(build, "25G332") == 0)
        return p2672;
    if (build && strcmp(build, "25G342") == 0)
        return p2673;
    return nullptr;
}
#undef EXTRA_SET

inline bool extraPatchBytes(uint8_t *page, size_t size, const ExtraPatch &patch, bool restore) {
    const auto *needle = restore ? patch.replace : patch.find;
    for (size_t i = 0; i + patch.size <= size; ++i)
        if (memcmp(page + i, needle, patch.size) == 0) {
            memcpy(page + i, restore ? patch.find : patch.replace, patch.size);
            return true;
        }
    return false;
}
inline bool extraContainsHelper(const uint8_t *page, size_t size, const ExtraPatch &helper) {
    for (size_t i = 0; i + helper.size <= size; ++i)
        if (memcmp(page + i, helper.replace, helper.size) == 0)
            return true;
    return false;
}
inline uint32_t applyExtraPatches(const char *build, bool enabled, bool scanning, uint8_t *page, size_t size) {
    if (!enabled)
        return 0;
    const auto *patches = extraPatchSet(build);
    if (!patches)
        return 0;
    // Helpers precede callers. Cached callers are restored if the corresponding
    // helper cannot be established in the same page on this validation pass.
    static constexpr unsigned order[] = {0, 2, 4, 1, 3, 5};
    uint32_t changed = 0;
    for (unsigned bit : order) {
        const auto &patch = patches[bit];
        if (patch.scanningOnly && !scanning)
            continue;
        if (bit == 1 || bit == 3 || bit == 5) {
            const unsigned helper = bit == 1 ? 2 : 4;
            if (!extraContainsHelper(page, size, patches[helper])) {
                extraPatchBytes(page, size, patch, true);
                continue;
            }
        }
        if (extraPatchBytes(page, size, patch, false))
            changed |= 1U << bit;
    }
    return changed;
}
}
