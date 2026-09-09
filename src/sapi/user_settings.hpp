#pragma once

#include <string>

#include <windows.h>

namespace Infovox {
namespace sapi {

// Where the configuration utility and the engine meet.
//
//   HKCU\Software\Infovox210\CustomVoice     the "Infovox 210 Custom Voice"
//                                            token: language, voice variant and
//                                            every parameter
//   HKCU\Software\Infovox210\Voices\<id>     per-voice values for the two
//                                            parameters SAPI has no slider for
//                                            (pitch modulation and breathiness)
//
// Everything is a 0..100 slider where 0 is the engine minimum and 100 the
// engine maximum, so the extremes of a screen reader's own sliders reach the
// extremes of the synthesizer.
constexpr wchar_t kSettingsRoot[] = L"Software\\Infovox210";

struct CustomVoiceSettings {
    std::string voiceId = "AM01";   // which language + variant it speaks with
    int rate = 15;                  // 0..100 -> 0..999 words per minute
    int pitch = 50;
    int pitchMod = 25;
    int breath = 0;
    int volume = 100;
    int clarity = 40;   // 0 = the engine's own band-limited output
};

inline int clamp100(int v) { return v < 0 ? 0 : v > 100 ? 100 : v; }

inline bool readDword(HKEY root, const wchar_t* subkey, const wchar_t* name, int* out) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, val = 0, size = sizeof val;
    bool ok = RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<BYTE*>(&val),
                               &size) == ERROR_SUCCESS && type == REG_DWORD;
    RegCloseKey(k);
    if (ok) *out = int(val);
    return ok;
}

inline bool readString(HKEY root, const wchar_t* subkey, const wchar_t* name,
                       std::wstring* out) {
    HKEY k = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[256] = {0};
    DWORD type = 0, size = sizeof buf;
    bool ok = RegQueryValueExW(k, name, nullptr, &type, reinterpret_cast<BYTE*>(buf),
                               &size) == ERROR_SUCCESS && type == REG_SZ;
    RegCloseKey(k);
    if (ok) *out = buf;
    return ok;
}

inline void writeDword(HKEY root, const wchar_t* subkey, const wchar_t* name, int v) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k,
                        nullptr) != ERROR_SUCCESS)
        return;
    DWORD val = DWORD(v);
    RegSetValueExW(k, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof val);
    RegCloseKey(k);
}

inline void writeString(HKEY root, const wchar_t* subkey, const wchar_t* name,
                        const std::wstring& v) {
    HKEY k = nullptr;
    if (RegCreateKeyExW(root, subkey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k,
                        nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                   DWORD((v.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

// Re-read for every utterance, so a change in the configuration utility takes
// effect on the next thing spoken without restarting the screen reader.
inline CustomVoiceSettings loadCustomVoice() {
    CustomVoiceSettings s;
    const std::wstring key = std::wstring(kSettingsRoot) + L"\\CustomVoice";
    std::wstring v;
    if (readString(HKEY_CURRENT_USER, key.c_str(), L"Voice", &v) && v.size() == 4) {
        s.voiceId.assign(v.begin(), v.end());
    }
    int t;
    if (readDword(HKEY_CURRENT_USER, key.c_str(), L"Rate", &t))     s.rate = clamp100(t);
    if (readDword(HKEY_CURRENT_USER, key.c_str(), L"Pitch", &t))    s.pitch = clamp100(t);
    if (readDword(HKEY_CURRENT_USER, key.c_str(), L"PitchMod", &t)) s.pitchMod = clamp100(t);
    if (readDword(HKEY_CURRENT_USER, key.c_str(), L"Breath", &t))   s.breath = clamp100(t);
    if (readDword(HKEY_CURRENT_USER, key.c_str(), L"Volume", &t))   s.volume = clamp100(t);
    if (readDword(HKEY_CURRENT_USER, key.c_str(), L"Clarity", &t))  s.clarity = clamp100(t);
    return s;
}

inline void saveCustomVoice(const CustomVoiceSettings& s) {
    const std::wstring key = std::wstring(kSettingsRoot) + L"\\CustomVoice";
    writeString(HKEY_CURRENT_USER, key.c_str(), L"Voice",
                std::wstring(s.voiceId.begin(), s.voiceId.end()));
    writeDword(HKEY_CURRENT_USER, key.c_str(), L"Rate", s.rate);
    writeDword(HKEY_CURRENT_USER, key.c_str(), L"Pitch", s.pitch);
    writeDword(HKEY_CURRENT_USER, key.c_str(), L"PitchMod", s.pitchMod);
    writeDword(HKEY_CURRENT_USER, key.c_str(), L"Breath", s.breath);
    writeDword(HKEY_CURRENT_USER, key.c_str(), L"Volume", s.volume);
    writeDword(HKEY_CURRENT_USER, key.c_str(), L"Clarity", s.clarity);
}

// Pitch modulation and breathiness for an ordinary voice: SAPI has no slider
// for either, so the configuration utility is the only place they can be set.
struct VoiceTweaks {
    int pitchMod = -1;    // -1 keeps the voice's own default
    int breath = 0;
    int clarity = 40;     // 0 = the engine's own band-limited output
};

inline VoiceTweaks loadVoiceTweaks(const std::string& voiceId) {
    VoiceTweaks t;
    std::wstring key = std::wstring(kSettingsRoot) + L"\\Voices\\";
    key.append(voiceId.begin(), voiceId.end());
    int v;
    if (readDword(HKEY_CURRENT_USER, key.c_str(), L"PitchMod", &v)) t.pitchMod = clamp100(v);
    if (readDword(HKEY_CURRENT_USER, key.c_str(), L"Breath", &v))   t.breath = clamp100(v);
    if (readDword(HKEY_CURRENT_USER, key.c_str(), L"Clarity", &v))  t.clarity = clamp100(v);
    return t;
}

inline void saveVoiceTweaks(const std::string& voiceId, const VoiceTweaks& t) {
    std::wstring key = std::wstring(kSettingsRoot) + L"\\Voices\\";
    key.append(voiceId.begin(), voiceId.end());
    writeDword(HKEY_CURRENT_USER, key.c_str(), L"PitchMod", t.pitchMod);
    writeDword(HKEY_CURRENT_USER, key.c_str(), L"Breath", t.breath);
    writeDword(HKEY_CURRENT_USER, key.c_str(), L"Clarity", t.clarity);
}

}  // namespace sapi
}  // namespace Infovox
