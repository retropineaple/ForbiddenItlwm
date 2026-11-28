// BTLEAdvKcSymbol.hpp: finding _apfs_vnop_verify in the Boot KC at Lilu's start (#121).
// Without arguments: a small fileset KC built here, with the success path and every refusal.
// With KC file paths (BootKernelExtensions.kc): looks the symbol up in each and prints it.
#include <BTLEAdvKcSymbol.hpp>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using namespace BTLEAdv;

static const uint64_t kBase = 0xffffff8000200000ULL;
static const char *kApfs = "com.apple.filesystems.apfs";
static const char *kSym = "_apfs_vnop_verify";

struct Image {
    std::vector<uint8_t> bytes;
    uint64_t base;       // address of byte 0 (synthetic) ...
    bool segmented;      // ... or map through the KC's segments (real files)
    struct Seg { uint64_t vm, size, fileoff, filesize; };
    std::vector<Seg> segs;
};
static const uint8_t *readImage(void *ctx, uint64_t addr, uint64_t len) {
    const Image &im = *static_cast<const Image *>(ctx);
    if (!im.segmented) {
        if (addr < im.base || addr - im.base > im.bytes.size() || len > im.bytes.size() - (addr - im.base))
            return nullptr;
        return im.bytes.data() + (addr - im.base);
    }
    for (const Image::Seg &s : im.segs)
        if (addr >= s.vm && addr - s.vm <= s.filesize && len <= s.filesize - (addr - s.vm)) {
            const uint64_t off = s.fileoff + (addr - s.vm);
            if (off > im.bytes.size() || len > im.bytes.size() - off)
                return nullptr;
            return im.bytes.data() + off;
        }
    return nullptr;
}

static void put32(std::vector<uint8_t> &b, size_t off, uint32_t v) { memcpy(&b[off], &v, 4); }
static void put64(std::vector<uint8_t> &b, size_t off, uint64_t v) { memcpy(&b[off], &v, 8); }
static void putName(std::vector<uint8_t> &b, size_t off, const char *n) { memset(&b[off], 0, 16); memcpy(&b[off], n, strlen(n)); }
// LC_SEGMENT_64 with no sections (72 bytes).
static size_t segment(std::vector<uint8_t> &b, size_t off, const char *name, uint64_t vm, uint64_t size,
                      uint64_t fileoff, uint32_t prot) {
    put32(b, off, kKcSegment64);
    put32(b, off + 4, 72);
    putName(b, off + 8, name);
    put64(b, off + 24, vm);
    put64(b, off + 32, size);
    put64(b, off + 40, fileoff);
    put64(b, off + 48, size);
    put32(b, off + 56, prot);
    put32(b, off + 60, prot);
    return off + 72;
}
static size_t filesetEntry(std::vector<uint8_t> &b, size_t off, uint64_t vm, const char *id) {
    const uint32_t size = uint32_t((32 + strlen(id) + 1 + 7) & ~size_t(7));
    put32(b, off, kKcFilesetEntry);
    put32(b, off + 4, size);
    put64(b, off + 8, vm);
    put64(b, off + 16, vm - kBase);
    put32(b, off + 24, 32);
    memcpy(&b[off + 32], id, strlen(id) + 1);
    return off + size;
}
static void header(std::vector<uint8_t> &b, size_t off, uint32_t filetype, uint32_t ncmds, uint32_t sizeofcmds) {
    put32(b, off, kKcMagic64);
    put32(b, off + 4, 0x01000007);
    put32(b, off + 8, 3);
    put32(b, off + 12, filetype);
    put32(b, off + 16, ncmds);
    put32(b, off + 20, sizeofcmds);
}

// Layout: outer header + commands at 0; apfs's header at 0x4000 (in __REGION0), its code at 0x5000;
// the shared __LINKEDIT at 0x8000 with the symbols at 0x8000 and their names at 0x8100.
struct Layout { size_t kextCmds, symtab, nlist, strs, entryVm, kextFiletype; };
static Image build(Layout &L) {
    Image im;
    im.bytes.assign(0xc000, 0);
    im.base = kBase;
    im.segmented = false;
    std::vector<uint8_t> &b = im.bytes;
    size_t o = 32;
    o = segment(b, o, "__TEXT", kBase, 0x4000, 0, 1);
    o = segment(b, o, "__REGION0", kBase + 0x4000, 0x4000, 0x4000, 5);
    o = segment(b, o, "__LINKEDIT", kBase + 0x8000, 0x4000, 0x8000, 1);
    o = filesetEntry(b, o, kBase + 0x4000 + 0x800, "com.apple.kernel");   // a decoy (no kext there)
    L.entryVm = o + 8;
    o = filesetEntry(b, o, kBase + 0x4000, kApfs);
    header(b, 0, kKcFileset, 5, uint32_t(o - 32));
    // apfs's Mach-O
    L.kextFiletype = 0x4000 + 12;
    size_t k = 0x4000 + 32;
    k = segment(b, k, "__TEXT", kBase + 0x4000, 0x1000, 0x4000, 1);
    k = segment(b, k, "__TEXT_EXEC", kBase + 0x5000, 0x1000, 0x5000, 5);
    k = segment(b, k, "__DATA", kBase + 0x6000, 0x1000, 0x6000, 3);
    k = segment(b, k, "__LINKEDIT", kBase + 0x8000, 0x4000, 0x8000, 1);
    L.symtab = k;
    put32(b, k, kKcSymtab);
    put32(b, k + 4, 24);
    put32(b, k + 8, 0x8000);    // symoff (file offset)
    put32(b, k + 12, 5);        // nsyms
    put32(b, k + 16, 0x8100);   // stroff
    put32(b, k + 20, 0x100);    // strsize
    k += 24;
    header(b, 0x4000, kKcKextBundle, 5, uint32_t(k - 0x4000 - 32));
    L.kextCmds = 0x4000 + 32;
    // names
    const char *names = "\0_apfs_vnop_verify\0_other\0_apfs_vnop_verify_x\0_apfs_vnop_verif\0";
    memcpy(&b[0x8100], names, 64);
    const uint32_t nVerify = 1, nOther = 19, nLonger = 26, nShorter = 46;
    L.nlist = 0x8000;
    L.strs = 0x8100;
    struct { uint32_t strx; uint8_t type; uint64_t value; } syms[] = {
        {nVerify, 0x24, kBase + 0x5200},    // a debugging entry with the same name: skipped
        {nOther, 0x0f, kBase + 0x5010},
        {nLonger, 0x0f, kBase + 0x5020},    // longer name with the same start
        {nShorter, 0x0f, kBase + 0x5030},   // shorter name, a prefix of it
        {nVerify, 0x0f, kBase + 0x5100},    // the one
    };
    for (size_t i = 0; i < 5; i++) {
        put32(b, 0x8000 + i * 16, syms[i].strx);
        b[0x8000 + i * 16 + 4] = syms[i].type;
        b[0x8000 + i * 16 + 5] = 1;
        put64(b, 0x8000 + i * 16 + 8, syms[i].value);
    }
    return im;
}
static uint32_t lookup(Image &im, const char *entry, const char *sym, uint64_t &v) {
    return kcFindSymbol(&im, readImage, kBase, entry, sym, v);
}

static int realKC(const char *path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { printf("%s: can't open\n", path); return 1; }
    Image im;
    im.bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    im.segmented = true;
    im.base = 0;
    // Map through the outer segments; the header itself is in __TEXT at file offset 0.
    uint64_t kc = 0;
    const uint32_t ncmds = kcdetail::rd32(&im.bytes[16]);
    size_t o = 32;
    for (uint32_t i = 0; i < ncmds; i++) {
        const uint32_t cmd = kcdetail::rd32(&im.bytes[o]), size = kcdetail::rd32(&im.bytes[o + 4]);
        if (cmd == kKcSegment64) {
            Image::Seg s{kcdetail::rd64(&im.bytes[o + 24]), kcdetail::rd64(&im.bytes[o + 32]),
                         kcdetail::rd64(&im.bytes[o + 40]), kcdetail::rd64(&im.bytes[o + 48])};
            im.segs.push_back(s);
            if (s.fileoff == 0 && s.filesize)
                kc = s.vm;
        }
        o += size;
    }
    uint64_t v = 0;
    const uint32_t r = kcFindSymbol(&im, readImage, kc, kApfs, kSym, v);
    printf("%s: result %u, %s = 0x%llx\n", path, r, kSym, (unsigned long long)v);
    return r == KcFound ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc > 1) {
        int bad = 0;
        for (int i = 1; i < argc; i++)
            bad |= realKC(argv[i]);
        return bad;
    }
    Layout L;
    uint64_t v = 1;
    {   // found: the defined symbol, not the debugging entry or the names around it
        Image im = build(L);
        assert(lookup(im, kApfs, kSym, v) == KcFound && v == kBase + 0x5100);
        assert(lookup(im, kApfs, "_other", v) == KcFound && v == kBase + 0x5010);
        assert(lookup(im, kApfs, "_apfs_vnop_verif", v) == KcFound && v == kBase + 0x5030);
        assert(lookup(im, kApfs, "_missing", v) == KcNoSymbol && v == 0);
        assert(lookup(im, "com.apple.filesystems", kSym, v) == KcNoEntry);   // ids match whole, not by prefix
        assert(lookup(im, "com.apple.kernel", kSym, v) == KcBadEntry);        // no kext header at the decoy
    }
    {   // not a fileset KC, or a bad header
        Image im = build(L);
        put32(im.bytes, 0, 0xfeedface);
        assert(lookup(im, kApfs, kSym, v) == KcNoHeader);
        im = build(L);
        put32(im.bytes, 12, kKcKextBundle);
        assert(lookup(im, kApfs, kSym, v) == KcNoHeader);
        im = build(L);
        put32(im.bytes, 20, 0x7fffffff);                   // sizeofcmds beyond what's readable
        assert(lookup(im, kApfs, kSym, v) == KcNoHeader);
        im = build(L);
        put32(im.bytes, 32 + 4, 0);                        // a command of size 0
        assert(lookup(im, kApfs, kSym, v) == KcNoHeader);
        im = build(L);
        put32(im.bytes, 16, 1000);                         // more commands than fit
        assert(lookup(im, kApfs, kSym, v) == KcNoHeader);
        assert(kcFindSymbol(&im, readImage, kBase + 0x20000, kApfs, kSym, v) == KcNoHeader);   // unreadable
    }
    {   // the entry points outside the KC's segments, or not at a kext
        Image im = build(L);
        put64(im.bytes, L.entryVm, kBase + 0x10000);
        assert(lookup(im, kApfs, kSym, v) == KcBadEntry);
        im = build(L);
        put32(im.bytes, L.kextFiletype, kKcFileset);
        assert(lookup(im, kApfs, kSym, v) == KcBadEntry);
    }
    {   // symbol and string tables outside __LINKEDIT
        Image im = build(L);
        put32(im.bytes, L.symtab + 8, 0x7ff0);             // symoff before __LINKEDIT's file offset
        assert(lookup(im, kApfs, kSym, v) == KcBadSymtab);
        im = build(L);
        put32(im.bytes, L.symtab + 12, 0x401);             // nsyms running past __LINKEDIT (0x4010 bytes)
        assert(lookup(im, kApfs, kSym, v) == KcBadSymtab);
        im = build(L);
        put32(im.bytes, L.symtab + 12, 0x7fffffff);        // huge nsyms
        assert(lookup(im, kApfs, kSym, v) == KcBadSymtab);
        im = build(L);
        put32(im.bytes, L.symtab + 20, 0x4000);            // strings past the end
        assert(lookup(im, kApfs, kSym, v) == KcBadSymtab);
        im = build(L);
        put32(im.bytes, L.symtab + 12, 0);
        assert(lookup(im, kApfs, kSym, v) == KcNoSymtab);
    }
    {   // a name index past the strings is skipped; a value outside the code is refused
        Image im = build(L);
        put32(im.bytes, L.nlist + 4 * 16, 0x100);          // the wanted entry's name index == strsize
        assert(lookup(im, kApfs, kSym, v) == KcNoSymbol);
        im = build(L);
        put64(im.bytes, L.nlist + 4 * 16 + 8, kBase + 0x6010);   // in __DATA (not executable)
        assert(lookup(im, kApfs, kSym, v) == KcBadSymbol && v == 0);
        im = build(L);
        put64(im.bytes, L.nlist + 4 * 16 + 8, kBase + 0x9000);   // in __LINKEDIT
        assert(lookup(im, kApfs, kSym, v) == KcBadSymbol);
    }
    printf("BTLEAdv Boot KC symbol lookup: passed\n");
    return 0;
}
