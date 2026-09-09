// Just enough classic Mac OS for the Infovox 210 'ttsc' component.
//
// The component imports 56 symbols from InterfaceLib, MathLib and SpeechLib.
// This runtime implements all of them on top of a Unicorn PowerPC CPU: the
// Memory Manager (handles), the Resource Manager (over .ivp packs), the Sound
// Manager double-buffer path (which is how PCM leaves the engine), the Time and
// Notification Managers, the Component Manager and Mixed Mode.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <unicorn/unicorn.h>

#include "ivpack.h"
#include "pef.h"

namespace ppc {

// Emulated address space.
constexpr uint32_t kCodeBase = 0x10000000;
constexpr uint32_t kDataBase = 0x20000000;
constexpr uint32_t kImpTVec  = 0x30000000;   // 8 bytes per import
constexpr uint32_t kImpCode  = 0x31000000;   // 4 bytes per import (trap stubs)
constexpr uint32_t kHeapBase = 0x40000000;
constexpr uint32_t kHeapSize = 0x00800000;   // 8 MB (all 11 languages peak at ~0.6 MB)
constexpr uint32_t kMptrBase = 0x48000000;   // master pointers
constexpr uint32_t kMptrSize = 0x00100000;
constexpr uint32_t kStackTop = 0x50100000;
constexpr uint32_t kStackSize= 0x00100000;
constexpr uint32_t kMagicRet = 0x5FFF0000;

constexpr int16_t kNoErr = 0;
constexpr int16_t kMemFullErr = -108;
constexpr int16_t kResNotFound = -192;

// Double-buffer state captured from SndPlayDoubleBuffer.
struct DoubleBuffer {
    bool     active = false;
    uint32_t channel = 0;
    uint32_t header = 0;
    int16_t  channels = 1;
    int16_t  sampleSize = 16;
    uint32_t sampleRate = 22050;   // whole Hz
    uint32_t buffers[2] = {0, 0};
    uint32_t doubleBackProc = 0;
};

class MacRuntime {
public:
    MacRuntime();
    ~MacRuntime();

    bool init(const ResourcePack* component, std::string* err);

    // Register a voice file so HOpenResFile can find it by name.
    void addVoicePack(const std::string& fileName, const ResourcePack* pack);

    // --- guest memory -----------------------------------------------------
    uint32_t rd32(uint32_t a) const;
    uint16_t rd16(uint32_t a) const;
    uint8_t  rd8(uint32_t a) const;
    void     wr32(uint32_t a, uint32_t v);
    void     wr16(uint32_t a, uint16_t v);
    void     wr8(uint32_t a, uint8_t v);
    void     read(uint32_t a, void* dst, size_t n) const;
    void     write(uint32_t a, const void* src, size_t n);

    // --- heap -------------------------------------------------------------
    uint32_t alloc(uint32_t size);
    uint32_t newHandle(uint32_t size, bool clear);
    void     disposeHandle(uint32_t h);
    int16_t  setHandleSize(uint32_t h, uint32_t size);
    uint32_t handleSize(uint32_t h) const;
    uint32_t heapUsed() const { return heapNext_ - kHeapBase; }
    uint32_t deref(uint32_t h) const { return rd32(h); }

    // --- execution --------------------------------------------------------
    // Calls a PowerPC routine with the CFM ABI; returns r3.
    int32_t call(uint32_t codeAddr, uint32_t toc, const std::vector<uint32_t>& args);
    // Calls a UPP (RoutineDescriptor) built by NewRoutineDescriptor.
    int32_t callUpp(uint32_t upp, const std::vector<uint32_t>& args);

    uint32_t entryCode() const { return pef_.entryCode(); }
    uint32_t entryToc()  const { return pef_.entryToc(); }
    uint32_t storage() const { return storage_; }
    void     setStorage(uint32_t s) { storage_ = s; }

    const DoubleBuffer& doubleBuffer() const { return dbl_; }
    void clearDoubleBuffer() { dbl_.active = false; }

    // Deferred tasks and Time Manager tasks queued by the engine at interrupt
    // time; the render loop runs them between buffers.
    void runPendingTasks();

    // Client Speech Manager callbacks arrive as sentinel "UPP" values.
    struct Callback { uint32_t upp; uint32_t args[4]; };
    std::vector<Callback>& callbacks() { return callbacks_; }

    // SpeechLib::GetVoiceInfo(VoiceSpec*, OSType selector, void* info).  The
    // component calls back into the Speech Manager to locate a voice file; the
    // engine driver answers from the installed .ivp packs.
    using VoiceInfoFn = std::function<int32_t(uint32_t voiceSpec, uint32_t selector,
                                             uint32_t info)>;
    void setVoiceInfoHandler(VoiceInfoFn fn) { voiceInfo_ = std::move(fn); }

    const std::string& lastError() const { return lastError_; }
    bool  faulted() const { return faulted_; }

private:
    static void hookImportTramp(uc_engine*, uint64_t addr, uint32_t size, void* user);
    void hookImport(uint64_t addr);
    void run(uint32_t pc);

    // import implementations
    void impNewHandleClear();  void impDisposeHandle();  void impGetHandleSize();
    void impSetHandleSize();   void impMemError();       void impHLock();
    void impHUnlock();         void impHLockHi();        void impHPurge();
    void impHGetState();       void impHSetState();      void impRecoverHandle();
    void impBlockMove();       void impMunger();
    void impOpenComponentResFile(); void impCloseComponentResFile();
    void impCloseResFile();    void impUseResFile();     void impCurResFile();
    void impHOpenResFile();    void impGetResource();    void impReleaseResource();
    void impDetachResource();
    void impSndSoundManagerVersion(); void impSndNewChannel();
    void impSndDisposeChannel(); void impSndDoImmediate(); void impSndChannelStatus();
    void impSndPlayDoubleBuffer();
    void impNewRoutineDescriptor(); void impDisposeRoutineDescriptor();
    void impCallUniversalProc(); void impSetA5();
    void impSetComponentInstanceStorage(); void impCountComponentInstances();
    void impInsXTime();  void impPrimeTime();  void impRmvTime();  void impDTInstall();
    void impMicroseconds();
    void impNMInstall(); void impNMRemove();
    void impGestalt();   void impNumToString();
    void impFixDiv();    void impFixMul();  void impFix2Long();  void impLong2Fix();
    void impMath1(double (*fn)(double));
    void impPow();

    uint32_t gpr(int n) const;
    void     setGpr(int n, uint32_t v);
    double   fpr(int n) const;
    void     setFpr(int n, double v);

    struct HandleInfo { uint32_t size; uint32_t ptr; uint8_t flags; };
    struct FreeBlock { uint32_t addr; uint32_t size; };
    struct OpenRes { int16_t refNum; const ResourcePack* pack;
                     std::map<ResKey, uint32_t> loaded; };

    uc_engine* uc_ = nullptr;
    Pef pef_;
    const ResourcePack* component_ = nullptr;
    std::map<std::string, const ResourcePack*> voicePacks_;

    std::vector<std::string> importNames_;
    std::vector<void (MacRuntime::*)()> importFns_;

    uint32_t heapNext_ = kHeapBase;
    uint32_t mptrNext_ = kMptrBase;
    std::vector<FreeBlock> freeList_;
    std::map<uint32_t, HandleInfo> handles_;
    std::map<uint32_t, uint32_t> ptrToHandle_;
    int16_t memErr_ = kNoErr;

    std::vector<OpenRes> openRes_;
    int16_t nextRefNum_ = 100;
    int16_t curRes_ = 0;

    std::map<uint32_t, uint32_t> upps_;        // descriptor -> tvector
    std::vector<uint32_t> deferredTasks_;
    std::vector<uint32_t> timerTasks_;
    std::vector<Callback> callbacks_;

    VoiceInfoFn voiceInfo_;
    DoubleBuffer dbl_;
    uint32_t storage_ = 0;
    uint64_t micros_ = 0;

    bool     redirectPending_ = false;
    uint32_t redirectPc_ = 0, redirectToc_ = 0;
    bool     faulted_ = false;
    std::string lastError_;
};

}  // namespace ppc
