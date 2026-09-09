#include "ISpTTSEngineImpl.hpp"

#include <algorithm>
#include <cstring>
#include <new>

#include "user_settings.hpp"

namespace Infovox {
namespace sapi {

namespace {

constexpr WORD kChannels = INFOVOX_CHANNELS;
constexpr WORD kBits = INFOVOX_BITS_PER_SAMPLE;
constexpr DWORD kSampleRate = INFOVOX_SAMPLE_RATE;

// A screen reader's own 0..100 slider becomes SAPI -10..+10, so mapping that
// whole span onto the engine's own limits is what puts the engine's minimum at
// slider 0 and its maximum at slider 100.
int sapiToSlider(long v) {
    long s = (v + 10) * 5;          // -10..+10 -> 0..100
    return int(s < 0 ? 0 : s > 100 ? 100 : s);
}

// Wide text to Mac Roman, which is the encoding the 1996 engine reads.
std::string toMacRoman(const wchar_t* text, size_t len) {
    if (!text || !len) return std::string();
    int need = WideCharToMultiByte(10000, 0, text, int(len), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return std::string();
    std::string out(size_t(need), '\0');
    WideCharToMultiByte(10000, 0, text, int(len), &out[0], need, nullptr, nullptr);
    return out;
}

// The engine treats "[[" as the start of an embedded command and silently eats
// everything up to the matching "]]" -- a document that happens to contain
// double brackets would go unspoken.  Splitting the pair keeps the text audible.
void defuseCommandDelimiters(std::string* s) {
    for (size_t i = 0; i + 1 < s->size(); ++i) {
        if ((*s)[i] == '[' && (*s)[i + 1] == '[') {
            s->insert(i + 1, 1, ' ');
            ++i;
        }
    }
}

// A fragment that is nothing but characters the engine renders silently would
// leave a screen reader saying nothing at all when the user arrows onto them.
bool allSilent(const std::string& s) {
    for (unsigned char c : s) {
        if (isalnum(c)) return false;
        if (c >= 0x80) return false;
    }
    return true;
}

}  // namespace

// Reference counting and the live-object count belong to com::IUnknownImpl.
ISpTTSEngineImpl::ISpTTSEngineImpl() = default;
ISpTTSEngineImpl::~ISpTTSEngineImpl() = default;

STDMETHODIMP ISpTTSEngineImpl::SetObjectToken(ISpObjectToken* pToken) {
    if (!pToken) return E_INVALIDARG;
    token_ = pToken;
    LPWSTR value = nullptr;
    if (SUCCEEDED(pToken->GetStringValue(L"InfovoxVoice", &value)) && value) {
        std::wstring v(value);
        CoTaskMemFree(value);
        if (_wcsicmp(v.c_str(), L"custom") == 0) {
            custom_ = true;
        } else if (v.size() == 4) {
            voiceId_.assign(v.begin(), v.end());
        }
    }
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::GetObjectToken(ISpObjectToken** ppToken) {
    if (!ppToken) return E_POINTER;
    *ppToken = token_;
    if (*ppToken) (*ppToken)->AddRef();
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::GetOutputFormat(const GUID*, const WAVEFORMATEX*,
                                               GUID* pOutputFormatId,
                                               WAVEFORMATEX** ppCoMemOutputWaveFormatEx) {
    if (!pOutputFormatId || !ppCoMemOutputWaveFormatEx) return E_POINTER;
    auto* wf = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!wf) return E_OUTOFMEMORY;
    wf->wFormatTag = WAVE_FORMAT_PCM;
    wf->nChannels = kChannels;
    wf->nSamplesPerSec = kSampleRate;
    wf->wBitsPerSample = kBits;
    wf->nBlockAlign = WORD(kChannels * kBits / 8);
    wf->nAvgBytesPerSec = kSampleRate * wf->nBlockAlign;
    wf->cbSize = 0;
    *pOutputFormatId = SPDFID_WaveFormatEx;
    *ppCoMemOutputWaveFormatEx = wf;
    return S_OK;
}

bool ISpTTSEngineImpl::speakFragment(const SPVTEXTFRAG* frag, ISpTTSEngineSite* site,
                                     bool spellOut, ULONGLONG* streamSamples) {
    std::string text = toMacRoman(frag->pTextStart, frag->ulTextLen);
    defuseCommandDelimiters(&text);
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n')) text.pop_back();
    if (text.empty()) return true;

    // A lone punctuation mark is silent in normal mode; spelling it out is what
    // the user actually asked for when they arrowed onto it.
    bool spell = spellOut || (frag->ulTextLen <= 2 && allSilent(text));

    SpeakCommand cmd{};
    std::string voice = voiceId_;
    CustomVoiceSettings cs;
    if (custom_) {
        // Re-read every utterance so a change in the configuration utility
        // takes effect without restarting the screen reader.
        cs = loadCustomVoice();
        voice = cs.voiceId;
    }
    for (int i = 0; i < 4; ++i)
        cmd.voiceId[i] = i < int(voice.size()) ? voice[i] : ' ';

    long rate = 0;
    site->GetRate(&rate);
    rate += frag->State.RateAdj;
    USHORT vol = 100;
    site->GetVolume(&vol);

    if (custom_) {
        cmd.rate = cs.rate;
        cmd.pitch = cs.pitch;
        cmd.pitchMod = cs.pitchMod;
        cmd.breath = cs.breath;
        cmd.volume = cs.volume;
        cmd.clarity = cs.clarity;
    } else {
        VoiceTweaks tw = loadVoiceTweaks(voice);
        cmd.rate = sapiToSlider(rate);
        // SAPI has no GetPitch: pitch only ever arrives on the fragment.
        cmd.pitch = sapiToSlider(frag->State.PitchAdj.MiddleAdj);
        cmd.pitchMod = tw.pitchMod < 0 ? 25 : tw.pitchMod;
        cmd.breath = tw.breath;
        cmd.clarity = tw.clarity;
        cmd.volume = 100;
    }
    // The engine accepts soVolume but never applies it, so volume is a software
    // gain; fold the site's and the fragment's levels into it.
    long v = long(cmd.volume) * long(vol) / 100;
    v = v * long(frag->State.Volume) / 100;
    cmd.volume = int(v < 0 ? 0 : v > 100 ? 100 : v);
    cmd.spellOut = spell ? 1u : 0u;
    cmd.literalNumbers = 0;

    bool aborted = false;
    ULONGLONG base = *streamSamples;
    auto sink = [&](const int16_t* frames, size_t count) -> bool {
        if (site->GetActions() & SPVES_ABORT) { aborted = true; return false; }
        ULONG written = 0;
        // SAPI does not reliably fill pcbWritten; a loop that trusts it stalls
        // or truncates.  A successful call has taken the whole buffer.
        if (FAILED(site->Write(frames, ULONG(count * sizeof(int16_t)), &written))) {
            aborted = true;
            return false;
        }
        *streamSamples += count;
        return true;
    };
    auto abortFn = [&]() -> bool {
        return aborted || (site->GetActions() & SPVES_ABORT) != 0;
    };

    std::vector<EventRecord> events;
    std::wstring err;
    if (!pipe_.speak(cmd, text, sink, abortFn, &events, &err)) return !aborted;

    // Word boundaries, so a host that highlights spoken words can follow along.
    ULONGLONG interest = 0;
    site->GetEventInterest(&interest);
    if (!events.empty() && (interest & SPFEI(SPEI_WORD_BOUNDARY))) {
        for (const EventRecord& e : events) {
            if (e.kind != 0) continue;
            SPEVENT ev{};
            ev.eEventId = SPEI_WORD_BOUNDARY;
            ev.elParamType = SPET_LPARAM_IS_UNDEFINED;
            ev.ullAudioStreamOffset =
                (base + e.sampleOffset) * (kBits / 8) * kChannels;
            ev.lParam = LPARAM(frag->ulTextSrcOffset + ULONG(e.a));
            ev.wParam = WPARAM(e.b);
            site->AddEvents(&ev, 1);
        }
    }
    return !aborted;
}

STDMETHODIMP ISpTTSEngineImpl::Speak(DWORD, REFGUID, const WAVEFORMATEX*,
                                     const SPVTEXTFRAG* pTextFragList,
                                     ISpTTSEngineSite* pOutputSite) {
    if (!pOutputSite) return E_POINTER;
    if (!pTextFragList) return S_OK;

    ULONGLONG streamSamples = 0;
    for (const SPVTEXTFRAG* frag = pTextFragList; frag; frag = frag->pNext) {
        if (pOutputSite->GetActions() & SPVES_ABORT) break;

        switch (frag->State.eAction) {
            case SPVA_Speak:
            case SPVA_SpellOut:
            case SPVA_Pronounce:
                // Anything else -- bookmarks above all -- must not reach the
                // engine, or their contents get read out as if they were text.
                if (!speakFragment(frag, pOutputSite,
                                   frag->State.eAction == SPVA_SpellOut,
                                   &streamSamples))
                    return S_OK;
                break;

            case SPVA_Silence: {
                // A run of digital silence of the requested length.
                ULONG ms = frag->State.SilenceMSecs;
                size_t frames = size_t(kSampleRate) * ms / 1000;
                std::vector<int16_t> quiet(std::min<size_t>(frames, kSampleRate), 0);
                while (frames) {
                    size_t n = std::min(frames, quiet.size());
                    ULONG written = 0;
                    if (FAILED(pOutputSite->Write(quiet.data(),
                                                  ULONG(n * sizeof(int16_t)), &written)))
                        return S_OK;
                    streamSamples += n;
                    frames -= n;
                }
                break;
            }

            case SPVA_Bookmark: {
                ULONGLONG interest = 0;
                pOutputSite->GetEventInterest(&interest);
                if (interest & SPFEI(SPEI_TTS_BOOKMARK)) {
                    std::wstring mark(frag->pTextStart, frag->ulTextLen);
                    SPEVENT ev{};
                    ev.eEventId = SPEI_TTS_BOOKMARK;
                    ev.elParamType = SPET_LPARAM_IS_STRING;
                    ev.ullAudioStreamOffset = streamSamples * (kBits / 8) * kChannels;
                    ev.lParam = LPARAM(mark.c_str());
                    ev.wParam = WPARAM(_wtol(mark.c_str()));
                    pOutputSite->AddEvents(&ev, 1);
                }
                break;
            }

            default:
                break;
        }
    }
    return S_OK;
}

}  // namespace sapi
}  // namespace Infovox
