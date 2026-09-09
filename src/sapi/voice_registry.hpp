#pragma once

#include <string>
#include <vector>

#include <windows.h>

namespace Infovox {
namespace sapi {

struct voice_row {
    const char*    id;        // 'AM01'
    const char*    pack;      // "american"
    const wchar_t* name;      // display name
    const wchar_t* language;  // human readable language
    unsigned       lcid;
    int            gender;    // 0 neuter, 1 male, 2 female
    int            age;
    int            variant;   // 1..5 within the language
};

#include "voice_table.inc"

constexpr size_t kVoiceCount = sizeof(kVoiceTable) / sizeof(kVoiceTable[0]);
constexpr size_t kPackCount = sizeof(kPackLabels) / sizeof(kPackLabels[0]);

// Token index kVoiceCount is the configurable voice.
constexpr size_t kCustomVoiceIndex = kVoiceCount;
constexpr size_t kTokenCount = kVoiceCount + 1;

const voice_row* voiceById(const char* id);
bool packInstalled(const char* pack);      // engine\<pack>.ivp exists
std::vector<size_t> installedVoiceIndices();

// Writes one SAPI voice token per installed voice into
// HKLM\SOFTWARE\Microsoft\Speech\Voices\Tokens.  Which registry view they land
// in follows the architecture of the dll doing the registering, so the 32-bit
// build fills WOW6432Node and the 64-bit build the native view; both are
// installed, so every host sees the full set.
void write_voice_tokens(HKEY root, const std::wstring& clsid);
void remove_voice_tokens(HKEY root) noexcept;

}  // namespace sapi
}  // namespace Infovox
