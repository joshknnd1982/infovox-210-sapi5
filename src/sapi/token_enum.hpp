#pragma once

// Small replacements for the two sphelper.h helpers we need.  sphelper.h drags
// in ATL, which the Build Tools install does not necessarily have, and all we
// want is to walk the voice category and read a token's display name.
#include <string>

#include <windows.h>
#include <sapi.h>

namespace Infovox {
namespace sapi {

inline HRESULT enumVoiceTokens(IEnumSpObjectTokens** out) {
    *out = nullptr;
    ISpObjectTokenCategory* cat = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                  IID_ISpObjectTokenCategory,
                                  reinterpret_cast<void**>(&cat));
    if (FAILED(hr)) return hr;
    hr = cat->SetId(SPCAT_VOICES, FALSE);
    if (SUCCEEDED(hr)) hr = cat->EnumTokens(nullptr, nullptr, out);
    cat->Release();
    return hr;
}

inline std::wstring tokenDescription(ISpObjectToken* token) {
    LPWSTR value = nullptr;
    if (SUCCEEDED(token->GetStringValue(nullptr, &value)) && value) {
        std::wstring s(value);
        CoTaskMemFree(value);
        return s;
    }
    return L"(unnamed)";
}

}  // namespace sapi
}  // namespace Infovox
