// A Block Ack action frame that carries an HT Control field (Order bit) reads as the plain frame once it is stripped.
#include <AWDLBlockAck.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace ItlAWDL;
int main()
{
    const uint8_t peer[6] = {0x06, 0xaa, 0xbb, 0xcc, 0xdd, 0xee}, own[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    const uint8_t bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    // The phone's answer to our ADDBA request is the same frame as the one we send to answer its own: category 3,
    // action 1, from the phone to us. buildAddbaResponse(request from us, own = the phone's address) writes it.
    AddbaRequest ours{};
    memcpy(ours.peer, own, 6);
    memcpy(ours.bssid, bssid, 6);
    ours.token = 111; ours.tid = 0; ours.window = 64; ours.immediate = true; ours.ssn = 0;
    uint8_t plain[64] = {};
    assert(buildAddbaResponse(plain, sizeof(plain), ours, peer, 0x123) == kAddbaResponseLength);
    AddbaAnswer answer{};
    assert(parseAddbaAnswer(plain, kAddbaResponseLength, own, &answer) && answer.token == 111 && answer.status == 0 &&
           answer.tid == 0 && answer.window == 64 && answer.immediate);
    // The same frame with +HTC: Order bit set, four HT Control bytes after the sequence control.
    uint8_t htc[64] = {};
    memcpy(htc, plain, 24);
    htc[1] |= 0x80;
    htc[24] = 0x01; htc[25] = 0x02; htc[26] = 0x03; htc[27] = 0x04;
    memcpy(htc + 28, plain + 24, kAddbaResponseLength - 24);
    uint32_t length = kAddbaResponseLength + 4;
    assert(!parseAddbaAnswer(htc, length, own, &answer));          // read at 24 it is nothing
    uint8_t action = 9;
    assert(stripBaActionHtc(htc, &length, &action) && action == 1 && length == kAddbaResponseLength);
    assert(!memcmp(htc, plain, kAddbaResponseLength));              // byte for byte the plain frame
    assert(parseAddbaAnswer(htc, length, own, &answer) && answer.token == 111 && answer.window == 64);
    // Left alone: a plain frame, another category, an action above 2, too short, null arguments.
    uint32_t n = kAddbaResponseLength;
    assert(!stripBaActionHtc(plain, &n, &action) && n == kAddbaResponseLength);
    uint8_t vendor[40] = {}; vendor[0] = 0xd0; vendor[1] = 0x80; vendor[28] = 127; vendor[29] = 0;
    n = 40;
    assert(!stripBaActionHtc(vendor, &n, &action) && n == 40 && vendor[1] == 0x80);
    uint8_t other[40] = {}; other[0] = 0xd0; other[1] = 0x80; other[28] = 3; other[29] = 3;
    n = 40;
    assert(!stripBaActionHtc(other, &n, &action));
    uint8_t shortFrame[29] = {}; shortFrame[0] = 0xd0; shortFrame[1] = 0x80; shortFrame[28] = 3;
    n = 29;
    assert(!stripBaActionHtc(shortFrame, &n, &action) && !stripBaActionHtc(nullptr, &n, &action) && !stripBaActionHtc(htc, nullptr, &action));
    puts("Block Ack +HTC: passed");
    return 0;
}
