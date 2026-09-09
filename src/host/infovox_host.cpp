// Infovox 210 worker.
//
// Owns the PowerPC emulator and serves the SAPI engines over a named pipe.
// Several pipe instances listen from startup so a client never finds the name
// missing; each connection gets its own engine, so one application speaking a
// long passage cannot stall another.
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "../ppc/infovox.h"
#include "../sapi/pipe_protocol.h"

namespace {

std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L"\\/");
    return s == std::wstring::npos ? L"." : p.substr(0, s);
}

std::atomic<bool> g_shutdown{false};

// A connection's I/O is overlapped so a CMD_STOP can be read while audio is
// still being written back: a synchronous handle would serialise the two and
// the cancel would not arrive until the utterance had finished.
class Connection {
public:
    explicit Connection(HANDLE pipe) : pipe_(pipe) {
        readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    ~Connection() {
        if (readEvent_) CloseHandle(readEvent_);
        if (writeEvent_) CloseHandle(writeEvent_);
    }

    bool writeAll(const void* data, size_t len) {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        while (len) {
            OVERLAPPED ov{};
            ov.hEvent = writeEvent_;
            ResetEvent(writeEvent_);
            DWORD wrote = 0;
            DWORD chunk = DWORD(len > 0x10000 ? 0x10000 : len);
            if (!WriteFile(pipe_, p, chunk, &wrote, &ov)) {
                if (GetLastError() != ERROR_IO_PENDING) return false;
                if (!GetOverlappedResult(pipe_, &ov, &wrote, TRUE)) return false;
            }
            if (wrote == 0) return false;
            p += wrote;
            len -= wrote;
        }
        return true;
    }

    bool send(uint32_t type, const void* payload, size_t len) {
        PipeHeader h{type, uint32_t(len)};
        if (!writeAll(&h, sizeof h)) return false;
        return len == 0 || writeAll(payload, len);
    }

    // Starts (or continues) an overlapped read of the next header.
    bool pollCommand(uint32_t* type, std::vector<uint8_t>* payload, bool wait) {
        if (!readPending_) {
            ResetEvent(readEvent_);
            readOv_ = OVERLAPPED{};
            readOv_.hEvent = readEvent_;
            DWORD got = 0;
            if (!ReadFile(pipe_, &pendingHeader_, sizeof pendingHeader_, &got, &readOv_)) {
                DWORD e = GetLastError();
                if (e != ERROR_IO_PENDING) return false;
                readPending_ = true;
            } else {
                return finishHeader(type, payload);
            }
        }
        DWORD got = 0;
        if (!GetOverlappedResult(pipe_, &readOv_, &got, wait ? TRUE : FALSE)) {
            DWORD e = GetLastError();
            if (!wait && e == ERROR_IO_INCOMPLETE) { *type = 0; return true; }
            return false;
        }
        readPending_ = false;
        return finishHeader(type, payload);
    }

    HANDLE handle() const { return pipe_; }

private:
    bool finishHeader(uint32_t* type, std::vector<uint8_t>* payload) {
        *type = pendingHeader_.type;
        payload->resize(pendingHeader_.size);
        size_t left = pendingHeader_.size;
        uint8_t* p = payload->data();
        while (left) {
            OVERLAPPED ov{};
            ov.hEvent = readEvent_;
            ResetEvent(readEvent_);
            DWORD got = 0;
            if (!ReadFile(pipe_, p, DWORD(left), &got, &ov)) {
                if (GetLastError() != ERROR_IO_PENDING) return false;
                if (!GetOverlappedResult(pipe_, &ov, &got, TRUE)) return false;
            }
            if (got == 0) return false;
            p += got;
            left -= got;
        }
        return true;
    }

    HANDLE pipe_;
    HANDLE readEvent_ = nullptr;
    HANDLE writeEvent_ = nullptr;
    OVERLAPPED readOv_{};
    PipeHeader pendingHeader_{};
    bool readPending_ = false;
};

void sendError(Connection& c, const std::string& msg) {
    c.send(RESP_ERROR, msg.data(), msg.size());
}

void serveVoices(Connection& c, ppc::InfovoxEngine& eng) {
    // count, then per voice: id[4], variant, gender, age, language, region,
    // version, packLen + pack, nameLen + name (both UTF-8)
    std::vector<uint8_t> out;
    auto put32 = [&](uint32_t v) {
        out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8));
        out.push_back(uint8_t(v >> 16)); out.push_back(uint8_t(v >> 24));
    };
    auto putStr = [&](const std::string& s) {
        put32(uint32_t(s.size()));
        out.insert(out.end(), s.begin(), s.end());
    };
    // Mac Roman -> UTF-8 so voice names survive the trip.
    auto macToUtf8 = [](const std::string& s) {
        if (s.empty()) return std::string();
        int wl = MultiByteToWideChar(10000, 0, s.c_str(), int(s.size()), nullptr, 0);
        std::wstring w(wl, 0);
        MultiByteToWideChar(10000, 0, s.c_str(), int(s.size()), &w[0], wl);
        int ul = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wl, nullptr, 0, nullptr, nullptr);
        std::string u(ul, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), wl, &u[0], ul, nullptr, nullptr);
        return u;
    };

    put32(uint32_t(eng.voices().size()));
    for (const auto& v : eng.voices()) {
        char id[4] = {' ', ' ', ' ', ' '};
        for (size_t i = 0; i < 4 && i < v.id.size(); ++i) id[i] = v.id[i];
        out.insert(out.end(), id, id + 4);
        put32(uint32_t(v.variant));
        put32(uint32_t(v.gender));
        put32(uint32_t(v.age));
        put32(uint32_t(v.language));
        put32(uint32_t(v.region));
        put32(uint32_t(v.version));
        putStr(v.pack);
        putStr(macToUtf8(v.name));
    }
    c.send(RESP_VOICES, out.data(), out.size());
}

void serveSpeak(Connection& c, ppc::InfovoxEngine& eng,
                const std::vector<uint8_t>& payload) {
    if (payload.size() < sizeof(SpeakCommand)) { sendError(c, "short speak command"); return; }
    SpeakCommand cmd;
    memcpy(&cmd, payload.data(), sizeof cmd);
    if (payload.size() < sizeof cmd + cmd.textLength) { sendError(c, "short speak text"); return; }
    std::string text(reinterpret_cast<const char*>(payload.data() + sizeof cmd),
                     cmd.textLength);

    std::string voice(cmd.voiceId, 4);
    ppc::InfovoxParams p;
    p.rate = cmd.rate; p.pitch = cmd.pitch; p.pitchMod = cmd.pitchMod;
    p.breath = cmd.breath; p.volume = cmd.volume; p.clarity = cmd.clarity;
    p.spellOut = cmd.spellOut != 0;
    p.literalNumbers = cmd.literalNumbers != 0;
    eng.setParams(p);

    std::string err;
    if (voice != eng.currentVoice() && !eng.setVoice(voice, &err)) {
        sendError(c, err);
        return;
    }

    bool aborted = false;
    std::vector<uint8_t> chunk;
    auto sink = [&](const int16_t* frames, size_t count) {
        chunk.resize(count * 2);
        memcpy(chunk.data(), frames, chunk.size());
        return c.send(RESP_AUDIO, chunk.data(), chunk.size());
    };
    auto cancelled = [&]() {
        if (aborted) return true;
        uint32_t type = 0;
        std::vector<uint8_t> pl;
        if (!c.pollCommand(&type, &pl, /*wait=*/false)) { aborted = true; return true; }
        if (type == CMD_STOP) { aborted = true; return true; }
        return false;
    };

    if (!eng.render(text, sink, cancelled, &err)) {
        sendError(c, err);
        return;
    }
    // Word and phoneme boundaries for SAPI events.
    std::vector<EventRecord> evs;
    for (const auto& e : eng.events()) {
        EventRecord r;
        r.kind = e.kind == ppc::InfovoxEvent::kWord ? 0u : 1u;
        r.sampleOffset = e.sampleOffset;
        r.a = e.a; r.b = e.b;
        evs.push_back(r);
    }
    if (!evs.empty())
        c.send(RESP_EVENT, evs.data(), evs.size() * sizeof(EventRecord));
    c.send(RESP_END, nullptr, 0);
}

void serveClient(HANDLE pipe, const std::wstring& engineDir) {
    Connection c(pipe);
    ppc::InfovoxEngine eng;
    std::string err;
    bool ready = eng.init(engineDir, &err);

    for (;;) {
        uint32_t type = 0;
        std::vector<uint8_t> payload;
        if (!c.pollCommand(&type, &payload, /*wait=*/true)) break;
        if (type == 0) continue;
        if (!ready && type != CMD_HELLO && type != CMD_PING) {
            sendError(c, err.empty() ? "engine unavailable" : err);
            continue;
        }
        switch (type) {
            case CMD_HELLO: {
                HelloReply r{INFOVOX_PROTOCOL_VERSION,
                             ready ? eng.sampleRate() : INFOVOX_SAMPLE_RATE,
                             ready ? uint32_t(eng.voices().size()) : 0u,
                             INFOVOX_BITS_PER_SAMPLE, INFOVOX_CHANNELS};
                c.send(RESP_HELLO, &r, sizeof r);
                break;
            }
            case CMD_LIST_VOICES: serveVoices(c, eng); break;
            case CMD_SPEAK:       serveSpeak(c, eng, payload); break;
            case CMD_STOP:        break;      // nothing in flight
            case CMD_PING:        c.send(RESP_PONG, nullptr, 0); break;
            case CMD_SHUTDOWN:    g_shutdown = true; return;
            default:              sendError(c, "unknown command"); break;
        }
    }
    FlushFileBuffers(pipe);
    DisconnectNamedPipe(pipe);
}

void pipeThread(const std::wstring& engineDir) {
    while (!g_shutdown) {
        HANDLE pipe = CreateNamedPipeW(
            INFOVOX_PIPE_NAME,
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            INFOVOX_PIPE_INSTANCES, 64 * 1024, 64 * 1024, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(50);
            continue;
        }
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        BOOL connected = ConnectNamedPipe(pipe, &ov);
        DWORD e = GetLastError();
        if (!connected && e == ERROR_IO_PENDING) {
            DWORD dummy = 0;
            connected = GetOverlappedResult(pipe, &ov, &dummy, TRUE);
        } else if (!connected && e == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        }
        CloseHandle(ov.hEvent);
        if (!connected || g_shutdown) { CloseHandle(pipe); continue; }

        serveClient(pipe, engineDir);
        CloseHandle(pipe);
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::wstring dir = exeDir() + L"\\engine";
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--engine") == 0 && i + 1 < argc) dir = argv[++i];
    }

    // Fail loudly at startup rather than on the first utterance.
    {
        ppc::InfovoxEngine probe;
        std::string err;
        if (!probe.init(dir, &err)) {
            fwprintf(stderr, L"Infovox worker: engine unavailable (%hs)\n", err.c_str());
            return 1;
        }
    }

    std::vector<std::thread> threads;
    for (int i = 0; i < INFOVOX_PIPE_INSTANCES; ++i)
        threads.emplace_back(pipeThread, dir);
    for (auto& t : threads) t.join();
    return 0;
}
