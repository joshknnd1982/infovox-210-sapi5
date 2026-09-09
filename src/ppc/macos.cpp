#include "macos.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ppc {
namespace {

// QEMU gates every floating point instruction on MSR[FP]; without it the first
// lfd raises an unhandled CPU exception.
constexpr uint32_t kMsrFp = 1u << 13;

inline uint32_t roundUp16(uint32_t v) { return (v + 15u) & ~15u; }

std::string fourCcToString(uint32_t v) {
    char s[5] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v), 0};
    return std::string(s, 4);
}

}  // namespace

MacRuntime::MacRuntime() = default;

MacRuntime::~MacRuntime() {
    if (uc_) uc_close(uc_);
}

uint32_t MacRuntime::gpr(int n) const {
    uint32_t v = 0;
    uc_reg_read(uc_, UC_PPC_REG_0 + n, &v);
    return v;
}
void MacRuntime::setGpr(int n, uint32_t v) { uc_reg_write(uc_, UC_PPC_REG_0 + n, &v); }

double MacRuntime::fpr(int n) const {
    uint64_t bits = 0;
    uc_reg_read(uc_, UC_PPC_REG_FPR0 + n, &bits);
    double d;
    memcpy(&d, &bits, 8);
    return d;
}
void MacRuntime::setFpr(int n, double v) {
    uint64_t bits;
    memcpy(&bits, &v, 8);
    uc_reg_write(uc_, UC_PPC_REG_FPR0 + n, &bits);
}

uint32_t MacRuntime::rd32(uint32_t a) const {
    uint8_t b[4] = {0, 0, 0, 0};
    uc_mem_read(uc_, a, b, 4);
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) |
           (uint32_t(b[2]) << 8) | b[3];
}
uint16_t MacRuntime::rd16(uint32_t a) const {
    uint8_t b[2] = {0, 0};
    uc_mem_read(uc_, a, b, 2);
    return uint16_t((uint32_t(b[0]) << 8) | b[1]);
}
uint8_t MacRuntime::rd8(uint32_t a) const {
    uint8_t b = 0;
    uc_mem_read(uc_, a, &b, 1);
    return b;
}
void MacRuntime::wr32(uint32_t a, uint32_t v) {
    uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    uc_mem_write(uc_, a, b, 4);
}
void MacRuntime::wr16(uint32_t a, uint16_t v) {
    uint8_t b[2] = {uint8_t(v >> 8), uint8_t(v)};
    uc_mem_write(uc_, a, b, 2);
}
void MacRuntime::wr8(uint32_t a, uint8_t v) { uc_mem_write(uc_, a, &v, 1); }
void MacRuntime::read(uint32_t a, void* dst, size_t n) const {
    if (n) uc_mem_read(uc_, a, dst, n);
}
void MacRuntime::write(uint32_t a, const void* src, size_t n) {
    if (n) uc_mem_write(uc_, a, src, n);
}

// ---------------------------------------------------------------- heap -----

uint32_t MacRuntime::alloc(uint32_t size) {
    size = roundUp16(size ? size : 1);
    for (size_t i = 0; i < freeList_.size(); ++i) {
        if (freeList_[i].size >= size) {
            uint32_t a = freeList_[i].addr, sz = freeList_[i].size;
            freeList_.erase(freeList_.begin() + i);
            if (sz > size + 64) freeList_.push_back({a + size, sz - size});
            return a;
        }
    }
    if (heapNext_ + size > kHeapBase + kHeapSize) return 0;
    uint32_t a = heapNext_;
    heapNext_ += size;
    return a;
}

uint32_t MacRuntime::newHandle(uint32_t size, bool clear) {
    uint32_t p = alloc(size);
    if (!p) { memErr_ = kMemFullErr; return 0; }
    if (clear) {
        std::vector<uint8_t> zeros(roundUp16(size), 0);
        write(p, zeros.data(), zeros.size());
    }
    uint32_t mp = mptrNext_;
    mptrNext_ += 4;
    wr32(mp, p);
    handles_[mp] = {size, p, 0};
    ptrToHandle_[p] = mp;
    memErr_ = kNoErr;
    return mp;
}

void MacRuntime::disposeHandle(uint32_t mp) {
    auto it = handles_.find(mp);
    if (it == handles_.end()) return;
    ptrToHandle_.erase(it->second.ptr);
    freeList_.push_back({it->second.ptr, roundUp16(it->second.size)});
    handles_.erase(it);
    wr32(mp, 0);
    memErr_ = kNoErr;
}

int16_t MacRuntime::setHandleSize(uint32_t mp, uint32_t newSize) {
    auto it = handles_.find(mp);
    if (it == handles_.end()) return kMemFullErr;
    if (roundUp16(newSize) <= roundUp16(it->second.size)) {
        it->second.size = newSize;
        return kNoErr;
    }
    uint32_t p = alloc(newSize);
    if (!p) return kMemFullErr;
    std::vector<uint8_t> tmp(it->second.size);
    read(it->second.ptr, tmp.data(), tmp.size());
    write(p, tmp.data(), tmp.size());
    ptrToHandle_.erase(it->second.ptr);
    freeList_.push_back({it->second.ptr, roundUp16(it->second.size)});
    it->second.ptr = p;
    it->second.size = newSize;
    ptrToHandle_[p] = mp;
    wr32(mp, p);
    return kNoErr;
}

uint32_t MacRuntime::handleSize(uint32_t mp) const {
    auto it = handles_.find(mp);
    return it == handles_.end() ? 0 : it->second.size;
}

// ---------------------------------------------------------------- init -----

bool MacRuntime::init(const ResourcePack* component, std::string* err) {
    component_ = component;
    const std::vector<uint8_t>* ppcm = component->find(FourCC("PPCm"), 128);
    if (!ppcm) { if (err) *err = "component pack has no PPCm 128"; return false; }

    uc_mode mode = uc_mode(UC_MODE_32 | UC_MODE_BIG_ENDIAN);
    if (uc_open(UC_ARCH_PPC, mode, &uc_) != UC_ERR_OK) {
        if (err) *err = "cannot create PowerPC CPU";
        return false;
    }
    struct { uint32_t base, size; } regions[] = {
        {kCodeBase, 0x00200000}, {kDataBase, 0x00100000},
        {kImpTVec, 0x00010000},  {kImpCode, 0x00010000},
        {kHeapBase, kHeapSize},  {kMptrBase, kMptrSize},
        {kStackTop - kStackSize, kStackSize},
        {kMagicRet & ~0xFFFu, 0x1000},
    };
    for (auto& r : regions) {
        if (uc_mem_map(uc_, r.base, r.size, UC_PROT_ALL) != UC_ERR_OK) {
            if (err) *err = "cannot map guest memory";
            return false;
        }
    }

    // Give every import a distinct fake TVector plus a one-instruction stub.
    // The stub is `blr`; a code hook implements the call before it runs.
    std::vector<uint32_t> importAddrs(512);
    for (size_t i = 0; i < importAddrs.size(); ++i)
        importAddrs[i] = kImpTVec + uint32_t(i) * 8;

    if (!pef_.load(ppcm->data(), ppcm->size(), kCodeBase, kDataBase,
                   importAddrs, err))
        return false;

    write(kCodeBase, pef_.code().data(), pef_.code().size());
    write(kDataBase, pef_.data().data(), pef_.data().size());

    const auto& imps = pef_.imports();
    importNames_.clear();
    for (const auto& im : imps) importNames_.push_back(im.name);
    for (size_t i = 0; i < imps.size(); ++i) {
        wr32(kImpTVec + uint32_t(i) * 8, kImpCode + uint32_t(i) * 4);
        wr32(kImpTVec + uint32_t(i) * 8 + 4, kDataBase);
        wr32(kImpCode + uint32_t(i) * 4, 0x4E800020);   // blr
    }

    static const struct { const char* name; void (MacRuntime::*fn)(); } kTable[] = {
        {"NewHandleClear", &MacRuntime::impNewHandleClear},
        {"DisposeHandle", &MacRuntime::impDisposeHandle},
        {"GetHandleSize", &MacRuntime::impGetHandleSize},
        {"SetHandleSize", &MacRuntime::impSetHandleSize},
        {"MemError", &MacRuntime::impMemError},
        {"HLock", &MacRuntime::impHLock},
        {"HUnlock", &MacRuntime::impHUnlock},
        {"HLockHi", &MacRuntime::impHLockHi},
        {"HPurge", &MacRuntime::impHPurge},
        {"HGetState", &MacRuntime::impHGetState},
        {"HSetState", &MacRuntime::impHSetState},
        {"RecoverHandle", &MacRuntime::impRecoverHandle},
        {"BlockMove", &MacRuntime::impBlockMove},
        {"BlockMoveData", &MacRuntime::impBlockMove},
        {"Munger", &MacRuntime::impMunger},
        {"OpenComponentResFile", &MacRuntime::impOpenComponentResFile},
        {"CloseComponentResFile", &MacRuntime::impCloseComponentResFile},
        {"CloseResFile", &MacRuntime::impCloseResFile},
        {"UseResFile", &MacRuntime::impUseResFile},
        {"CurResFile", &MacRuntime::impCurResFile},
        {"HOpenResFile", &MacRuntime::impHOpenResFile},
        {"GetResource", &MacRuntime::impGetResource},
        {"ReleaseResource", &MacRuntime::impReleaseResource},
        {"DetachResource", &MacRuntime::impDetachResource},
        {"SndSoundManagerVersion", &MacRuntime::impSndSoundManagerVersion},
        {"SndNewChannel", &MacRuntime::impSndNewChannel},
        {"SndDisposeChannel", &MacRuntime::impSndDisposeChannel},
        {"SndDoImmediate", &MacRuntime::impSndDoImmediate},
        {"SndChannelStatus", &MacRuntime::impSndChannelStatus},
        {"SndPlayDoubleBuffer", &MacRuntime::impSndPlayDoubleBuffer},
        {"NewRoutineDescriptor", &MacRuntime::impNewRoutineDescriptor},
        {"DisposeRoutineDescriptor", &MacRuntime::impDisposeRoutineDescriptor},
        {"CallUniversalProc", &MacRuntime::impCallUniversalProc},
        {"SetA5", &MacRuntime::impSetA5},
        {"SetComponentInstanceStorage", &MacRuntime::impSetComponentInstanceStorage},
        {"CountComponentInstances", &MacRuntime::impCountComponentInstances},
        {"InsXTime", &MacRuntime::impInsXTime},
        {"PrimeTime", &MacRuntime::impPrimeTime},
        {"RmvTime", &MacRuntime::impRmvTime},
        {"DTInstall", &MacRuntime::impDTInstall},
        {"Microseconds", &MacRuntime::impMicroseconds},
        {"NMInstall", &MacRuntime::impNMInstall},
        {"NMRemove", &MacRuntime::impNMRemove},
        {"Gestalt", &MacRuntime::impGestalt},
        {"NumToString", &MacRuntime::impNumToString},
        {"FixDiv", &MacRuntime::impFixDiv},
        {"FixMul", &MacRuntime::impFixMul},
        {"Fix2Long", &MacRuntime::impFix2Long},
        {"Long2Fix", &MacRuntime::impLong2Fix},
        {"pow", &MacRuntime::impPow},
    };

    importFns_.assign(importNames_.size(), nullptr);
    for (size_t i = 0; i < importNames_.size(); ++i) {
        const std::string& n = importNames_[i];
        for (const auto& e : kTable) {
            if (n == e.name) { importFns_[i] = e.fn; break; }
        }
        if (importFns_[i]) continue;
        if (n == "sin")  { importFns_[i] = nullptr; }
        // MathLib unaries are dispatched by name in hookImport().
    }

    uc_hook h;
    if (uc_hook_add(uc_, &h, UC_HOOK_CODE, (void*)&MacRuntime::hookImportTramp, this,
                    kImpCode, kImpCode + uint64_t(importNames_.size()) * 4) != UC_ERR_OK) {
        if (err) *err = "cannot install import hook";
        return false;
    }
    return true;
}

void MacRuntime::addVoicePack(const std::string& fileName, const ResourcePack* pack) {
    voicePacks_[fileName] = pack;
}

// ----------------------------------------------------------- execution -----

void MacRuntime::hookImportTramp(uc_engine*, uint64_t addr, uint32_t, void* user) {
    static_cast<MacRuntime*>(user)->hookImport(addr);
}

void MacRuntime::hookImport(uint64_t addr) {
    size_t idx = size_t((addr - kImpCode) / 4);
    if (idx >= importNames_.size()) return;
    const std::string& n = importNames_[idx];
    if (importFns_[idx]) {
        (this->*importFns_[idx])();
        return;
    }
    // MathLib transcendentals
    if (n == "sin")  { impMath1(::sin);  return; }
    if (n == "cos")  { impMath1(::cos);  return; }
    if (n == "exp")  { impMath1(::exp);  return; }
    if (n == "sqrt") { impMath1(::sqrt); return; }
    if (n == "fabs") { impMath1(::fabs); return; }
    if (n == "GetVoiceInfo") {
        int32_t r = voiceInfo_ ? voiceInfo_(gpr(3), gpr(4), gpr(5)) : -244;
        setGpr(3, uint32_t(r));
        return;
    }
    faulted_ = true;
    lastError_ = "unimplemented import: " + n;
    uc_emu_stop(uc_);
}

void MacRuntime::run(uint32_t pc) {
    for (;;) {
        uint32_t msr = kMsrFp;
        uc_reg_write(uc_, UC_PPC_REG_MSR, &msr);
        redirectPending_ = false;
        uc_err e = uc_emu_start(uc_, pc, kMagicRet, 0, 0);
        if (e != UC_ERR_OK && !redirectPending_) {
            faulted_ = true;
            lastError_ = std::string("PowerPC fault: ") + uc_strerror(e);
            return;
        }
        if (!redirectPending_) return;
        pc = redirectPc_;
        uc_reg_write(uc_, UC_PPC_REG_2, &redirectToc_);
    }
}

int32_t MacRuntime::call(uint32_t codeAddr, uint32_t toc,
                         const std::vector<uint32_t>& args) {
    uint32_t sp = kStackTop - 0x10000;
    std::vector<uint8_t> zeros(0x100, 0);
    write(sp, zeros.data(), zeros.size());
    uc_reg_write(uc_, UC_PPC_REG_1, &sp);
    uc_reg_write(uc_, UC_PPC_REG_2, &toc);
    for (size_t i = 0; i < args.size() && i < 8; ++i) setGpr(int(3 + i), args[i]);
    uint32_t lr = kMagicRet;
    uc_reg_write(uc_, UC_PPC_REG_LR, &lr);
    run(codeAddr);
    return int32_t(gpr(3));
}

int32_t MacRuntime::callUpp(uint32_t upp, const std::vector<uint32_t>& args) {
    uint32_t tvec = upp;
    auto it = upps_.find(upp);
    if (it != upps_.end()) {
        tvec = it->second;
    } else if (rd16(upp) == 0xAAFE) {
        tvec = rd32(upp + 20);
    }
    uint32_t code = rd32(tvec), toc = rd32(tvec + 4);
    return call(code, toc, args);
}

void MacRuntime::runPendingTasks() {
    std::vector<uint32_t> dts;
    dts.swap(deferredTasks_);
    for (uint32_t dt : dts) callUpp(rd32(dt + 8), {rd32(dt + 12)});
    for (uint32_t tt : timerTasks_) callUpp(rd32(tt + 6), {tt});
    dts.clear();
    dts.swap(deferredTasks_);
    for (uint32_t dt : dts) callUpp(rd32(dt + 8), {rd32(dt + 12)});
}

// ------------------------------------------------------ Memory Manager -----

void MacRuntime::impNewHandleClear() { setGpr(3, newHandle(gpr(3), true)); }
void MacRuntime::impDisposeHandle()  { disposeHandle(gpr(3)); }
void MacRuntime::impGetHandleSize()  { memErr_ = kNoErr; setGpr(3, handleSize(gpr(3))); }
void MacRuntime::impSetHandleSize()  { memErr_ = setHandleSize(gpr(3), gpr(4)); }
void MacRuntime::impMemError()       { setGpr(3, uint32_t(uint16_t(memErr_))); }
void MacRuntime::impHLock()          { memErr_ = kNoErr; }
void MacRuntime::impHUnlock()        { memErr_ = kNoErr; }
void MacRuntime::impHLockHi()        { memErr_ = kNoErr; }
void MacRuntime::impHPurge()         { memErr_ = kNoErr; }
void MacRuntime::impHGetState() {
    auto it = handles_.find(gpr(3));
    setGpr(3, it == handles_.end() ? 0 : it->second.flags);
}
void MacRuntime::impHSetState() {
    auto it = handles_.find(gpr(3));
    if (it != handles_.end()) it->second.flags = uint8_t(gpr(4));
}
void MacRuntime::impRecoverHandle() {
    auto it = ptrToHandle_.find(gpr(3));
    setGpr(3, it == ptrToHandle_.end() ? 0 : it->second);
}
void MacRuntime::impBlockMove() {
    uint32_t src = gpr(3), dst = gpr(4), n = gpr(5);
    if (!n) return;
    std::vector<uint8_t> tmp(n);
    read(src, tmp.data(), n);
    write(dst, tmp.data(), n);
}

void MacRuntime::impMunger() {
    uint32_t h = gpr(3);
    int32_t off = int32_t(gpr(4));
    uint32_t p1 = gpr(5);
    int32_t l1 = int32_t(gpr(6));
    uint32_t p2 = gpr(7);
    int32_t l2 = int32_t(gpr(8));
    auto it = handles_.find(h);
    if (it == handles_.end()) { setGpr(3, 0xFFFFFFFFu); return; }

    std::vector<uint8_t> cur(it->second.size);
    read(it->second.ptr, cur.data(), cur.size());

    size_t pos, mlen;
    if (p1 == 0) {
        if (off < 0 || size_t(off) > cur.size()) { setGpr(3, 0xFFFFFFFFu); return; }
        pos = size_t(off);
        mlen = size_t(l1 > 0 ? l1 : 0);
    } else {
        std::vector<uint8_t> pat(l1 > 0 ? size_t(l1) : 0);
        if (!pat.empty()) read(p1, pat.data(), pat.size());
        size_t from = size_t(off > 0 ? off : 0);
        auto found = std::search(cur.begin() + std::min(from, cur.size()), cur.end(),
                                 pat.begin(), pat.end());
        if (pat.empty() || found == cur.end()) { setGpr(3, 0xFFFFFFFFu); return; }
        pos = size_t(found - cur.begin());
        mlen = pat.size();
    }
    if (pos + mlen > cur.size()) mlen = cur.size() - pos;

    std::vector<uint8_t> rep;
    if (p2 && l2 > 0) { rep.resize(size_t(l2)); read(p2, rep.data(), rep.size()); }

    std::vector<uint8_t> out;
    out.reserve(cur.size() - mlen + rep.size());
    out.insert(out.end(), cur.begin(), cur.begin() + pos);
    out.insert(out.end(), rep.begin(), rep.end());
    out.insert(out.end(), cur.begin() + pos + mlen, cur.end());

    if (setHandleSize(h, uint32_t(out.size())) != kNoErr) { setGpr(3, 0xFFFFFFFFu); return; }
    write(handles_[h].ptr, out.data(), out.size());
    setGpr(3, uint32_t(pos + rep.size()));
}

// ---------------------------------------------------- Resource Manager -----

void MacRuntime::impOpenComponentResFile() {
    for (auto& r : openRes_)
        if (r.pack == component_) { curRes_ = r.refNum; setGpr(3, uint32_t(int32_t(r.refNum))); return; }
    openRes_.push_back({nextRefNum_++, component_, {}});
    curRes_ = openRes_.back().refNum;
    setGpr(3, uint32_t(int32_t(curRes_)));
}
void MacRuntime::impCloseComponentResFile() { setGpr(3, 0); }
void MacRuntime::impCloseResFile() {}
void MacRuntime::impUseResFile() { curRes_ = int16_t(gpr(3)); }
void MacRuntime::impCurResFile() { setGpr(3, uint32_t(int32_t(curRes_))); }

void MacRuntime::impHOpenResFile() {
    uint32_t namePtr = gpr(5);
    uint8_t len = rd8(namePtr);
    std::string name(len, 0);
    if (len) read(namePtr + 1, &name[0], len);
    auto it = voicePacks_.find(name);
    if (it == voicePacks_.end()) { setGpr(3, 0xFFFFFFFFu); return; }
    for (auto& r : openRes_)
        if (r.pack == it->second) { curRes_ = r.refNum; setGpr(3, uint32_t(int32_t(r.refNum))); return; }
    openRes_.push_back({nextRefNum_++, it->second, {}});
    curRes_ = openRes_.back().refNum;
    setGpr(3, uint32_t(int32_t(curRes_)));
}

void MacRuntime::impGetResource() {
    uint32_t type = gpr(3);
    int32_t id = int16_t(gpr(4) & 0xFFFF);
    // current resource file first, then the rest, as the Resource Manager does
    std::vector<OpenRes*> order;
    for (auto& r : openRes_) if (r.refNum == curRes_) order.push_back(&r);
    for (auto& r : openRes_) if (r.refNum != curRes_) order.push_back(&r);
    for (OpenRes* r : order) {
        const std::vector<uint8_t>* d = r->pack->find(type, id);
        if (!d) continue;
        ResKey k{type, id};
        auto ld = r->loaded.find(k);
        if (ld != r->loaded.end()) { setGpr(3, ld->second); return; }
        uint32_t h = newHandle(uint32_t(d->size()), false);
        if (!h) { setGpr(3, 0); return; }
        write(handles_[h].ptr, d->data(), d->size());
        r->loaded[k] = h;
        setGpr(3, h);
        return;
    }
    setGpr(3, 0);
}

void MacRuntime::impReleaseResource() {
    uint32_t h = gpr(3);
    for (auto& r : openRes_)
        for (auto it = r.loaded.begin(); it != r.loaded.end(); ++it)
            if (it->second == h) { r.loaded.erase(it); return; }
}
void MacRuntime::impDetachResource() { impReleaseResource(); }

// ------------------------------------------------------- Sound Manager -----

void MacRuntime::impSndSoundManagerVersion() { setGpr(3, 0x03050000); }

void MacRuntime::impSndNewChannel() {
    uint32_t pchan = gpr(3);
    uint32_t ch = alloc(0x100);
    std::vector<uint8_t> zeros(0x100, 0);
    write(ch, zeros.data(), zeros.size());
    wr32(pchan, ch);
    setGpr(3, 0);
}
void MacRuntime::impSndDisposeChannel() { setGpr(3, 0); }
void MacRuntime::impSndDoImmediate()    { setGpr(3, 0); }
void MacRuntime::impSndChannelStatus() {
    uint32_t size = gpr(4), p = gpr(5);
    std::vector<uint8_t> zeros(size, 0);
    write(p, zeros.data(), zeros.size());
    setGpr(3, 0);
}
void MacRuntime::impSndPlayDoubleBuffer() {
    uint32_t hdr = gpr(4);
    dbl_.active = true;
    dbl_.channel = gpr(3);
    dbl_.header = hdr;
    dbl_.channels   = int16_t(rd16(hdr + 0));
    dbl_.sampleSize = int16_t(rd16(hdr + 2));
    dbl_.sampleRate = rd32(hdr + 8) >> 16;      // UnsignedFixed
    dbl_.buffers[0] = rd32(hdr + 12);
    dbl_.buffers[1] = rd32(hdr + 16);
    dbl_.doubleBackProc = rd32(hdr + 20);
    dbBytesPerFrame_ = uint32_t(dbl_.channels) * uint32_t(dbl_.sampleSize / 8);
    if (dbBytesPerFrame_ == 0) dbBytesPerFrame_ = 2;

    // Watch stores into the sound data so a callback can be placed within the
    // buffer it interrupts.  The range covers both buffers; hookBufWrite
    // ignores anything outside the one currently being filled.
    if (!dbHookInstalled_ && dbl_.buffers[0] && dbl_.buffers[1]) {
        uint32_t lo = dbl_.buffers[0] < dbl_.buffers[1] ? dbl_.buffers[0] : dbl_.buffers[1];
        uint32_t hi = dbl_.buffers[0] < dbl_.buffers[1] ? dbl_.buffers[1] : dbl_.buffers[0];
        uint32_t span = hi - lo;
        if (span > 0x40000) span = 0x40000;
        uc_hook h;
        if (uc_hook_add(uc_, &h, UC_HOOK_MEM_WRITE,
                        (void*)&MacRuntime::hookBufWriteTramp, this,
                        lo, uint64_t(hi) + span) == UC_ERR_OK)
            dbHookInstalled_ = true;
    }
    beginBufferFill(dbl_.buffers[0]);
    setGpr(3, 0);
}

void MacRuntime::beginBufferFill(uint32_t buffer) {
    dbFillBase_ = buffer + kDbHeaderSize;
    // The engine never writes more than one buffer's worth; the far buffer
    // bounds this one, and 256 KB caps it when they are not adjacent.
    uint32_t other = buffer == dbl_.buffers[0] ? dbl_.buffers[1] : dbl_.buffers[0];
    uint32_t limit = (other > buffer && other - buffer < 0x40000) ? other : buffer + 0x40000;
    dbFillLimit_ = limit;
    dbWriteFrames_ = 0;
}

void MacRuntime::hookBufWriteTramp(uc_engine*, uc_mem_type, uint64_t addr, int size,
                                   int64_t, void* user) {
    static_cast<MacRuntime*>(user)->hookBufWrite(addr, size);
}

void MacRuntime::hookBufWrite(uint64_t addr, int size) {
    if (!dbFillBase_ || addr < dbFillBase_ || addr >= dbFillLimit_) return;
    uint32_t end = uint32_t(addr - dbFillBase_) + uint32_t(size > 0 ? size : 0);
    uint32_t frames = (end + dbBytesPerFrame_ - 1) / dbBytesPerFrame_;
    if (frames > dbWriteFrames_) dbWriteFrames_ = frames;
}

// ---------------------------------------------------------- Mixed Mode -----

void MacRuntime::impNewRoutineDescriptor() {
    uint32_t proc = gpr(3), procInfo = gpr(4);
    uint32_t rd = alloc(32);
    std::vector<uint8_t> b(32, 0);
    b[0] = 0xAA; b[1] = 0xFE; b[2] = 7; b[3] = 0;
    b[12] = uint8_t(procInfo >> 24); b[13] = uint8_t(procInfo >> 16);
    b[14] = uint8_t(procInfo >> 8);  b[15] = uint8_t(procInfo);
    b[17] = 1;                                        // kPowerPCISA
    b[20] = uint8_t(proc >> 24); b[21] = uint8_t(proc >> 16);
    b[22] = uint8_t(proc >> 8);  b[23] = uint8_t(proc);
    write(rd, b.data(), b.size());
    upps_[rd] = proc;
    setGpr(3, rd);
}
void MacRuntime::impDisposeRoutineDescriptor() { upps_.erase(gpr(3)); }

void MacRuntime::impCallUniversalProc() {
    uint32_t upp = gpr(3);
    uint16_t magic = 0;
    if (upp >= kCodeBase) magic = rd16(upp);
    if (magic != 0xAAFE) {
        // A Speech Manager client callback: record it and return 0.
        callbacks_.push_back({upp, {gpr(5), gpr(6), gpr(7), gpr(8)}, dbWriteFrames_});
        setGpr(3, 0);
        return;
    }
    if (rd8(upp + 17) != 1) {
        faulted_ = true;
        lastError_ = "68k routine descriptor is not supported";
        uc_emu_stop(uc_);
        return;
    }
    uint32_t tvec = rd32(upp + 20);
    uint32_t code = rd32(tvec), toc = rd32(tvec + 4);
    uint32_t args[8];
    for (int i = 0; i < 8; ++i) args[i] = gpr(5 + i);
    for (int i = 0; i < 8; ++i) setGpr(3 + i, args[i]);
    // Unicorn cannot nest emu_start, so stop here and resume at the target with
    // LR still holding the original return address.
    redirectPending_ = true;
    redirectPc_ = code;
    redirectToc_ = toc;
    uc_emu_stop(uc_);
}

void MacRuntime::impSetA5() { setGpr(3, 0); }

// --------------------------------------------------- Component Manager -----

void MacRuntime::impSetComponentInstanceStorage() { storage_ = gpr(4); }
void MacRuntime::impCountComponentInstances() { setGpr(3, 1); }

// -------------------------------------------- Time / Notification / misc ---

void MacRuntime::impInsXTime() {
    uint32_t t = gpr(3);
    if (std::find(timerTasks_.begin(), timerTasks_.end(), t) == timerTasks_.end())
        timerTasks_.push_back(t);
}
void MacRuntime::impPrimeTime() {
    uint32_t t = gpr(3);
    if (std::find(timerTasks_.begin(), timerTasks_.end(), t) == timerTasks_.end())
        timerTasks_.push_back(t);
}
void MacRuntime::impRmvTime() {
    timerTasks_.erase(std::remove(timerTasks_.begin(), timerTasks_.end(), gpr(3)),
                      timerTasks_.end());
}
void MacRuntime::impDTInstall() {
    deferredTasks_.push_back(gpr(3));
    setGpr(3, 0);
}
void MacRuntime::impMicroseconds() {
    micros_ += 1000;
    wr32(gpr(3), uint32_t(micros_ >> 32));
    wr32(gpr(3) + 4, uint32_t(micros_));
}
// The demo build used the Notification Manager for its nag; the engine is
// patched not to reach it, and a stub keeps any stray call harmless.
void MacRuntime::impNMInstall() { setGpr(3, 0); }
void MacRuntime::impNMRemove()  { setGpr(3, 0); }

void MacRuntime::impGestalt() {
    uint32_t sel = gpr(3), out = gpr(4);
    uint32_t v = 0;
    bool known = true;
    switch (sel) {
        case FourCC("snd "): v = 0x0000FFFF; break;
        case FourCC("proc"): v = 3; break;
        case FourCC("tmgr"): v = 3; break;
        case FourCC("nmgr"): v = 1; break;
        case FourCC("sysv"): v = 0x0900; break;
        case FourCC("ram "): v = 64u * 1024 * 1024; break;
        default: known = false; break;
    }
    if (!known) { setGpr(3, uint32_t(int32_t(-5551))); return; }
    wr32(out, v);
    setGpr(3, 0);
}

void MacRuntime::impNumToString() {
    int32_t n = int32_t(gpr(3));
    uint32_t out = gpr(4);
    char buf[16];
    int len = snprintf(buf, sizeof buf, "%d", n);
    wr8(out, uint8_t(len));
    write(out + 1, buf, size_t(len));
}

void MacRuntime::impFixDiv() {
    int32_t a = int32_t(gpr(3)), b = int32_t(gpr(4));
    if (b == 0) { setGpr(3, 0x7FFFFFFF); return; }
    double r = (double(a) / double(b)) * 65536.0;
    setGpr(3, uint32_t(int32_t(r)));
}
void MacRuntime::impFixMul() {
    int64_t r = (int64_t(int32_t(gpr(3))) * int64_t(int32_t(gpr(4)))) >> 16;
    setGpr(3, uint32_t(int32_t(r)));
}
void MacRuntime::impFix2Long() {
    int32_t v = int32_t(gpr(3));
    setGpr(3, uint32_t(int32_t(std::lround(double(v) / 65536.0))));
}
void MacRuntime::impLong2Fix() { setGpr(3, uint32_t(int32_t(gpr(3)) << 16)); }

void MacRuntime::impMath1(double (*fn)(double)) { setFpr(1, fn(fpr(1))); }
void MacRuntime::impPow() { setFpr(1, ::pow(fpr(1), fpr(2))); }

}  // namespace ppc
