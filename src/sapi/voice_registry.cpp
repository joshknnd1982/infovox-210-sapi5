#include "voice_registry.hpp"

#include <cstdio>
#include <cstring>

#include <shlwapi.h>

#include "pipe_client.h"

namespace Infovox {
namespace sapi {

namespace {

constexpr wchar_t kTokensKey[] =
    L"SOFTWARE\\Microsoft\\Speech\\Voices\\Tokens";
constexpr wchar_t kTokenPrefix[] = L"Infovox210_";

std::wstring widen(const char* s) {
    return std::wstring(s, s + strlen(s));
}

bool setValue(HKEY key, const wchar_t* name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(value.c_str()),
                          DWORD((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

void deleteTree(HKEY parent, const wchar_t* name) {
    // RegDeleteKeyW refuses a key that still has subkeys, and every voice token
    // has an Attributes subkey -- deleting only the top level would leave the
    // tokens in place and the voices broken.
    RegDeleteTreeW(parent, name);
}

}  // namespace

const voice_row* voiceById(const char* id) {
    for (const auto& v : kVoiceTable)
        if (strncmp(v.id, id, 4) == 0) return &v;
    return nullptr;
}

bool packInstalled(const char* pack) {
    std::wstring path = installDir() + L"engine\\" + widen(pack) + L".ivp";
    return PathFileExistsW(path.c_str()) != FALSE;
}

std::vector<size_t> installedVoiceIndices() {
    std::vector<size_t> out;
    for (size_t i = 0; i < kVoiceCount; ++i)
        if (packInstalled(kVoiceTable[i].pack)) out.push_back(i);
    return out;
}

void write_voice_tokens(HKEY root, const std::wstring& clsid) {
    HKEY tokens = nullptr;
    if (RegCreateKeyExW(root, kTokensKey, 0, nullptr, 0,
                        KEY_READ | KEY_WRITE, nullptr, &tokens, nullptr) != ERROR_SUCCESS)
        return;

    std::vector<size_t> installed = installedVoiceIndices();
    // Index kVoiceCount is the configurable voice; publish it whenever any
    // language is installed, because it can be pointed at any of them.
    std::vector<size_t> all = installed;
    if (!installed.empty()) all.push_back(kCustomVoiceIndex);

    for (size_t idx : all) {
        const bool custom = (idx == kCustomVoiceIndex);
        const voice_row* v = custom ? nullptr : &kVoiceTable[idx];

        std::wstring tokenName = kTokenPrefix;
        tokenName += custom ? L"Custom" : widen(v->id);

        HKEY tok = nullptr;
        if (RegCreateKeyExW(tokens, tokenName.c_str(), 0, nullptr, 0,
                            KEY_READ | KEY_WRITE, nullptr, &tok, nullptr) != ERROR_SUCCESS)
            continue;

        const std::wstring display = custom ? kCustomVoiceName : v->name;
        setValue(tok, nullptr, display);
        setValue(tok, L"CLSID", clsid);
        // Our own key, so the engine knows which voice a token stands for
        // without parsing its display name.
        setValue(tok, L"InfovoxVoice", custom ? L"custom" : widen(v->id));

        HKEY attrs = nullptr;
        if (RegCreateKeyExW(tok, L"Attributes", 0, nullptr, 0,
                            KEY_READ | KEY_WRITE, nullptr, &attrs, nullptr) == ERROR_SUCCESS) {
            wchar_t lang[16];
            swprintf_s(lang, L"%X", custom ? 0x0409u : v->lcid);
            setValue(attrs, L"Language", lang);
            setValue(attrs, L"Gender", custom ? L"Male"
                                     : v->gender == 2 ? L"Female"
                                     : v->gender == 1 ? L"Male" : L"Neutral");
            setValue(attrs, L"Age",
                     custom ? L"Adult" : (v->age <= 12 ? L"Child" : L"Adult"));
            setValue(attrs, L"Vendor", L"Infovox AB");
            setValue(attrs, L"Name", display);
            setValue(attrs, L"Version", L"2.0.2");
            RegCloseKey(attrs);
        }
        RegCloseKey(tok);
    }
    RegCloseKey(tokens);
}

void remove_voice_tokens(HKEY root) noexcept {
    HKEY tokens = nullptr;
    if (RegOpenKeyExW(root, kTokensKey, 0, KEY_READ | KEY_WRITE, &tokens) != ERROR_SUCCESS)
        return;
    for (const auto& v : kVoiceTable) {
        std::wstring name = std::wstring(kTokenPrefix) + widen(v.id);
        deleteTree(tokens, name.c_str());
    }
    deleteTree(tokens, (std::wstring(kTokenPrefix) + L"Custom").c_str());
    RegCloseKey(tokens);
}

}  // namespace sapi
}  // namespace Infovox
