// Command line driver for the Infovox 210 engine: lists voices, renders WAV
// files and sweeps parameters.  Used to verify the emulator outside SAPI.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <windows.h>

#include "../src/ppc/infovox.h"

namespace {

std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}

bool writeWav(const std::string& path, const std::vector<int16_t>& pcm, uint32_t rate) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t dataBytes = uint32_t(pcm.size() * 2);
    uint32_t riff = 36 + dataBytes;
    uint32_t byteRate = rate * 2;
    uint16_t one = 1, two = 2, bits = 16;
    uint32_t fmtLen = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&two, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    if (!pcm.empty()) fwrite(pcm.data(), 2, pcm.size(), f);
    fclose(f);
    return true;
}

// UTF-8 (or plain ASCII) -> Mac Roman, which is what the engine reads.
std::string toMacRoman(const std::string& in) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, in.c_str(), int(in.size()), nullptr, 0);
    std::wstring w(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, in.c_str(), int(in.size()), &w[0], wlen);
    int len = WideCharToMultiByte(10000, 0, w.c_str(), wlen, nullptr, 0, nullptr, nullptr);
    std::string out(len, 0);
    WideCharToMultiByte(10000, 0, w.c_str(), wlen, &out[0], len, nullptr, nullptr);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    std::wstring dir = exeDir() + L"\\engine";
    ppc::InfovoxEngine eng;
    std::string err;
    if (!eng.init(dir, &err)) {
        fprintf(stderr, "init failed: %s\n", err.c_str());
        return 1;
    }

    if (argc >= 2 && strcmp(argv[1], "list") == 0) {
        printf("%-6s %-11s %-7s %5s  %s\n", "id", "pack", "gender", "ver", "name");
        for (const auto& v : eng.voices()) {
            const char* g = v.gender == 1 ? "male" : v.gender == 2 ? "female" : "neuter";
            printf("%-6s %-11s %-7s %5d  %s\n", v.id.c_str(), v.pack.c_str(), g,
                   v.version, v.name.c_str());
        }
        printf("%zu voices\n", eng.voices().size());
        // Load every language once so the reported peak heap figure is real.
        for (const auto& v : eng.voices()) {
            if (v.variant != 1) continue;
            std::string e2;
            if (!eng.setVoice(v.id, &e2)) continue;
            std::vector<int16_t> junk;
            eng.render("test", [&](const int16_t*, size_t n) { junk.resize(n); return true; },
                       nullptr, &e2);
        }
        printf("emulated heap after loading all languages: %u bytes\n", eng.heapUsed());
        return 0;
    }

    if (argc >= 3 && strcmp(argv[1], "phonemes") == 0) {
        if (!eng.setVoice(argv[2], &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
        const auto& sym = eng.phonemeSymbols();
        printf("%s: %zu phoneme opcodes\n", argv[2], sym.size());
        for (size_t i = 0; i < sym.size(); ++i)
            if (!sym[i].empty()) printf("%3zu %s\n", i, sym[i].c_str());
        return 0;
    }

    // speak <voice> <out.wav> [rate pitch pitchmod breath volume] <text...>
    if (argc >= 4 && strcmp(argv[1], "speak") == 0) {
        std::string voice = argv[2], out = argv[3];
        int arg = 4;
        ppc::InfovoxParams p;
        if (argc >= 9 && argv[4][0] >= '0' && argv[4][0] <= '9') {
            p.rate = atoi(argv[4]); p.pitch = atoi(argv[5]);
            p.pitchMod = atoi(argv[6]); p.breath = atoi(argv[7]);
            p.volume = atoi(argv[8]);
            arg = 9;
            if (argc >= 10 && argv[9][0] >= '0' && argv[9][0] <= '9') {
                p.clarity = atoi(argv[9]);
                arg = 10;
            }
        }
        std::string text;
        for (int i = arg; i < argc; ++i) { if (!text.empty()) text += " "; text += argv[i]; }
        if (text.empty()) text = "Hello.";

        eng.setParams(p);
        if (!eng.setVoice(voice, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }

        std::vector<int16_t> pcm;
        LARGE_INTEGER f, t0, t1;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&t0);
        bool ok = eng.render(toMacRoman(text),
                             [&](const int16_t* s, size_t n) {
                                 pcm.insert(pcm.end(), s, s + n); return true;
                             },
                             nullptr, &err);
        QueryPerformanceCounter(&t1);
        if (!ok) { fprintf(stderr, "render failed: %s\n", err.c_str()); return 1; }
        double secs = double(pcm.size()) / eng.sampleRate();
        double took = double(t1.QuadPart - t0.QuadPart) / double(f.QuadPart);
        writeWav(out, pcm, eng.sampleRate());
        printf("%s: %.2fs audio in %.3fs (%.1fx realtime), %zu events -> %s\n",
               voice.c_str(), secs, took, took > 0 ? secs / took : 0,
               eng.events().size(), out.c_str());
        if (getenv("INFOVOX_EVENTS")) {
            const auto& sym = eng.phonemeSymbols();
            for (const auto& e : eng.events()) {
                double t = double(e.sampleOffset) / double(eng.sampleRate());
                if (e.kind == ppc::InfovoxEvent::kPhoneme) {
                    const char* s = (e.a >= 0 && size_t(e.a) < sym.size())
                                        ? sym[e.a].c_str() : "?";
                    printf("  %7.3f  phoneme %-4s (%d)\n", t, s, e.a);
                } else {
                    printf("  %7.3f  word at byte %d, %d bytes\n", t, e.a, e.b);
                }
            }
        }
        return 0;
    }

    fprintf(stderr,
            "usage:\n"
            "  infovox_probe list\n"
            "  infovox_probe speak <voiceId> <out.wav> [rate pitch pmod breath vol] <text>\n");
    return 2;
}
