// WPA3-SAE joins as classified from a WCL association request (WCL.hpp).
#include <WCL.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using AirportWCL::LegacyAuth;
using AirportWCL::classifyLegacyAuth;
using AirportWCL::JoinRequest;

// A minimal CCMP RSN element, so validRSNOverride() and the cipher check pass.
static void makeRSN(JoinRequest &j, bool ccmp = true) {
    const uint8_t ie[] = {48, 20, 1, 0, 0, 0x0f, 0xac, uint8_t(ccmp ? 4 : 2),
                          1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 8,
                          0x00, 0x80};
    static_assert(sizeof(ie) == 22, "RSN element");
    memcpy(j.rsn, ie, sizeof(ie));
    j.rsnLength = sizeof(ie);
}

static JoinRequest base() {
    JoinRequest j = {};
    j.lowerAuth = 1;
    j.ssidLength = 3;
    memcpy(j.ssid, "net", 3);
    makeRSN(j);
    return j;
}

int main() {
    const uint32_t SAE = 1u << 12;

    // Pure WPA3-SAE: raw passphrase with cipher 10.
    {
        auto j = base();
        j.upperAuth = SAE;
        j.keyCipher = 10;
        j.keyLength = 8;
        memcpy(j.key, "password", 8);
        assert(classifyLegacyAuth(j) == LegacyAuth::PersonalSAE);
    }
    // Longest passphrase WCL can hand over (63 bytes) still fits.
    {
        auto j = base();
        j.upperAuth = SAE;
        j.keyCipher = 10;
        j.keyLength = 63;
        memset(j.key, 'a', 63);
        assert(classifyLegacyAuth(j) == LegacyAuth::PersonalSAE);
    }
    // SAE requires a passphrase, cipher 10 and a CCMP group cipher.
    {
        auto j = base();
        j.upperAuth = SAE;
        j.keyCipher = 6;
        j.keyLength = 32;
        assert(classifyLegacyAuth(j) == LegacyAuth::Invalid);
    }
    {
        auto j = base();
        j.upperAuth = SAE;
        j.keyCipher = 10;
        j.keyLength = 0;
        assert(classifyLegacyAuth(j) == LegacyAuth::Invalid);
    }
    {
        auto j = base();
        j.upperAuth = SAE;
        j.keyCipher = 10;
        j.keyLength = 64;
        memset(j.key, 'a', sizeof(j.key));
        assert(classifyLegacyAuth(j) == LegacyAuth::Invalid);
    }
    {
        auto j = base();
        makeRSN(j, false);
        j.upperAuth = SAE;
        j.keyCipher = 10;
        j.keyLength = 8;
        assert(classifyLegacyAuth(j) == LegacyAuth::Invalid);
    }
    // Mixing SAE with WPA2 types is not how WCL expresses a transition join.
    {
        auto j = base();
        j.upperAuth = SAE | (1u << 1);
        j.keyCipher = 10;
        j.keyLength = 8;
        assert(classifyLegacyAuth(j) == LegacyAuth::Invalid);
    }

    // A WPA2 join with a raw passphrase (cipher 10) is still Personal; the
    // caller derives the PSK with PBKDF2.
    {
        auto j = base();
        j.upperAuth = 1u << 1;
        j.keyCipher = 10;
        j.keyLength = 8;
        memcpy(j.key, "password", 8);
        assert(classifyLegacyAuth(j) == LegacyAuth::Personal);
    }
    // Embedded NUL bytes are not a passphrase.
    {
        auto j = base();
        j.upperAuth = 1u << 1;
        j.keyCipher = 10;
        j.keyLength = 8;
        memcpy(j.key, "pass\0ord", 8);
        assert(classifyLegacyAuth(j) == LegacyAuth::Invalid);
    }
    // Unchanged: classic 32-byte PSK and enterprise.
    {
        auto j = base();
        j.upperAuth = 1u << 1;
        j.keyCipher = 6;
        j.keyLength = 32;
        assert(classifyLegacyAuth(j) == LegacyAuth::Personal);
    }
    {
        auto j = base();
        j.upperAuth = 1u << 0;
        j.keyLength = 0;
        assert(classifyLegacyAuth(j) == LegacyAuth::Enterprise);
    }

    // decodeJoin must accept the enlarged key field used by SAE passphrases.
    {
        std::vector<uint8_t> p(0x218 + 18, 0);
        const uint32_t count = 1, ssidLength = 3, keyLength = 63;
        memcpy(&p[0x214], &count, 4);
        memcpy(&p[0x1c], &ssidLength, 4);
        memcpy(&p[0x44], &keyLength, 4);
        memcpy(&p[0x20], "net", 3);
        memset(&p[0x50], 'a', keyLength);
        const uint16_t rsnLength = 0;
        memcpy(&p[0xd4], &rsnLength, 2);
        p[0x21c] = 0x02;    // a non-zero BSSID
        JoinRequest j;
        assert(AirportWCL::decodeJoin(p.data(), p.size(), j));
        assert(j.keyLength == keyLength);
        assert(memcmp(j.key, p.data() + 0x50, keyLength) == 0);
    }
    puts("WCL WPA3-SAE classification: passed (host only)");
}
