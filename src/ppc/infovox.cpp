#include "infovox.h"

#include <algorithm>
#include <cstring>
#include <map>

#include <windows.h>

namespace ppc {
namespace {

// Component Manager selectors implemented by the Infovox 'ttsc' component.
constexpr int16_t kOpen = -1, kClose = -2;
constexpr int16_t kSpeak = 1, kStop = 4, kGetInfo = 5, kSetInfo = 6;

// Speech Manager info selectors.
constexpr uint32_t soRate       = FourCC("rate");
constexpr uint32_t soPitchBase  = FourCC("pbas");
constexpr uint32_t soPitchMod   = FourCC("pmod");
constexpr uint32_t soCurrentVoice = FourCC("cvox");
constexpr uint32_t soSynthExtension = FourCC("xtnd");
constexpr uint32_t soPhonemeSymbols = FourCC("phsy");
constexpr uint32_t soCharacterMode = FourCC("char");
constexpr uint32_t soNumberMode = FourCC("nmbr");
constexpr uint32_t soSpeechDoneCb = FourCC("sdcb");
constexpr uint32_t soWordCb     = FourCC("wdcb");
constexpr uint32_t soPhonemeCb  = FourCC("phcb");
constexpr uint32_t soErrorCb    = FourCC("ercb");
constexpr uint32_t soSyncCb     = FourCC("sycb");
constexpr uint32_t kInVx        = FourCC("InVx");
constexpr uint32_t kAspi        = FourCC("aspi");

// Sentinel callback pointers: the component invokes these through
// CallUniversalProc, where the runtime recognises them as client callbacks.
constexpr uint32_t kCbSpeechDone = 0xDEAD0002;
constexpr uint32_t kCbSync       = 0xDEAD0003;
constexpr uint32_t kCbWord       = 0xDEAD0004;
constexpr uint32_t kCbPhoneme    = 0xDEAD0005;
constexpr uint32_t kCbError      = 0xDEAD0006;

constexpr uint32_t dbBufferReady = 1, dbLastBuffer = 4;

// Which phonemes need frication synthesised for them, by the symbol the
// language pack itself gives them (the 'ttss' table).  Infovox used one
// notation across all eleven languages -- S, SH, F, TH, H and so on, with
// lowercase digraphs in the two English packs -- so almost all of this is
// language-independent.  `pack` settles the one symbol that is not: CH is the
// German ach-Laut but the Spanish /tS/.
//
// Anything unlisted is Voiced, which synthesises nothing, so a language whose
// symbol is not recognised is left exactly as the engine rendered it.
Fric fricationFor(const std::string& sym, const std::string& pack) {
    struct Entry { const char* sym; Fric kind; };
    static const Entry kMap[] = {
        // English packs use lowercase digraphs and uppercase singles.
        {"S", Fric::S},      {"S1", Fric::S},     {"ts", Fric::S},
        {"Z", Fric::Z},
        {"sh", Fric::Sh},    {"SH", Fric::Sh},    {"2S", Fric::Sh},
        {"SJ", Fric::Sh},    {"TJ", Fric::Sh},    {"ch", Fric::Sh},
        {"zh", Fric::Zh},    {"ZH", Fric::Zh},    {"jh", Fric::Zh},
        {"F", Fric::F},
        {"V", Fric::V},
        {"th", Fric::Th},    {"TH", Fric::Th},
        {"dh", Fric::Dh},    {"DH", Fric::Dh},
        {"hh", Fric::H},     {"H", Fric::H},
        {"X", Fric::X},      {"KJ", Fric::X},     {"GH", Fric::X},
        {"P", Fric::Burst},  {"T", Fric::Burst},  {"K", Fric::Burst},
        {"2T", Fric::Burst}, {"KH", Fric::Burst},
    };
    if (sym.empty()) return Fric::Voiced;
    if (sym == "CH") return pack == "spanish" ? Fric::Sh : Fric::X;
    for (const auto& e : kMap)
        if (sym == e.sym) return e.kind;
    return Fric::Voiced;
}

inline int32_t toFixed(double v) { return int32_t(v * 65536.0 + (v < 0 ? -0.5 : 0.5)); }

// 0..100 slider -> [lo,hi] engine units.
inline double scale(int slider, double lo, double hi) {
    if (slider < 0) slider = 0;
    if (slider > 100) slider = 100;
    return lo + (hi - lo) * (double(slider) / 100.0);
}

std::string fourCc(uint32_t v) {
    char s[5] = {char(v >> 24), char(v >> 16), char(v >> 8), char(v), 0};
    return std::string(s);
}

inline uint32_t rdBE32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | p[3];
}
inline int16_t rdBE16(const uint8_t* p) {
    return int16_t((uint32_t(p[0]) << 8) | p[1]);
}
std::string pascalString(const uint8_t* p, size_t cap) {
    size_t n = p[0];
    if (n + 1 > cap) n = cap ? cap - 1 : 0;
    return std::string(reinterpret_cast<const char*>(p + 1), n);
}

}  // namespace

InfovoxEngine::InfovoxEngine() = default;
InfovoxEngine::~InfovoxEngine() = default;

bool InfovoxEngine::init(const std::wstring& engineDir, std::string* err) {
    std::wstring dir = engineDir;
    if (!dir.empty() && dir.back() != L'\\') dir.push_back(L'\\');

    if (!component_.loadFile(dir + L"infovox210.ivp", err)) return false;

    mac_.reset(new MacRuntime());
    if (!mac_->init(&component_, err)) return false;
    mac_->setVoiceInfoHandler([this](uint32_t vs, uint32_t sel, uint32_t info) {
        return handleGetVoiceInfo(vs, sel, info);
    });

    // Every other .ivp in the directory is a language pack.
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.ivp").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring fn = fd.cFileName;
            if (_wcsicmp(fn.c_str(), L"infovox210.ivp") == 0) continue;
            auto pack = std::unique_ptr<ResourcePack>(new ResourcePack());
            std::string perr;
            if (!pack->loadFile(dir + fn, &perr)) continue;
            std::string slug;
            for (wchar_t c : fn) slug.push_back(char(c & 0x7F));
            size_t dot = slug.rfind('.');
            if (dot != std::string::npos) slug.erase(dot);

            // Mac file name the component will pass to HOpenResFile.
            std::string macName = pack->name(FourCC("STR "), -16397);
            if (macName.empty()) macName = slug;
            macName = slug;   // we choose it ourselves in handleGetVoiceInfo

            int packIdx = int(packs_.size());
            for (const ResKey& k : pack->keys()) {
                if (k.type != FourCC("ttvd")) continue;
                const std::vector<uint8_t>* d = pack->find(k.type, k.id);
                if (!d || d->size() < 362) continue;
                InfovoxVoice v;
                v.id      = fourCc(rdBE32(d->data() + 8));
                v.version = int(rdBE32(d->data() + 12));
                v.name    = pascalString(d->data() + 16, 64);
                v.comment = pascalString(d->data() + 80, 256);
                v.gender  = rdBE16(d->data() + 336);
                v.age     = rdBE16(d->data() + 338);
                v.language= rdBE16(d->data() + 342);
                v.region  = rdBE16(d->data() + 344);
                v.pack    = slug;
                v.variant = v.id.size() == 4 ? (v.id[3] - '0') : 1;
                voices_.push_back(v);
                voicePackIndex_.push_back(packIdx);
            }
            mac_->addVoicePack(macName, pack.get());
            packNames_.push_back(slug);
            packFileNames_.push_back(macName);
            packs_.push_back(std::move(pack));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (voices_.empty()) { if (err) *err = "no Infovox language packs found"; return false; }
    std::sort(voices_.begin(), voices_.end(),
              [](const InfovoxVoice& a, const InfovoxVoice& b) { return a.id < b.id; });

    // Open the component.
    scratch_ = mac_->alloc(0x1000);
    textBuf_ = mac_->alloc(0x8000);
    int32_t r = component(kOpen, {instance_});
    if (r != 0) {
        if (err) *err = "component Open failed (" + std::to_string(r) + ")";
        return false;
    }
    if (mac_->faulted()) { if (err) *err = mac_->lastError(); return false; }
    opened_ = true;

    // Install the callbacks we care about (they are set by value, not pointer).
    for (uint32_t sel : {soSpeechDoneCb, soWordCb, soPhonemeCb, soErrorCb, soSyncCb}) {
        uint32_t sentinel = sel == soSpeechDoneCb ? kCbSpeechDone
                          : sel == soWordCb       ? kCbWord
                          : sel == soPhonemeCb    ? kCbPhoneme
                          : sel == soErrorCb      ? kCbError : kCbSync;
        component(kSetInfo, {sel, sentinel});
    }
    return true;
}

const InfovoxVoice* InfovoxEngine::findVoice(const std::string& id) const {
    for (const auto& v : voices_) if (v.id == id) return &v;
    return nullptr;
}

int32_t InfovoxEngine::component(int16_t selector, const std::vector<uint32_t>& args) {
    // ComponentParameters { UInt8 flags; UInt8 paramSize; SInt16 what; long params[] }
    // params[0] holds the LAST declared argument.
    uint8_t hdr[4] = {0, uint8_t(args.size() * 4),
                      uint8_t(uint16_t(selector) >> 8), uint8_t(uint16_t(selector))};
    mac_->write(scratch_, hdr, 4);
    for (size_t i = 0; i < args.size(); ++i)
        mac_->wr32(scratch_ + 4 + uint32_t(i) * 4, args[args.size() - 1 - i]);
    return mac_->call(mac_->entryCode(), mac_->entryToc(), {scratch_, mac_->storage()});
}

int32_t InfovoxEngine::handleGetVoiceInfo(uint32_t voiceSpec, uint32_t selector,
                                          uint32_t info) {
    uint32_t vid = mac_->rd32(voiceSpec + 4);
    std::string id = fourCc(vid);
    int idx = -1;
    for (size_t i = 0; i < voices_.size(); ++i)
        if (voices_[i].id == id) { idx = int(i); break; }
    if (idx < 0) return -244;                       // voiceNotFound
    int packIdx = voicePackIndex_[idx];

    if (selector == FourCC("fref")) {
        // VoiceFileInfo { FSSpec fileSpec; short resID; }
        const std::string& nm = packFileNames_[packIdx];
        std::vector<uint8_t> fs(72, 0);
        fs[0] = 0xFF; fs[1] = 0xFF;                 // vRefNum = -1
        fs[5] = 2;                                  // parID = 2
        fs[6] = uint8_t(std::min<size_t>(nm.size(), 63));
        memcpy(fs.data() + 7, nm.data(), fs[6]);
        // resID: the component only needs it to be the voice's own resource.
        int16_t resId = 0;
        const ResourcePack* pack = packs_[packIdx].get();
        for (const ResKey& k : pack->keys()) {
            if (k.type != FourCC("ttvd")) continue;
            const std::vector<uint8_t>* d = pack->find(k.type, k.id);
            if (d && d->size() >= 12 && rdBE32(d->data() + 8) == vid) {
                resId = int16_t(k.id);
                break;
            }
        }
        fs[70] = uint8_t(uint16_t(resId) >> 8);
        fs[71] = uint8_t(uint16_t(resId));
        mac_->write(info, fs.data(), fs.size());
        return 0;
    }
    if (selector == FourCC("info")) {
        const ResourcePack* pack = packs_[packIdx].get();
        for (const ResKey& k : pack->keys()) {
            if (k.type != FourCC("ttvd")) continue;
            const std::vector<uint8_t>* d = pack->find(k.type, k.id);
            if (d && d->size() >= 12 && rdBE32(d->data() + 8) == vid) {
                mac_->write(info, d->data(), d->size());
                return 0;
            }
        }
    }
    return -244;
}

bool InfovoxEngine::setVoice(const std::string& id, std::string* err) {
    const InfovoxVoice* v = findVoice(id);
    if (!v) { if (err) *err = "unknown voice " + id; return false; }
    uint32_t vs = scratch_ + 0x800;
    mac_->wr32(vs, kInVx);
    uint32_t code = 0;
    for (int i = 0; i < 4 && i < int(id.size()); ++i)
        code = (code << 8) | uint8_t(id[i]);
    mac_->wr32(vs + 4, code);
    int32_t r = component(kSetInfo, {soCurrentVoice, vs});
    if (r != 0) {
        if (err) *err = "voice " + id + " rejected (" + std::to_string(r) + ")";
        return false;
    }
    currentVoice_ = id;
    currentPack_ = v->pack;
    loadPhonemeTable();
    return applyParams(err);
}

void InfovoxEngine::loadPhonemeTable() {
    phonemes_.clear();
    uint32_t buf = scratch_ + 0x900;
    mac_->wr32(buf, 0);
    if (component(kGetInfo, {soPhonemeSymbols, buf}) != 0) return;
    uint32_t h = mac_->rd32(buf);
    if (!h) return;
    uint32_t p = mac_->deref(h);
    if (!p) return;
    int16_t count = int16_t(mac_->rd16(p));
    if (count <= 0 || count > 512) return;
    phonemes_.resize(size_t(count) + 1);
    for (int i = 0; i < count; ++i) {
        uint32_t rec = p + 2 + uint32_t(i) * 54;
        int16_t opcode = int16_t(mac_->rd16(rec));
        uint8_t len = mac_->rd8(rec + 2);
        std::string sym(len, 0);
        if (len) mac_->read(rec + 3, &sym[0], len);
        if (opcode >= 0 && size_t(opcode) < phonemes_.size()) phonemes_[opcode] = sym;
    }
}

bool InfovoxEngine::applyParams(std::string* err) {
    uint32_t p = scratch_ + 0x820;
    auto setFixed = [&](uint32_t sel, double value) {
        mac_->wr32(p, uint32_t(toFixed(value)));
        return component(kSetInfo, {sel, p});
    };
    // 0..100 sliders span the engine's own limits, so the extremes of a screen
    // reader's slider reach the extremes of the synthesizer.
    setFixed(soRate,      scale(params_.rate, 0.0, 999.0));
    setFixed(soPitchBase, scale(params_.pitch, 0.0, 100.0));
    setFixed(soPitchMod,  scale(params_.pitchMod, 0.0, 100.0));

    // Breathiness is the one Infovox-private parameter: SpeechXtndData with
    // creator 'InVx' and selector 'aspi', value as Fixed 0..100.
    uint32_t x = scratch_ + 0x840;
    mac_->wr32(x, kInVx);
    mac_->wr32(x + 4, kAspi);
    mac_->wr32(x + 8, uint32_t(toFixed(scale(params_.breath, 0.0, 100.0))));
    component(kSetInfo, {soSynthExtension, x});
    (void)err;
    return true;
}

void InfovoxEngine::stopChannel() { component(kStop, {}); }

bool InfovoxEngine::applyModes() {
    if (params_.spellOut == curSpellOut_ &&
        params_.literalNumbers == curLiteralNumbers_)
        return true;
    // soCharacterMode and soNumberMode are refused with synthNotReady (-242)
    // unless the channel is idle, so stop it before switching.
    stopChannel();
    uint32_t p = scratch_ + 0x860;
    const uint32_t kNorm = FourCC("NORM"), kLtrl = FourCC("LTRL");
    mac_->wr32(p, params_.spellOut ? kLtrl : kNorm);
    if (component(kSetInfo, {soCharacterMode, p}) == 0) curSpellOut_ = params_.spellOut;
    mac_->wr32(p, params_.literalNumbers ? kLtrl : kNorm);
    if (component(kSetInfo, {soNumberMode, p}) == 0)
        curLiteralNumbers_ = params_.literalNumbers;
    return true;
}

bool InfovoxEngine::render(const std::string& text, const PcmSink& sink,
                           const CancelFn& cancelled, std::string* err) {
    if (!opened_) { if (err) *err = "engine not open"; return false; }
    events_.clear();
    // Leave the channel idle before every utterance: the previous one ends with
    // the engine still marked busy, which makes mode changes fail.
    stopChannel();
    applyModes();
    if (!applyParams(err)) return false;

    // The engine consumes input until a NUL, not until the byte count, so the
    // buffer must be terminated.
    std::string body = text;
    if (body.size() > 0x7F00) body.resize(0x7F00);
    std::vector<uint8_t> buf(body.begin(), body.end());
    buf.insert(buf.end(), 64, 0);
    mac_->write(textBuf_, buf.data(), buf.size());

    mac_->clearDoubleBuffer();
    mac_->callbacks().clear();
    int32_t r = component(kSpeak, {textBuf_, uint32_t(body.size()), 0});
    if (mac_->faulted()) { if (err) *err = mac_->lastError(); return false; }
    if (r != 0) { if (err) *err = "Speak failed (" + std::to_string(r) + ")"; return false; }

    const DoubleBuffer& d = mac_->doubleBuffer();
    if (!d.active) return true;                      // nothing to say
    sampleRate_ = d.sampleRate;
    const uint32_t bytesPerFrame = uint32_t(d.channels) * uint32_t(d.sampleSize / 8);
    if (bytesPerFrame == 0) { if (err) *err = "bad audio format"; return false; }

    const double gain = double(params_.volume) / 100.0;
    clarity_.configure(double(sampleRate_), params_.clarity);
    std::vector<uint8_t> raw;
    std::vector<int16_t> pcm;
    uint64_t produced = 0;
    int which = 0;

    // Callbacks fire while the engine is filling a buffer, and that buffer is
    // not emitted until two turns later, so they are held against the buffer
    // they came from and resolved to a frame offset when it goes out.  The
    // callbacks left over from Speak belong to whatever it pre-filled, which is
    // the start of the utterance.
    struct Pending { uint32_t writeFrames; bool word; int32_t a, b; };
    std::map<uint32_t, std::vector<Pending>> pending;
    auto collect = [&](uint32_t buffer) {
        auto& v = pending[buffer];
        for (const auto& cb : mac_->callbacks()) {
            if (cb.upp == kCbWord)
                v.push_back({cb.writeFrames, true, int32_t(cb.args[2]), int32_t(cb.args[3])});
            else if (cb.upp == kCbPhoneme)
                v.push_back({cb.writeFrames, false, int32_t(cb.args[2]), 0});
        }
        mac_->callbacks().clear();
    };
    collect(d.buffers[0]);

    // A screen reader abandons an utterance on every keypress.  Stopping dead
    // leaves the waveform hanging wherever it happened to be -- measured at a
    // buffer edge that is a mean of 1400 counts and can reach 11000 -- and the
    // step to silence is heard as a click.  So the last thing sent after a
    // cancel is this much of the audio that would have followed, ramped down.
    const size_t fadeFrames = size_t(sampleRate_) * 5 / 1000;   // 5 ms

    for (int guard = 0; guard < 20000; ++guard) {
        const bool stopping = cancelled && cancelled();

        uint32_t b = d.buffers[which];
        uint32_t frames = mac_->rd32(b);
        uint32_t flags = mac_->rd32(b + 4);

        if (frames) {
            // Resolve this buffer's callbacks against where in it they fired,
            // then hand the phonemes to the clarity stage before it sees the
            // audio they belong to.
            auto it = pending.find(b);
            if (it != pending.end()) {
                for (const auto& p : it->second) {
                    uint32_t off = p.writeFrames > frames ? frames : p.writeFrames;
                    uint64_t at = produced + off;
                    if (p.word) {
                        events_.push_back({InfovoxEvent::kWord, uint32_t(at), p.a, p.b});
                    } else {
                        events_.push_back({InfovoxEvent::kPhoneme, uint32_t(at), p.a, 0});
                        const std::string& sym =
                            (p.a >= 0 && size_t(p.a) < phonemes_.size()) ? phonemes_[p.a]
                                                                         : std::string();
                        clarity_.addPhoneme(at, fricationFor(sym, currentPack_));
                    }
                }
                it->second.clear();
            }

            raw.resize(size_t(frames) * bytesPerFrame);
            mac_->read(b + kDbHeaderSize, raw.data(), raw.size());
            pcm.resize(frames);
            for (uint32_t i = 0; i < frames; ++i)
                pcm[i] = int16_t((uint32_t(raw[i * 2]) << 8) | raw[i * 2 + 1]);

            // Clarity runs before the volume trim so it always sees the level
            // the engine actually produces; otherwise its level reference, and
            // with it the loudness of the frication, would move with a slider
            // that is meant only to make the whole thing quieter.
            clarity_.process(pcm.data(), pcm.size());
            if (gain != 1.0) {
                for (uint32_t i = 0; i < frames; ++i) {
                    int v = int(pcm[i] * gain);
                    pcm[i] = int16_t(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
                }
            }

            if (stopping) {
                // Ramp what continues on from the last buffer down to zero.
                size_t n = pcm.size() < fadeFrames ? pcm.size() : fadeFrames;
                for (size_t i = 0; i < n; ++i) {
                    double w = 1.0 - double(i) / double(n);
                    pcm[i] = int16_t(double(pcm[i]) * w);
                }
                pcm.resize(n);
            }
            if (!sink(pcm.data(), pcm.size())) {
                component(kStop, {});
                return true;
            }
            produced += frames;
        }
        if (stopping) {
            component(kStop, {});
            return true;
        }
        if (flags & dbLastBuffer) break;

        mac_->wr32(b + 4, flags & ~dbBufferReady);
        mac_->wr32(b, 0);
        mac_->runPendingTasks();
        mac_->beginBufferFill(b);
        mac_->callUpp(d.doubleBackProc, {d.channel, b});
        if (mac_->faulted()) { if (err) *err = mac_->lastError(); return false; }

        collect(b);
        which ^= 1;
    }
    return true;
}

}  // namespace ppc
