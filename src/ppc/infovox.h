// Infovox 210 engine driver: opens the Component Manager component under the
// PowerPC runtime, selects voices, applies parameters and renders PCM.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "clarity.h"
#include "ivpack.h"
#include "macos.h"

namespace ppc {

// Every parameter the engine actually exposes, on a uniform 0..100 scale where
// 0 is the engine minimum and 100 the engine maximum.
struct InfovoxParams {
    int rate     = 15;    // 0..100 -> 0..999 words per minute (engine default 150)
    int pitch    = 50;    // 0..100 -> soPitchBase   0..100
    int pitchMod = 25;    // 0..100 -> soPitchMod    0..100
    int breath   = 0;     // 0..100 -> InVx 'aspi'   0..100
    int volume   = 100;   // 0..100 -> software gain (the engine ignores soVolume)
    bool spellOut = false;  // soCharacterMode LTRL: say each character's name
    bool literalNumbers = false;  // soNumberMode LTRL: say digits one by one
    // The engine cannot synthesise above ~4 kHz, which is where sibilants
    // live; this puts that band back.  0 is the engine untouched.
    int clarity = 40;
};

struct InfovoxVoice {
    std::string id;        // 'AM01'
    std::string pack;      // "american"
    std::string name;      // "American: male voice"
    std::string comment;
    int   variant = 1;     // 1..5 within the language
    int   gender = 0;      // 0 neuter, 1 male, 2 female
    int   age = 30;
    int   language = 0;    // Mac OS language code
    int   region = 0;
    int   version = 0;
};

// One rendered utterance event (word or phoneme boundary).
struct InfovoxEvent {
    enum Kind { kWord, kPhoneme } kind;
    uint32_t sampleOffset;   // frames from the start of the utterance
    int32_t  a, b;           // word: byte offset + length; phoneme: opcode
};

class InfovoxEngine {
public:
    InfovoxEngine();
    ~InfovoxEngine();

    // engineDir holds infovox210.ivp plus one .ivp per installed language.
    bool init(const std::wstring& engineDir, std::string* err);

    const std::vector<InfovoxVoice>& voices() const { return voices_; }
    const InfovoxVoice* findVoice(const std::string& id) const;

    bool setVoice(const std::string& id, std::string* err);
    const std::string& currentVoice() const { return currentVoice_; }

    void setParams(const InfovoxParams& p) { params_ = p; }
    const InfovoxParams& params() const { return params_; }

    uint32_t sampleRate() const { return sampleRate_; }
    uint32_t heapUsed() const { return mac_ ? mac_->heapUsed() : 0; }

    // Streams 16-bit little-endian mono PCM to `sink`.  Returning false from
    // either callback aborts the utterance promptly (between 46 ms buffers).
    using PcmSink = std::function<bool(const int16_t* frames, size_t count)>;
    using CancelFn = std::function<bool()>;
    bool render(const std::string& macRomanText, const PcmSink& sink,
                const CancelFn& cancelled, std::string* err);

    const std::vector<InfovoxEvent>& events() const { return events_; }

    // Phoneme symbols for the current language, indexed by opcode.
    const std::vector<std::string>& phonemeSymbols() const { return phonemes_; }

private:
    int32_t component(int16_t selector, const std::vector<uint32_t>& params);
    bool    applyParams(std::string* err);
    int32_t handleGetVoiceInfo(uint32_t voiceSpec, uint32_t selector, uint32_t info);
    void    loadPhonemeTable();
    void    stopChannel();
    bool    applyModes();

    // The engine rejects a mode change while the channel is busy, so the modes
    // in force for the last utterance are tracked and only re-sent on a change.
    bool curSpellOut_ = false;
    bool curLiteralNumbers_ = false;

    std::unique_ptr<MacRuntime> mac_;
    ResourcePack component_;
    std::vector<std::unique_ptr<ResourcePack>> packs_;
    std::vector<std::string> packNames_;      // parallel to packs_
    std::vector<std::string> packFileNames_;  // Mac file name used by HOpenResFile
    std::vector<InfovoxVoice> voices_;
    std::vector<int> voicePackIndex_;         // parallel to voices_

    std::string currentVoice_;
    InfovoxParams params_;
    uint32_t sampleRate_ = 22050;
    uint32_t textBuf_ = 0, scratch_ = 0;
    uint32_t instance_ = 0x00CC0001;
    std::vector<InfovoxEvent> events_;
    std::vector<std::string> phonemes_;
    ConsonantClarity clarity_;
    bool opened_ = false;
};

}  // namespace ppc
