#pragma once

#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>
#include <shlobj.h>

namespace Infovox {
namespace sapi {

// Where the configuration utility and the engine meet: one INI file per user.
//
//   %APPDATA%\Infovox210\settings.ini
//
//     [CustomVoice]   the "Infovox 210 Custom Voice" token: language, voice
//                     variant and every parameter
//     [Voice.<id>]    per-voice values for the parameters SAPI has no slider
//                     for (pitch modulation, breathiness, consonant clarity)
//
// None of it is in the registry.  The COM class and the voice tokens still
// are, because that is the only place SAPI looks for voices, but the installer
// writes those once; nothing a user sets is kept there, and saving it needs no
// elevation.
//
// Everything is a 0..100 slider where 0 is the engine minimum and 100 the
// engine maximum, so the extremes of a screen reader's own sliders reach the
// extremes of the synthesizer.
constexpr wchar_t kSettingsFolder[] = L"Infovox210";
constexpr wchar_t kSettingsFile[] = L"settings.ini";
constexpr wchar_t kGeneralSection[] = L"General";
constexpr wchar_t kCustomVoiceSection[] = L"CustomVoice";
constexpr wchar_t kVoiceSectionPrefix[] = L"Voice.";

// Where 1.6.0 and earlier kept the same values; see legacySettings().
constexpr wchar_t kLegacySettingsKey[] = L"Software\\Infovox210";

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

// ---------------------------------------------------------------------------
// The file.  Read and written here rather than through the profile API so
// that a save can replace the whole file with one rename: the settings dialog
// saves while a screen reader may be speaking, and the engine must never read
// half a file.
// ---------------------------------------------------------------------------

struct IniEntry {
    std::wstring key;
    std::wstring value;
};

struct IniSection {
    std::wstring name;
    std::vector<IniEntry> entries;
};

// Sections and keys in file order.  Comments are not kept: every save writes
// the file afresh under its own header.
using IniDocument = std::vector<IniSection>;

inline std::wstring iniTrim(const std::wstring& s) {
    const size_t first = s.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return std::wstring();
    const size_t last = s.find_last_not_of(L" \t\r\n");
    return s.substr(first, last - first + 1);
}

inline IniDocument iniParse(const std::wstring& text) {
    IniDocument doc;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        const std::wstring line = iniTrim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line[0] == L'[') {
            const size_t close = line.find(L']');
            const size_t len = close == std::wstring::npos ? std::wstring::npos : close - 1;
            doc.push_back({iniTrim(line.substr(1, len)), {}});
            continue;
        }
        const size_t eq = line.find(L'=');
        // A value before the first section header belongs to nothing.
        if (eq == std::wstring::npos || doc.empty()) continue;
        doc.back().entries.push_back({iniTrim(line.substr(0, eq)),
                                      iniTrim(line.substr(eq + 1))});
    }
    return doc;
}

inline const std::wstring* iniFind(const IniDocument& doc, const wchar_t* section,
                                   const wchar_t* key) {
    for (const IniSection& s : doc) {
        if (_wcsicmp(s.name.c_str(), section) != 0) continue;
        for (const IniEntry& e : s.entries)
            if (_wcsicmp(e.key.c_str(), key) == 0) return &e.value;
    }
    return nullptr;
}

// Anything that starts with a number reads as that number, so a hand-edited
// "40 ; the default" is still 40.
inline bool iniReadInt(const IniDocument& doc, const wchar_t* section, const wchar_t* key,
                       int* out) {
    const std::wstring* v = iniFind(doc, section, key);
    if (!v) return false;
    wchar_t* end = nullptr;
    const long n = wcstol(v->c_str(), &end, 10);
    if (end == v->c_str()) return false;
    *out = int(n);
    return true;
}

inline void iniSet(IniDocument* doc, const wchar_t* section, const wchar_t* key,
                   const std::wstring& value) {
    IniSection* target = nullptr;
    for (IniSection& s : *doc) {
        if (_wcsicmp(s.name.c_str(), section) == 0) {
            target = &s;
            break;
        }
    }
    if (!target) {
        doc->push_back({section, {}});
        target = &doc->back();
    }
    for (IniEntry& e : target->entries) {
        if (_wcsicmp(e.key.c_str(), key) == 0) {
            e.value = value;
            return;
        }
    }
    target->entries.push_back({key, value});
}

inline void iniSetInt(IniDocument* doc, const wchar_t* section, const wchar_t* key, int v) {
    iniSet(doc, section, key, std::to_wstring(v));
}

// %APPDATA%\Infovox210\settings.ini, or empty if this profile has no roaming
// AppData folder.  Asked of the shell rather than taken from %APPDATA%: the
// engine runs inside whatever application is speaking, and the environment is
// that application's to change.  Worked out once per process.
inline const std::wstring& settingsPath() {
    static const std::wstring path = [] {
        std::wstring p;
        PWSTR base = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &base)) &&
            base) {
            p = base;
            p += L'\\';
            p += kSettingsFolder;
            p += L'\\';
            p += kSettingsFile;
        }
        CoTaskMemFree(base);
        return p;
    }();
    return path;
}

// Every share mode is granted, delete included, so a reader never stops the
// settings dialog from replacing the file.
inline bool readSettingsFile(const std::wstring& path, IniDocument* doc) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::string bytes;
    LARGE_INTEGER size{};
    // Far larger than anything written here; not a settings file.
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart <= 1024 * 1024;
    if (ok && size.QuadPart > 0) {
        bytes.resize(size_t(size.QuadPart));
        DWORD got = 0;
        ok = ReadFile(file, &bytes[0], DWORD(bytes.size()), &got, nullptr) &&
             got == bytes.size();
    }
    CloseHandle(file);
    if (!ok) return false;

    // UTF-8 as written, with or without a byte order mark, or UTF-16 if an
    // editor saved it that way.
    std::wstring text;
    if (bytes.size() >= 2 && BYTE(bytes[0]) == 0xFF && BYTE(bytes[1]) == 0xFE) {
        text.resize((bytes.size() - 2) / sizeof(wchar_t));
        if (!text.empty())
            memcpy(&text[0], bytes.data() + 2, text.size() * sizeof(wchar_t));
    } else {
        const size_t skip = bytes.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
        const int n = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + skip,
                                          int(bytes.size() - skip), nullptr, 0);
        if (n > 0) {
            text.resize(size_t(n));
            MultiByteToWideChar(CP_UTF8, 0, bytes.data() + skip, int(bytes.size() - skip),
                                &text[0], n);
        }
    }
    *doc = iniParse(text);
    return true;
}

inline std::wstring iniSerialize(const IniDocument& doc) {
    std::wstring text =
        L"; Infovox 210 SAPI5 settings.\r\n"
        L";\r\n"
        L"; Written by Infovox 210 Settings, and safe to edit by hand: the engine\r\n"
        L"; reads this file at the start of every utterance, so a saved change is\r\n"
        L"; heard on the next thing spoken.\r\n"
        L";\r\n"
        L"; Every parameter is 0 to 100, where 0 is the engine's minimum and 100 its\r\n"
        L"; maximum.  [CustomVoice] is the Infovox 210 Custom Voice; its Voice is a\r\n"
        L"; voice ID such as AM01 (American English, male) or SW02 (Swedish, female).\r\n"
        L"; [Voice.<ID>] holds one voice's pitch modulation, breathiness and\r\n"
        L"; consonant clarity; its rate, pitch and volume come from SAPI.\r\n";
    for (const IniSection& s : doc) {
        text += L"\r\n[" + s.name + L"]\r\n";
        for (const IniEntry& e : s.entries) text += e.key + L"=" + e.value + L"\r\n";
    }
    return text;
}

// Written beside the real file and renamed over it, so a reader sees either
// the old settings or the new ones and never half a file.  With `replace`
// false an existing file is left as it is, which is what the move out of the
// registry wants when another process has got there first.
inline bool writeSettingsFile(const std::wstring& path, const IniDocument& doc, bool replace) {
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) return false;
    CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);

    const std::wstring text = iniSerialize(doc);
    std::string bytes;
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0,
                                      nullptr, nullptr);
    if (n > 0) {
        bytes.resize(size_t(n));
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), &bytes[0], n,
                            nullptr, nullptr);
    }

    // Named for this process, so that two processes moving the old settings
    // out of the registry at once cannot write into each other's copy.
    const std::wstring temp =
        path + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = WriteFile(file, bytes.data(), DWORD(bytes.size()), &wrote, nullptr) &&
              wrote == bytes.size();
    ok = FlushFileBuffers(file) && ok;
    CloseHandle(file);

    // A reader that has the file open at this instant makes the replace fail,
    // and it only holds it for one read, so try again shortly.
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
    for (int attempt = 0; ok && attempt < 20; ++attempt) {
        if (MoveFileExW(temp.c_str(), path.c_str(), flags)) return true;
        const DWORD err = GetLastError();
        if (err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION) break;
        Sleep(10);
    }
    DeleteFileW(temp.c_str());
    return false;
}

// ---------------------------------------------------------------------------
// Settings saved by 1.6.0 and earlier, under HKCU\Software\Infovox210.
// ---------------------------------------------------------------------------

inline bool readLegacyDword(HKEY key, const wchar_t* name, int* out) {
    DWORD type = 0, val = 0, size = sizeof val;
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&val), &size) !=
            ERROR_SUCCESS ||
        type != REG_DWORD)
        return false;
    *out = int(val);
    return true;
}

inline bool readLegacySettings(IniDocument* doc) {
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kLegacySettingsKey, 0, KEY_READ, &root) !=
        ERROR_SUCCESS)
        return false;
    int v = 0;
    if (readLegacyDword(root, L"Logging", &v))
        iniSetInt(doc, kGeneralSection, L"Logging", v != 0 ? 1 : 0);

    HKEY custom = nullptr;
    if (RegOpenKeyExW(root, L"CustomVoice", 0, KEY_READ, &custom) == ERROR_SUCCESS) {
        wchar_t buf[256] = {0};
        DWORD type = 0, size = sizeof buf - sizeof buf[0];   // keeps the terminator
        if (RegQueryValueExW(custom, L"Voice", nullptr, &type, reinterpret_cast<BYTE*>(buf),
                             &size) == ERROR_SUCCESS &&
            type == REG_SZ && wcslen(buf) == 4)
            iniSet(doc, kCustomVoiceSection, L"Voice", buf);
        for (const wchar_t* name : {L"Rate", L"Pitch", L"PitchMod", L"Breath", L"Volume",
                                    L"Clarity"})
            if (readLegacyDword(custom, name, &v))
                iniSetInt(doc, kCustomVoiceSection, name, clamp100(v));
        RegCloseKey(custom);
    }

    HKEY voices = nullptr;
    if (RegOpenKeyExW(root, L"Voices", 0, KEY_READ, &voices) == ERROR_SUCCESS) {
        wchar_t id[256];
        for (DWORD i = 0;; ++i) {
            DWORD len = ARRAYSIZE(id);
            if (RegEnumKeyExW(voices, i, id, &len, nullptr, nullptr, nullptr, nullptr) !=
                ERROR_SUCCESS)
                break;
            HKEY voice = nullptr;
            if (RegOpenKeyExW(voices, id, 0, KEY_READ, &voice) != ERROR_SUCCESS) continue;
            const std::wstring section = std::wstring(kVoiceSectionPrefix) + id;
            for (const wchar_t* name : {L"PitchMod", L"Breath", L"Clarity"})
                if (readLegacyDword(voice, name, &v))
                    iniSetInt(doc, section.c_str(), name, clamp100(v));
            RegCloseKey(voice);
        }
        RegCloseKey(voices);
    }
    RegCloseKey(root);
    return true;
}

// Moves the registry settings into the file, once per process.  Whichever runs
// first after an upgrade -- the engine or the dialog -- does it, and the key is
// deleted only once the file exists, so nothing can be lost on the way.
//
// Returns only what could not be moved: a low-integrity process cannot write
// to AppData, and goes on speaking with the registry values instead.  Anything
// that was moved is read back from the file like any other setting, so
// deleting the file later resets to the defaults rather than bringing the old
// values back.
inline const IniDocument& legacySettings(const std::wstring& path) {
    static std::once_flag once;
    static IniDocument unmoved;
    std::call_once(once, [&path] {
        IniDocument found;
        if (!readLegacySettings(&found)) return;
        writeSettingsFile(path, found, /*replace=*/false);
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
            RegDeleteTreeW(HKEY_CURRENT_USER, kLegacySettingsKey);
        else
            unmoved = found;
    });
    return unmoved;
}

// The file if there is one, and otherwise whatever an older version left in
// the registry.  False only when the file exists but could not be read: a save
// must not go ahead then, or it would write every other voice's values away.
inline bool readSettings(IniDocument* doc) {
    doc->clear();
    const std::wstring& path = settingsPath();
    if (path.empty()) return true;
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        const IniDocument& unmoved = legacySettings(path);
        // Looked for again: moving the old settings may just have created it,
        // here or in another process.
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            *doc = unmoved;
            return true;
        }
    }
    return readSettingsFile(path, doc);
}

// Read in full every time, so a change in the configuration utility takes
// effect on the next thing spoken without restarting the screen reader.
inline IniDocument loadSettings() {
    IniDocument doc;
    readSettings(&doc);
    return doc;
}

inline bool writeSettings(const IniDocument& doc) {
    const std::wstring& path = settingsPath();
    return !path.empty() && writeSettingsFile(path, doc, /*replace=*/true);
}

// ---------------------------------------------------------------------------
// What the engine and the dialog ask for.
// ---------------------------------------------------------------------------

inline CustomVoiceSettings loadCustomVoice() {
    const IniDocument doc = loadSettings();
    CustomVoiceSettings s;
    const std::wstring* v = iniFind(doc, kCustomVoiceSection, L"Voice");
    if (v && v->size() == 4) {
        s.voiceId.clear();
        for (wchar_t c : *v) s.voiceId.push_back(char(c));   // voice IDs are ASCII
    }
    int t;
    if (iniReadInt(doc, kCustomVoiceSection, L"Rate", &t))     s.rate = clamp100(t);
    if (iniReadInt(doc, kCustomVoiceSection, L"Pitch", &t))    s.pitch = clamp100(t);
    if (iniReadInt(doc, kCustomVoiceSection, L"PitchMod", &t)) s.pitchMod = clamp100(t);
    if (iniReadInt(doc, kCustomVoiceSection, L"Breath", &t))   s.breath = clamp100(t);
    if (iniReadInt(doc, kCustomVoiceSection, L"Volume", &t))   s.volume = clamp100(t);
    if (iniReadInt(doc, kCustomVoiceSection, L"Clarity", &t))  s.clarity = clamp100(t);
    return s;
}

inline bool saveCustomVoice(const CustomVoiceSettings& s) {
    IniDocument doc;
    if (!readSettings(&doc)) return false;
    iniSet(&doc, kCustomVoiceSection, L"Voice",
           std::wstring(s.voiceId.begin(), s.voiceId.end()));
    iniSetInt(&doc, kCustomVoiceSection, L"Rate", s.rate);
    iniSetInt(&doc, kCustomVoiceSection, L"Pitch", s.pitch);
    iniSetInt(&doc, kCustomVoiceSection, L"PitchMod", s.pitchMod);
    iniSetInt(&doc, kCustomVoiceSection, L"Breath", s.breath);
    iniSetInt(&doc, kCustomVoiceSection, L"Volume", s.volume);
    iniSetInt(&doc, kCustomVoiceSection, L"Clarity", s.clarity);
    return writeSettings(doc);
}

// Pitch modulation and breathiness for an ordinary voice: SAPI has no slider
// for either, so the configuration utility is the only place they can be set.
struct VoiceTweaks {
    int pitchMod = -1;    // -1 keeps the voice's own default
    int breath = 0;
    int clarity = 40;     // 0 = the engine's own band-limited output
};

inline std::wstring voiceSection(const std::string& voiceId) {
    return std::wstring(kVoiceSectionPrefix) + std::wstring(voiceId.begin(), voiceId.end());
}

inline VoiceTweaks loadVoiceTweaks(const std::string& voiceId) {
    const IniDocument doc = loadSettings();
    const std::wstring section = voiceSection(voiceId);
    VoiceTweaks t;
    int v;
    if (iniReadInt(doc, section.c_str(), L"PitchMod", &v)) t.pitchMod = clamp100(v);
    if (iniReadInt(doc, section.c_str(), L"Breath", &v))   t.breath = clamp100(v);
    if (iniReadInt(doc, section.c_str(), L"Clarity", &v))  t.clarity = clamp100(v);
    return t;
}

inline bool saveVoiceTweaks(const std::string& voiceId, const VoiceTweaks& t) {
    if (voiceId.empty()) return false;
    IniDocument doc;
    if (!readSettings(&doc)) return false;
    const std::wstring section = voiceSection(voiceId);
    iniSetInt(&doc, section.c_str(), L"PitchMod", t.pitchMod);
    iniSetInt(&doc, section.c_str(), L"Breath", t.breath);
    iniSetInt(&doc, section.c_str(), L"Clarity", t.clarity);
    return writeSettings(doc);
}

}  // namespace sapi
}  // namespace Infovox
