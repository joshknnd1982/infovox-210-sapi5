// Infovox 210 settings.
//
// Every parameter the engine exposes, on one 0..100 scale where 0 is the
// engine's minimum and 100 its maximum.  Each control carries a label that sits
// immediately before it in the tab order, so a screen reader announces what the
// value belongs to.
#include <string>
#include <vector>

#include <windows.h>
#include <commctrl.h>
#include <sapi.h>
#include <objbase.h>

#include "infovox_config.h"
#include "../src/sapi/token_enum.hpp"
#include "../src/sapi/user_settings.hpp"
#include "../src/sapi/voice_registry.hpp"

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace Infovox::sapi;

namespace {

HINSTANCE g_instance = nullptr;

// WM_COMMAND arrives from the spin buddies while the dialog is still being
// created -- before WM_INITDIALOG -- so this starts true or the first EN_CHANGE
// would write uninitialised values over the user's saved settings.
bool g_loading = true;

std::vector<size_t> g_installed;     // indices into kVoiceTable
std::vector<std::string> g_packs;    // installed language slugs, in table order

const wchar_t* const kVariantNames[5] = {
    L"Male", L"Female", L"Deep male", L"Child", L"Ghost"
};

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

int getInt(HWND dlg, int id) {
    BOOL ok = FALSE;
    int v = int(GetDlgItemInt(dlg, id, &ok, FALSE));
    return ok ? clamp100(v) : 0;
}

void setInt(HWND dlg, int id, int v) { SetDlgItemInt(dlg, id, UINT(clamp100(v)), FALSE); }

void setStatus(HWND dlg, const wchar_t* text) { SetDlgItemTextW(dlg, IDC_STATUS, text); }

bool customSelected(HWND dlg) {
    int sel = int(SendDlgItemMessageW(dlg, IDC_VOICE, CB_GETCURSEL, 0, 0));
    return sel >= 0 && sel == int(g_installed.size());
}

std::string selectedVoiceId(HWND dlg) {
    int sel = int(SendDlgItemMessageW(dlg, IDC_VOICE, CB_GETCURSEL, 0, 0));
    if (sel < 0 || sel >= int(g_installed.size())) return std::string();
    return kVoiceTable[g_installed[sel]].id;
}

// The language + voice type pair chosen in the custom voice group.
std::string customVoiceIdFromUi(HWND dlg) {
    int lang = int(SendDlgItemMessageW(dlg, IDC_LANG, CB_GETCURSEL, 0, 0));
    int variant = int(SendDlgItemMessageW(dlg, IDC_VARIANT, CB_GETCURSEL, 0, 0));
    if (lang < 0 || lang >= int(g_packs.size())) lang = 0;
    if (variant < 0 || variant > 4) variant = 0;
    for (size_t i : g_installed) {
        if (g_packs[size_t(lang)] == kVoiceTable[i].pack &&
            kVoiceTable[i].variant == variant + 1)
            return kVoiceTable[i].id;
    }
    return "AM01";
}

void selectCustomVoiceInUi(HWND dlg, const std::string& id) {
    const voice_row* v = voiceById(id.c_str());
    if (!v) return;
    for (size_t i = 0; i < g_packs.size(); ++i) {
        if (g_packs[i] == v->pack) {
            SendDlgItemMessageW(dlg, IDC_LANG, CB_SETCURSEL, i, 0);
            break;
        }
    }
    SendDlgItemMessageW(dlg, IDC_VARIANT, CB_SETCURSEL, v->variant - 1, 0);
}

// SAPI supplies rate, pitch and volume for an ordinary voice; only the two
// parameters it has no slider for are ours to set there.  The custom voice
// takes every value from here.
void updateEnabledState(HWND dlg) {
    const bool custom = customSelected(dlg);
    for (int id : {IDC_LANG, IDC_VARIANT, IDC_LANG_LABEL, IDC_VARIANT_LABEL})
        EnableWindow(GetDlgItem(dlg, id), custom);
    for (int id : {IDC_RATE, IDC_RATE_SPIN, IDC_PITCH, IDC_PITCH_SPIN,
                   IDC_VOLUME, IDC_VOLUME_SPIN, IDC_RATE_LABEL,
                   IDC_PITCH_LABEL, IDC_VOLUME_LABEL})
        EnableWindow(GetDlgItem(dlg, id), custom);
    SetDlgItemTextW(dlg, IDC_NOTE,
                    custom
                    ? L"The custom voice speaks with every value set here."
                    : L"Rate, pitch and volume for this voice come from your "
                      L"screen reader; pitch modulation and breathiness are set here.");
}

void loadIntoUi(HWND dlg) {
    g_loading = true;
    if (customSelected(dlg)) {
        CustomVoiceSettings s = loadCustomVoice();
        selectCustomVoiceInUi(dlg, s.voiceId);
        setInt(dlg, IDC_RATE, s.rate);
        setInt(dlg, IDC_PITCH, s.pitch);
        setInt(dlg, IDC_PMOD, s.pitchMod);
        setInt(dlg, IDC_BREATH, s.breath);
        setInt(dlg, IDC_VOLUME, s.volume);
        setInt(dlg, IDC_CLARITY, s.clarity);
    } else {
        std::string id = selectedVoiceId(dlg);
        VoiceTweaks t = loadVoiceTweaks(id);
        setInt(dlg, IDC_RATE, 15);
        setInt(dlg, IDC_PITCH, 50);
        setInt(dlg, IDC_PMOD, t.pitchMod < 0 ? 25 : t.pitchMod);
        setInt(dlg, IDC_BREATH, t.breath);
        setInt(dlg, IDC_VOLUME, 100);
        setInt(dlg, IDC_CLARITY, t.clarity);
    }
    updateEnabledState(dlg);
    g_loading = false;
}

void saveFromUi(HWND dlg) {
    if (g_loading) return;
    if (customSelected(dlg)) {
        CustomVoiceSettings s;
        s.voiceId = customVoiceIdFromUi(dlg);
        s.rate = getInt(dlg, IDC_RATE);
        s.pitch = getInt(dlg, IDC_PITCH);
        s.pitchMod = getInt(dlg, IDC_PMOD);
        s.breath = getInt(dlg, IDC_BREATH);
        s.volume = getInt(dlg, IDC_VOLUME);
        s.clarity = getInt(dlg, IDC_CLARITY);
        saveCustomVoice(s);
    } else {
        VoiceTweaks t;
        t.pitchMod = getInt(dlg, IDC_PMOD);
        t.breath = getInt(dlg, IDC_BREATH);
        t.clarity = getInt(dlg, IDC_CLARITY);
        saveVoiceTweaks(selectedVoiceId(dlg), t);
    }
    setStatus(dlg, L"Settings saved.");
}

// Speaks through SAPI itself, which is the one path that exercises everything
// the screen reader will use.
void speakTest(HWND dlg) {
    saveFromUi(dlg);
    wchar_t text[512] = {0};
    GetDlgItemTextW(dlg, IDC_TEXT, text, 511);
    if (!text[0])
        wcscpy_s(text, L"The quick brown fox jumps over the lazy dog.");

    std::wstring want = L"Infovox210_";
    want += customSelected(dlg) ? L"Custom" : widen(selectedVoiceId(dlg));

    ISpVoice* voice = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                IID_ISpVoice, reinterpret_cast<void**>(&voice)))) {
        setStatus(dlg, L"Could not start SAPI.");
        return;
    }
    IEnumSpObjectTokens* en = nullptr;
    bool set = false;
    if (SUCCEEDED(enumVoiceTokens(&en)) && en) {
        ISpObjectToken* tok = nullptr;
        while (!set && en->Next(1, &tok, nullptr) == S_OK && tok) {
            LPWSTR id = nullptr;
            if (SUCCEEDED(tok->GetId(&id)) && id) {
                std::wstring sid(id);
                CoTaskMemFree(id);
                if (sid.size() >= want.size() &&
                    _wcsicmp(sid.c_str() + sid.size() - want.size(), want.c_str()) == 0) {
                    set = SUCCEEDED(voice->SetVoice(tok));
                }
            }
            tok->Release();
            tok = nullptr;
        }
        en->Release();
    }
    if (!set) {
        setStatus(dlg, L"That voice is not registered with SAPI.");
        voice->Release();
        return;
    }
    setStatus(dlg, L"Speaking...");
    voice->Speak(text, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
    voice->Release();
}

void restoreDefaults(HWND dlg) {
    g_loading = true;
    setInt(dlg, IDC_RATE, 15);
    setInt(dlg, IDC_PITCH, 50);
    setInt(dlg, IDC_PMOD, 25);
    setInt(dlg, IDC_BREATH, 0);
    setInt(dlg, IDC_VOLUME, 100);
    setInt(dlg, IDC_CLARITY, 40);
    g_loading = false;
    saveFromUi(dlg);
    setStatus(dlg, L"Defaults restored.");
}

void populate(HWND dlg) {
    g_installed = installedVoiceIndices();
    g_packs.clear();
    for (size_t i : g_installed) {
        bool seen = false;
        for (const auto& p : g_packs) if (p == kVoiceTable[i].pack) { seen = true; break; }
        if (!seen) g_packs.push_back(kVoiceTable[i].pack);
    }
    for (size_t i : g_installed)
        SendDlgItemMessageW(dlg, IDC_VOICE, CB_ADDSTRING, 0,
                            LPARAM(kVoiceTable[i].name));
    SendDlgItemMessageW(dlg, IDC_VOICE, CB_ADDSTRING, 0, LPARAM(kCustomVoiceName));
    SendDlgItemMessageW(dlg, IDC_VOICE, CB_SETCURSEL, 0, 0);

    for (const auto& pack : g_packs) {
        const wchar_t* label = L"";
        for (const auto& p : kPackLabels)
            if (pack == p.pack) { label = p.label; break; }
        SendDlgItemMessageW(dlg, IDC_LANG, CB_ADDSTRING, 0, LPARAM(label));
    }
    SendDlgItemMessageW(dlg, IDC_LANG, CB_SETCURSEL, 0, 0);
    for (const wchar_t* v : kVariantNames)
        SendDlgItemMessageW(dlg, IDC_VARIANT, CB_ADDSTRING, 0, LPARAM(v));
    SendDlgItemMessageW(dlg, IDC_VARIANT, CB_SETCURSEL, 0, 0);

    for (int spin : {IDC_RATE_SPIN, IDC_PITCH_SPIN, IDC_PMOD_SPIN,
                     IDC_BREATH_SPIN, IDC_VOLUME_SPIN, IDC_CLARITY_SPIN})
        SendDlgItemMessageW(dlg, spin, UDM_SETRANGE32, 0, 100);

    SetDlgItemTextW(dlg, IDC_TEXT,
                    L"She sells sea shells. His house has fish. Sixty-six.");
}

INT_PTR CALLBACK dialogProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            populate(dlg);
            loadIntoUi(dlg);
            if (g_installed.empty())
                setStatus(dlg, L"No Infovox languages are installed.");
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_VOICE:
                    if (HIWORD(wp) == CBN_SELCHANGE) loadIntoUi(dlg);
                    return TRUE;
                case IDC_LANG:
                case IDC_VARIANT:
                    if (HIWORD(wp) == CBN_SELCHANGE) saveFromUi(dlg);
                    return TRUE;
                case IDC_RATE: case IDC_PITCH: case IDC_PMOD:
                case IDC_BREATH: case IDC_VOLUME: case IDC_CLARITY:
                    if (HIWORD(wp) == EN_CHANGE && !g_loading)
                        setStatus(dlg, L"Changed; choose Apply to save.");
                    return TRUE;
                case IDC_SPEAK:    speakTest(dlg); return TRUE;
                case IDC_DEFAULTS: restoreDefaults(dlg); return TRUE;
                case IDC_SAVE:     saveFromUi(dlg); return TRUE;
                case IDCANCEL:     EndDialog(dlg, 0); return TRUE;
                default: break;
            }
            break;

        case WM_CLOSE:
            EndDialog(dlg, 0);
            return TRUE;
        default:
            break;
    }
    return FALSE;
}

}  // namespace

// pipe_client.cpp asks for this to locate the install directory.
namespace Infovox { namespace sapi {
HMODULE moduleHandle() { return g_instance; }
}}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int) {
    g_instance = inst;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_CONFIG), nullptr, dialogProc, 0);
    CoUninitialize();
    return 0;
}
