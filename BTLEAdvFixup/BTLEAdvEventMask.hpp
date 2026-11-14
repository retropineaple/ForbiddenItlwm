// SPDX-License-Identifier: GPL-3.0-only
// Preserve extended LE event reports with normal -btleadv. Derived from
// crazyhulk/BlueToolFixup-FixA2DP-BLE PR #1 (929c7f6), split from PR #10.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace BTLEAdv {
static const uint8_t eventMaskFind2672[] = {0x85,0xc0,0x48,0x8d,0x05,0xf0,0x32,0x90,0x00,0x48,0x8d,0x15,0xf1,0x32,0x90,0x00,0x48,0x0f,0x44,0xd0};
static const uint8_t eventMaskReplace2672[] = {0x85,0xc0,0x48,0x8d,0x05,0xf0,0x32,0x90,0x00,0x48,0x8d,0x15,0xf1,0x32,0x90,0x00,0x0f,0x1f,0x40,0x00};
static const uint8_t eventMaskFind2673[] = {0x85,0xc0,0x48,0x8d,0x05,0x00,0x33,0x90,0x00,0x48,0x8d,0x15,0x01,0x33,0x90,0x00,0x48,0x0f,0x44,0xd0};
static const uint8_t eventMaskReplace2673[] = {0x85,0xc0,0x48,0x8d,0x05,0x00,0x33,0x90,0x00,0x48,0x8d,0x15,0x01,0x33,0x90,0x00,0x0f,0x1f,0x40,0x00};
static_assert(sizeof(eventMaskFind2672) == sizeof(eventMaskReplace2672), "event mask size");
static_assert(sizeof(eventMaskFind2673) == sizeof(eventMaskReplace2673), "event mask size");

inline bool applyEventMask(const char *build, bool scanning, uint8_t *page, size_t size) {
#if defined(BTLEADV_SEQUOIA) || defined(BTLEADV_SONOMA)
    return false;
#else
    if (!build || !scanning)
        return false;
    const uint8_t *find = nullptr, *replace = nullptr;
    if (strcmp(build, "25G332") == 0) {
        find = eventMaskFind2672; replace = eventMaskReplace2672;
    } else if (strcmp(build, "25G342") == 0) {
        find = eventMaskFind2673; replace = eventMaskReplace2673;
    } else {
        return false;
    }
    const size_t length = sizeof(eventMaskFind2672);
    for (size_t i = 0; i + length <= size; ++i) {
        if (memcmp(page + i, find, length) == 0) {
            memcpy(page + i, replace, length);
            return true;
        }
    }
    return false;
#endif
}
}
