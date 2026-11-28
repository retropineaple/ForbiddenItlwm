// Finds one symbol of one kext in the Boot kernel collection, kept free of kernel types for host tests (#121).
//
// Without keepsyms=1 the kernel frees the Boot KC's symbol table about a second into boot
// (OSKext::removeKextBootstrap, "Jettisoning fileset Linkedit segments"), at about the time APFS loads. A lookup on
// APFS's load event can then read freed memory: on one Sequoia Mac Lilu faulted on the first entry of APFS's symbol
// table. Looked up at Lilu's start instead, while the table is still mapped, the hook needs no lookup later.
//
// In memory the kernel has already slid the KC: fileset entry addresses, segment addresses and symbol values
// (kernel_collection_adjust_mh_addrs in XNU's dyld_kernel_fixups.h). A KC file has them unslid. Either way they
// are used as they are, so the same code reads the running KC and a KC file mapped at its own addresses.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace BTLEAdv {
// `len` readable bytes at `addr`, or nullptr. The kext reads the running KC; host tests map a KC file.
typedef const uint8_t *(*KcReader)(void *ctx, uint64_t addr, uint64_t len);

enum KcLookup : uint32_t {
    KcFound = 1,
    KcNoHeader,        // no MH_FILESET header at the address, or its commands don't fit
    KcNoEntry,         // no fileset entry with that id
    KcBadEntry,        // the entry's Mach-O isn't a kext inside the KC's segments
    KcNoSymtab,        // the kext has no LC_SYMTAB or no __LINKEDIT
    KcBadSymtab,       // its symbol or string table lies outside __LINKEDIT or the KC's segments
    KcNoSymbol,        // no defined symbol of that name
    KcBadSymbol,       // the symbol's value isn't inside one of the kext's executable segments
};

static constexpr uint32_t kKcMagic64 = 0xfeedfacf, kKcFileset = 0xc, kKcKextBundle = 0xb;
static constexpr uint32_t kKcSegment64 = 0x19, kKcSymtab = 0x2, kKcFilesetEntry = 0x80000035;
static constexpr uint32_t kKcMaxCommandBytes = 1u << 20, kKcMaxSymbols = 4u << 20;

namespace kcdetail {
inline uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
inline uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }
// A Mach-O's load commands, checked once by commands() below.
struct Commands { const uint8_t *p; uint32_t n, size; };
// Walks the commands, calling f(cmd, pointer, cmdsize) for each until it returns false. False if one is malformed.
template <typename F>
inline bool eachCommand(const Commands &c, F f) {
    uint32_t off = 0;
    for (uint32_t i = 0; i < c.n; i++) {
        if (c.size - off < 8)
            return false;
        const uint32_t cmd = rd32(c.p + off), cmdsize = rd32(c.p + off + 4);
        if (cmdsize < 8 || cmdsize > c.size - off)
            return false;
        if (!f(cmd, c.p + off, cmdsize))
            return true;
        off += cmdsize;
    }
    return true;
}
// The load commands of the Mach-O at `addr` with the given file type, all well formed.
inline bool commands(void *ctx, KcReader read, uint64_t addr, uint32_t filetype, Commands &c) {
    const uint8_t *h = read(ctx, addr, 32);
    if (!h || rd32(h) != kKcMagic64 || rd32(h + 12) != filetype)
        return false;
    c.n = rd32(h + 16);
    c.size = rd32(h + 20);
    if (c.size > kKcMaxCommandBytes || c.n > c.size / 8)
        return false;
    const uint8_t *all = read(ctx, addr, 32 + uint64_t(c.size));
    if (!all)
        return false;
    c.p = all + 32;
    return eachCommand(c, [](uint32_t, const uint8_t *, uint32_t) { return true; });
}
// [start, start + len) inside one segment of `c` (not __LINKEDIT when `code`, and then executable)?
inline bool inSegment(const Commands &c, uint64_t start, uint64_t len, bool code) {
    if (start + len < start)
        return false;
    bool inside = false;
    eachCommand(c, [&](uint32_t cmd, const uint8_t *p, uint32_t cmdsize) {
        if (cmd != kKcSegment64 || cmdsize < 72)
            return true;
        const uint64_t vm = rd64(p + 24), size = rd64(p + 32);
        if (code && (!(rd32(p + 60) & 4) || !memcmp(p + 8, "__LINKEDIT\0", 11)))   // initprot execute
            return true;
        if (vm + size >= vm && start >= vm && start + len <= vm + size) {
            inside = true;
            return false;
        }
        return true;
    });
    return inside;
}
}  // namespace kcdetail

// Looks up `symbol` (defined, not a debugging entry) in the fileset entry `entryId` of the MH_FILESET KC whose
// header is at `kcHeader`. Stores its value in `value` and returns KcFound, or returns why it couldn't. Nothing
// past the KC header is read before it's known to lie inside one of the KC's segments.
inline uint32_t kcFindSymbol(void *ctx, KcReader read, uint64_t kcHeader, const char *entryId, const char *symbol,
                             uint64_t &value) {
    using namespace kcdetail;
    value = 0;
    Commands kc;
    if (!commands(ctx, read, kcHeader, kKcFileset, kc))
        return KcNoHeader;
    uint64_t entry = 0;
    const size_t idLen = strlen(entryId);
    eachCommand(kc, [&](uint32_t cmd, const uint8_t *c, uint32_t cmdsize) {
        if (cmd != kKcFilesetEntry || cmdsize < 32)
            return true;
        const uint32_t idOff = rd32(c + 24);
        if (idOff >= 32 && idOff < cmdsize && cmdsize - idOff > idLen && !memcmp(c + idOff, entryId, idLen + 1)) {
            entry = rd64(c + 8);
            return false;
        }
        return true;
    });
    if (!entry)
        return KcNoEntry;
    if (!inSegment(kc, entry, 32, false))
        return KcBadEntry;
    // The kext's header is read only once its first 32 bytes are known to be inside the KC; its commands only
    // once they are too.
    const uint8_t *kh = read(ctx, entry, 32);
    if (!kh || rd32(kh) != kKcMagic64 || rd32(kh + 12) != kKcKextBundle ||
        !inSegment(kc, entry, 32 + uint64_t(rd32(kh + 20)), false))
        return KcBadEntry;
    Commands kext;
    if (!commands(ctx, read, entry, kKcKextBundle, kext))
        return KcBadEntry;
    bool haveLinkedit = false, haveSymtab = false;
    uint64_t leVm = 0, leSize = 0, leFile = 0;
    uint32_t symoff = 0, nsyms = 0, stroff = 0, strsize = 0;
    eachCommand(kext, [&](uint32_t cmd, const uint8_t *c, uint32_t cmdsize) {
        if (cmd == kKcSegment64 && cmdsize >= 72 && !haveLinkedit && !memcmp(c + 8, "__LINKEDIT\0", 11)) {
            haveLinkedit = true;
            leVm = rd64(c + 24);
            leSize = rd64(c + 32);
            leFile = rd64(c + 40);
        } else if (cmd == kKcSymtab && cmdsize >= 24 && !haveSymtab) {
            haveSymtab = true;
            symoff = rd32(c + 8);
            nsyms = rd32(c + 12);
            stroff = rd32(c + 16);
            strsize = rd32(c + 20);
        }
        return true;
    });
    if (!haveLinkedit || !haveSymtab || !nsyms || !strsize)
        return KcNoSymtab;
    // Offsets are file offsets of the whole KC; __LINKEDIT's fileoff maps them to its addresses (as XNU and Lilu do).
    const uint64_t symBytes = uint64_t(nsyms) * 16;
    if (nsyms > kKcMaxSymbols || symoff < leFile || stroff < leFile || symoff - leFile > leSize ||
        symBytes > leSize - (symoff - leFile) || stroff - leFile > leSize ||
        strsize > leSize - (stroff - leFile))
        return KcBadSymtab;
    const uint64_t symAddr = leVm + (symoff - leFile), strAddr = leVm + (stroff - leFile);
    if (!inSegment(kc, symAddr, symBytes, false) || !inSegment(kc, strAddr, strsize, false))
        return KcBadSymtab;
    const uint8_t *syms = read(ctx, symAddr, symBytes), *strs = read(ctx, strAddr, strsize);
    if (!syms || !strs)
        return KcBadSymtab;
    const size_t symLen = strlen(symbol);
    for (uint32_t i = 0; i < nsyms; i++) {
        const uint8_t *s = syms + uint64_t(i) * 16;
        const uint32_t strx = rd32(s);
        const uint8_t type = s[4];
        if ((type & 0xe0) || (type & 0x0e) != 0x0e)   // debugging entries; only symbols defined in a section
            continue;
        if (strx >= strsize || strsize - strx <= symLen || memcmp(strs + strx, symbol, symLen + 1))
            continue;
        const uint64_t v = rd64(s + 8);
        if (!inSegment(kext, v, 1, true))
            return KcBadSymbol;
        value = v;
        return KcFound;
    }
    return KcNoSymbol;
}
}  // namespace BTLEAdv
