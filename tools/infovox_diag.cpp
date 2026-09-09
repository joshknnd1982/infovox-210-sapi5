// Infovox 210 diagnostics: walks every registered voice through SAPI itself and
// writes a plain text report, for when a voice will not speak.
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>
#include <sapi.h>
#include <shlwapi.h>
#include <objbase.h>

#include "../src/sapi/pipe_client.h"
#include "../src/sapi/token_enum.hpp"
#include "../src/sapi/voice_registry.hpp"

using namespace Infovox::sapi;

namespace {

HMODULE g_self = nullptr;

std::string narrow(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string o(size_t(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), int(s.size()), &o[0], n, nullptr, nullptr);
    return o;
}

// Speaks into a memory stream so the check does not depend on an audio device.
bool speakToStream(ISpVoice* voice, const wchar_t* text, ULONG* bytes) {
    ISpStream* stream = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_ISpStream,
                                reinterpret_cast<void**>(&stream))))
        return false;
    WAVEFORMATEX wf{WAVE_FORMAT_PCM, INFOVOX_CHANNELS, INFOVOX_SAMPLE_RATE, 0, 2, 16, 0};
    wf.nBlockAlign = WORD(wf.nChannels * wf.wBitsPerSample / 8);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    bool ok = false;
    if (SUCCEEDED(stream->SetBaseStream(SHCreateMemStream(nullptr, 0),
                                        SPDFID_WaveFormatEx, &wf))) {
        if (SUCCEEDED(voice->SetOutput(stream, TRUE)) &&
            SUCCEEDED(voice->Speak(text, SPF_DEFAULT, nullptr))) {
            STATSTG st{};
            if (SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME))) {
                *bytes = ULONG(st.cbSize.QuadPart);
                ok = *bytes > 0;
            }
        }
    }
    stream->Release();
    return ok;
}

}  // namespace

namespace Infovox { namespace sapi {
HMODULE moduleHandle() { return g_self; }
}}

// Drives the worker directly, which is the half of the path that does not need
// the engine to be registered with SAPI.
int pipeSelfTest() {
    PipeClient client;
    std::vector<RemoteVoice> voices;
    if (!client.listVoices(&voices)) {
        printf("the worker did not answer\n");
        return 1;
    }
    printf("worker reports %zu voices\n", voices.size());

    int spoke = 0, silent = 0;
    for (const RemoteVoice& v : voices) {
        SpeakCommand cmd{};
        for (int i = 0; i < 4; ++i) cmd.voiceId[i] = v.id[i];
        cmd.rate = 15; cmd.pitch = 50; cmd.pitchMod = 25;
        cmd.breath = 0; cmd.volume = 100;
        size_t frames = 0;
        int16_t peak = 0;
        std::wstring err;
        bool ok = client.speak(cmd, "Infovox test one two three.",
                               [&](const int16_t* s, size_t n) {
                                   frames += n;
                                   for (size_t i = 0; i < n; ++i) {
                                       int16_t a = s[i] < 0 ? int16_t(-s[i]) : s[i];
                                       if (a > peak) peak = a;
                                   }
                                   return true;
                               },
                               nullptr, nullptr, &err);
        bool good = ok && frames > 4000 && peak > 1000;
        good ? ++spoke : ++silent;
        printf("  %.4s %-11s %6zu frames  peak %5d  %s\n", v.id.c_str(), v.pack.c_str(),
               frames, peak, good ? "OK" : "FAILED");
    }

    // A cancel must stop promptly and leave the connection usable.
    SpeakCommand cmd{};
    memcpy(cmd.voiceId, "AM01", 4);
    cmd.rate = 15; cmd.pitch = 50; cmd.pitchMod = 25; cmd.breath = 0; cmd.volume = 100;
    size_t got = 0;
    std::vector<int16_t> tail;
    std::wstring err;
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    client.speak(cmd,
                 "This is a long sentence that the caller abandons almost immediately, "
                 "which is what a screen reader does every time the user presses a key.",
                 [&](const int16_t* s2, size_t n) {
                     tail.insert(tail.end(), s2, s2 + n); got += n; return true;
                 },
                 [&]() { return got > 2048; }, nullptr, &err);
    QueryPerformanceCounter(&t1);
    double ms = 1000.0 * double(t1.QuadPart - t0.QuadPart) / double(f.QuadPart);
    printf("\ncancel after %zu frames took %.1f ms\n", got, ms);
    // The audio a cancel leaves behind must decay to zero, or the step to
    // silence is heard as a click every time the user presses a key.
    if (!tail.empty()) {
        int16_t last = tail.back();
        size_t n = tail.size();
        int16_t peakLast5ms = 0;
        size_t from = n > 110 ? n - 110 : 0;
        for (size_t i = from; i < n; ++i) {
            int16_t a = tail[i] < 0 ? int16_t(-tail[i]) : tail[i];
            if (a > peakLast5ms) peakLast5ms = a;
        }
        printf("cancel tail: last sample %d, peak over the final 5 ms %d -- %s\n",
               int(last), int(peakLast5ms),
               (last > -80 && last < 80) ? "decays to zero" : "STILL STEPPING");
        FILE* w = fopen("cancel_tail.wav", "wb");
        if (w) {
            uint32_t db = uint32_t(tail.size() * 2), riff = 36 + db;
            uint32_t rate = INFOVOX_SAMPLE_RATE, br = rate * 2, fl = 16;
            uint16_t one = 1, two = 2, bits = 16;
            fwrite("RIFF", 1, 4, w); fwrite(&riff, 4, 1, w); fwrite("WAVE", 1, 4, w);
            fwrite("fmt ", 1, 4, w); fwrite(&fl, 4, 1, w);
            fwrite(&one, 2, 1, w); fwrite(&one, 2, 1, w);
            fwrite(&rate, 4, 1, w); fwrite(&br, 4, 1, w);
            fwrite(&two, 2, 1, w); fwrite(&bits, 2, 1, w);
            fwrite("data", 1, 4, w); fwrite(&db, 4, 1, w);
            fwrite(tail.data(), 2, tail.size(), w);
            fclose(w);
        }
    }

    size_t after = 0;
    client.speak(cmd, "Still working.",
                 [&](const int16_t*, size_t n) { after += n; return true; },
                 nullptr, nullptr, &err);
    printf("utterance after the cancel: %zu frames %s\n", after,
           after > 4000 ? "OK" : "FAILED");

    // What arrowing through a document actually does: start, abandon almost
    // immediately, start again.  Every one of these must still end quietly.
    int worstTail = 0, rounds = 30, stalled = 0;
    for (int k = 0; k < rounds; ++k) {
        std::vector<int16_t> t;
        size_t seen = 0;
        client.speak(cmd, "Another line of text that will not be finished.",
                     [&](const int16_t* s2, size_t n) {
                         t.insert(t.end(), s2, s2 + n); seen += n; return true;
                     },
                     [&]() { return seen > 1024; }, nullptr, &err);
        if (t.empty()) { ++stalled; continue; }
        int last = t.back() < 0 ? -t.back() : t.back();
        if (last > worstTail) worstTail = last;
    }
    printf("%d rapid cancels: worst ending sample %d, %d produced nothing -- %s\n",
           rounds, worstTail, stalled,
           (worstTail < 200 && stalled == 0) ? "no step left to click" : "FAILED");

    size_t healthy = 0;
    client.speak(cmd, "Everything still works after all that.",
                 [&](const int16_t*, size_t n) { healthy += n; return true; },
                 nullptr, nullptr, &err);
    printf("utterance after 30 cancels: %zu frames %s\n", healthy,
           healthy > 4000 ? "OK" : "FAILED");

    printf("\n%d voices spoke, %d silent\n", spoke, silent);
    return silent == 0 && after > 4000 ? 0 : 1;
}

int main(int argc, char** argv) {
    g_self = GetModuleHandleW(nullptr);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc > 1 && strcmp(argv[1], "--pipe-test") == 0) {
        int r = pipeSelfTest();
        CoUninitialize();
        return r;
    }

    std::wstring reportPath;
    {
        wchar_t dir[MAX_PATH] = {0};
        DWORD n = GetEnvironmentVariableW(L"TEMP", dir, MAX_PATH);
        reportPath = (n ? std::wstring(dir) : L".") + L"\\Infovox210Diagnostics.txt";
    }
    FILE* out = _wfopen(reportPath.c_str(), L"w");
    if (!out) { printf("cannot write the report\n"); return 1; }

    auto emit = [&](const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt); vfprintf(out, fmt, ap); va_end(ap);
        va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    };

    emit("Infovox 210 SAPI5 diagnostics\n");
    emit("install directory: %s\n", narrow(installDir()).c_str());

    emit("\n-- engine packs --\n");
    int packs = 0;
    for (const auto& p : kPackLabels) {
        bool have = packInstalled(p.pack);
        packs += have ? 1 : 0;
        emit("  %-12s %s\n", p.pack, have ? "installed" : "not installed");
    }

    emit("\n-- worker --\n");
    PipeClient client;
    std::vector<RemoteVoice> remote;
    if (client.listVoices(&remote)) {
        emit("  worker answered, %zu voices available\n", remote.size());
    } else {
        emit("  the worker did not answer; infovox_host.exe may be missing\n");
    }

    emit("\n-- SAPI voices --\n");
    ISpVoice* voice = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice,
                                reinterpret_cast<void**>(&voice)))) {
        emit("  SAPI could not be started\n");
        fclose(out);
        return 1;
    }

    int ok = 0, bad = 0;
    IEnumSpObjectTokens* en = nullptr;
    if (SUCCEEDED(enumVoiceTokens(&en)) && en) {
        ISpObjectToken* tok = nullptr;
        while (en->Next(1, &tok, nullptr) == S_OK && tok) {
            LPWSTR id = nullptr;
            std::wstring sid;
            if (SUCCEEDED(tok->GetId(&id)) && id) { sid = id; CoTaskMemFree(id); }
            if (sid.find(L"Infovox210_") != std::wstring::npos) {
                std::wstring name = tokenDescription(tok);
                ULONG bytes = 0;
                bool spoke = SUCCEEDED(voice->SetVoice(tok)) &&
                             speakToStream(voice, L"Infovox test one two three.", &bytes);
                emit("  %-44s %s (%lu bytes)\n", narrow(name).c_str(),
                     spoke ? "OK" : "NO AUDIO", bytes);
                spoke ? ++ok : ++bad;
            }
            tok->Release();
            tok = nullptr;
        }
        en->Release();
    }
    voice->Release();

    emit("\n%d voices spoke, %d did not, %d language packs installed\n", ok, bad, packs);
    fclose(out);
    wprintf(L"\nreport written to %s\n", reportPath.c_str());
    CoUninitialize();
    return bad == 0 ? 0 : 1;
}
