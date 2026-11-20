#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <CommonCrypto/CommonHMAC.h>
#include <PTKDerivation.hpp>

static size_t lastLabelLen, lastOutLen;

static void hmacSha256(const uint8_t *key, size_t keyLen,
                       const uint8_t *data, size_t dataLen, uint8_t out[32]) {
    CCHmac(kCCHmacAlgSHA256, key, keyLen, data, dataLen, out);
}
static void hmacSha1(const uint8_t *key, size_t keyLen,
                     const uint8_t *data, size_t dataLen, uint8_t out[20]) {
    CCHmac(kCCHmacAlgSHA1, key, keyLen, data, dataLen, out);
}

static void testKdf(const uint8_t *key, size_t keyLen, const uint8_t *label,
                    size_t labelLen, const uint8_t *context, size_t contextLen,
                    uint8_t *out, size_t outLen) {
    uint8_t data[2 + 64 + 76 + 2], block[32];
    uint16_t length = uint16_t(outLen * 8), counter = 1;
    size_t used = 0;
    while (used < outLen) {
        data[0] = uint8_t(counter);
        data[1] = uint8_t(counter >> 8);
        memcpy(data + 2, label, labelLen);
        memcpy(data + 2 + labelLen, context, contextLen);
        data[2 + labelLen + contextLen] = uint8_t(length);
        data[3 + labelLen + contextLen] = uint8_t(length >> 8);
        hmacSha256(key, keyLen, data, 4 + labelLen + contextLen, block);
        size_t n = outLen - used < 32 ? outLen - used : 32;
        memcpy(out + used, block, n);
        used += n;
        counter++;
    }
    lastLabelLen = labelLen;
    lastOutLen = outLen;
}

static void testPrf(const uint8_t *key, size_t keyLen, const uint8_t *label,
                    size_t labelLen, const uint8_t *context, size_t contextLen,
                    uint8_t *out, size_t outLen) {
    uint8_t data[64 + 76 + 1], block[20];
    size_t used = 0;
    uint8_t counter = 0;
    while (used < outLen) {
        memcpy(data, label, labelLen);
        memcpy(data + labelLen, context, contextLen);
        data[labelLen + contextLen] = counter;
        hmacSha1(key, keyLen, data, 1 + labelLen + contextLen, block);
        size_t n = outLen - used < 20 ? outLen - used : 20;
        memcpy(out + used, block, n);
        used += n;
        counter++;
    }
    lastLabelLen = labelLen;
    lastOutLen = outLen;
}

int main() {
    static const uint8_t pmk[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,
        0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };
    static const uint8_t aa[6] = {0x00,0x11,0x22,0x33,0x44,0x55};
    static const uint8_t spa[6] = {0x66,0x77,0x88,0x99,0xaa,0xbb};
    uint8_t anonce[32], snonce[32];
    for (unsigned i = 0; i < 32; ++i) {
        anonce[i] = uint8_t(0xa0 + i);
        snonce[i] = uint8_t(0xc0 + i);
    }

    static const uint8_t expected256[48] = {
        0x86,0x72,0xb9,0xd2,0xab,0xd8,0x93,0x72,0x5b,0x6c,0x4c,0xad,
        0x87,0x3c,0x04,0x04,0xae,0x83,0xd3,0x64,0x0a,0x53,0xdd,0x95,
        0x33,0xf9,0x9b,0x8f,0x82,0x7b,0x8a,0x89,0x8e,0x1f,0xe8,0x52,
        0x84,0x1e,0x1a,0x3d,0x7d,0x03,0xe9,0x39,0x9d,0xe3,0xc2,0x0c
    };
    uint8_t ptk[64];
    PTKDerivation::derive(1, pmk, aa, spa, anonce, snonce, ptk,
                          sizeof(ptk), testKdf, testPrf);
    assert(lastLabelLen == 22 && lastOutLen == 48);
    assert(memcmp(ptk, expected256, sizeof(expected256)) == 0);

    memset(ptk, 0, sizeof(ptk));
    PTKDerivation::derive(1, pmk, spa, aa, snonce, anonce, ptk,
                          sizeof(ptk), testKdf, testPrf);
    assert(memcmp(ptk, expected256, sizeof(expected256)) == 0);

    static const uint8_t expectedSha1[64] = {
        0xdf,0x08,0xe4,0x73,0xc4,0xae,0xa9,0x77,0xdc,0xef,0xfa,0x0c,
        0xec,0x8e,0x8c,0x93,0xed,0xde,0x1c,0x4b,0x6c,0x51,0xea,0x73,
        0x97,0x16,0x8e,0x7c,0x70,0x81,0xd2,0xe6,0x51,0xff,0x5c,0x73,
        0xa8,0x78,0xd4,0x84,0x15,0x49,0xda,0xaf,0x46,0xe7,0x3e,0x65,
        0xd7,0xc8,0xde,0xb9,0x55,0x11,0x37,0x69,0x12,0x9e,0x4d,0x87,
        0xa2,0xae,0x55,0x06
    };
    memset(ptk, 0, sizeof(ptk));
    PTKDerivation::derive(0, pmk, aa, spa, anonce, snonce, ptk,
                          sizeof(ptk), testKdf, testPrf);
    assert(lastLabelLen == 23 && lastOutLen == 64);
    assert(memcmp(ptk, expectedSha1, sizeof(expectedSha1)) == 0);

    puts("PTK derivation: passed (known answer)");
}
