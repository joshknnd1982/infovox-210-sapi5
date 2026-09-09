#include "pipe_client.h"

#include <cstring>

#include <shlwapi.h>

namespace Infovox {
namespace sapi {

// Defined in sapi_main.cpp (the dll) or the tool that links this file.
HMODULE moduleHandle();

std::wstring installDir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(moduleHandle(), buf, MAX_PATH);
    std::wstring p(buf);
    size_t s = p.find_last_of(L"\\/");
    if (s == std::wstring::npos) return L".\\";
    p.erase(s + 1);
    // The 64-bit dll lives in an x64 subdirectory; the worker sits beside the
    // 32-bit one, one level up.
    std::wstring parent = p;
    if (parent.size() > 5) {
        std::wstring tail = parent.substr(parent.size() - 5);
        if (_wcsicmp(tail.c_str(), L"\\x64\\") == 0)
            parent.erase(parent.size() - 4);
    }
    if (PathFileExistsW((parent + L"infovox_host.exe").c_str())) return parent;
    return p;
}

PipeClient::PipeClient() {
    readEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    writeEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
}

PipeClient::~PipeClient() {
    disconnect();
    if (readEvent_) CloseHandle(readEvent_);
    if (writeEvent_) CloseHandle(writeEvent_);
}

void PipeClient::disconnect() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool PipeClient::startWorker() {
    std::wstring exe = installDir() + L"infovox_host.exe";
    if (!PathFileExistsW(exe.c_str())) return false;

    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\"";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(0);
    std::wstring dir = installDir();
    if (!CreateProcessW(exe.c_str(), mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool PipeClient::ensureConnected() {
    if (pipe_ != INVALID_HANDLE_VALUE) return true;

    for (int attempt = 0; attempt < 60; ++attempt) {
        pipe_ = CreateFileW(INFOVOX_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            if (handshake()) return true;
            disconnect();
            // A worker from an older install answers with a different protocol
            // version; replace it rather than speaking to it.
            shutdownWorker();
            Sleep(100);
        }
        if (attempt == 0 && !startWorker()) return false;
        Sleep(50);
    }
    return false;
}

bool PipeClient::writeAll(const void* data, size_t len) {
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
        if (!wrote) return false;
        p += wrote;
        len -= wrote;
    }
    return true;
}

bool PipeClient::readAll(void* data, size_t len, DWORD timeoutMs) {
    uint8_t* p = static_cast<uint8_t*>(data);
    while (len) {
        OVERLAPPED ov{};
        ov.hEvent = readEvent_;
        ResetEvent(readEvent_);
        DWORD got = 0;
        if (!ReadFile(pipe_, p, DWORD(len), &got, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) return false;
            DWORD w = WaitForSingleObject(readEvent_, timeoutMs);
            if (w != WAIT_OBJECT_0) {
                CancelIo(pipe_);
                return false;
            }
            if (!GetOverlappedResult(pipe_, &ov, &got, FALSE)) return false;
        }
        if (!got) return false;
        p += got;
        len -= got;
    }
    return true;
}

bool PipeClient::sendCommand(uint32_t type, const void* payload, size_t len) {
    PipeHeader h{type, uint32_t(len)};
    if (!writeAll(&h, sizeof h)) return false;
    return len == 0 || writeAll(payload, len);
}

bool PipeClient::readHeader(PipeHeader* h, DWORD timeoutMs) {
    return readAll(h, sizeof *h, timeoutMs);
}

bool PipeClient::handshake() {
    if (!sendCommand(CMD_HELLO, nullptr, 0)) return false;
    PipeHeader h{};
    if (!readHeader(&h, 10000)) return false;
    if (h.type != RESP_HELLO || h.size < sizeof(HelloReply)) return false;
    std::vector<uint8_t> pl(h.size);
    if (!readAll(pl.data(), pl.size(), 10000)) return false;
    HelloReply r{};
    memcpy(&r, pl.data(), sizeof r);
    if (r.protocol != INFOVOX_PROTOCOL_VERSION) return false;
    sampleRate_ = r.sampleRate ? r.sampleRate : INFOVOX_SAMPLE_RATE;
    return true;
}

bool PipeClient::listVoices(std::vector<RemoteVoice>* out) {
    if (!ensureConnected()) return false;
    if (!sendCommand(CMD_LIST_VOICES, nullptr, 0)) return false;
    PipeHeader h{};
    if (!readHeader(&h, 10000) || h.type != RESP_VOICES) return false;
    std::vector<uint8_t> pl(h.size);
    if (!readAll(pl.data(), pl.size(), 10000)) return false;

    size_t o = 0;
    auto get32 = [&](uint32_t* v) {
        if (o + 4 > pl.size()) return false;
        *v = uint32_t(pl[o]) | (uint32_t(pl[o + 1]) << 8) |
             (uint32_t(pl[o + 2]) << 16) | (uint32_t(pl[o + 3]) << 24);
        o += 4;
        return true;
    };
    auto getStr = [&](std::string* s) {
        uint32_t n = 0;
        if (!get32(&n) || o + n > pl.size()) return false;
        s->assign(reinterpret_cast<const char*>(pl.data() + o), n);
        o += n;
        return true;
    };
    uint32_t count = 0;
    if (!get32(&count)) return false;
    out->clear();
    for (uint32_t i = 0; i < count; ++i) {
        RemoteVoice v;
        if (o + 4 > pl.size()) return false;
        v.id.assign(reinterpret_cast<const char*>(pl.data() + o), 4);
        o += 4;
        uint32_t t = 0;
        if (!get32(&t)) return false; v.variant = int(t);
        if (!get32(&t)) return false; v.gender = int(t);
        if (!get32(&t)) return false; v.age = int(t);
        if (!get32(&t)) return false; v.language = int(t);
        if (!get32(&t)) return false; v.region = int(t);
        if (!get32(&t)) return false; v.version = int(t);
        if (!getStr(&v.pack)) return false;
        std::string utf8;
        if (!getStr(&utf8)) return false;
        int wl = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), int(utf8.size()), nullptr, 0);
        v.name.resize(wl);
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), int(utf8.size()), &v.name[0], wl);
        out->push_back(std::move(v));
    }
    return true;
}

bool PipeClient::speak(const SpeakCommand& cmd, const std::string& text,
                       const AudioSink& sink, const AbortFn& abort,
                       std::vector<EventRecord>* events, std::wstring* error) {
    if (!ensureConnected()) {
        if (error) *error = L"the Infovox 210 worker could not be started";
        return false;
    }
    SpeakCommand c = cmd;
    c.textLength = uint32_t(text.size());
    std::vector<uint8_t> payload(sizeof c + text.size());
    memcpy(payload.data(), &c, sizeof c);
    memcpy(payload.data() + sizeof c, text.data(), text.size());
    if (!sendCommand(CMD_SPEAK, payload.data(), payload.size())) {
        disconnect();
        if (error) *error = L"lost the connection to the Infovox 210 worker";
        return false;
    }

    bool stopSent = false;
    std::vector<uint8_t> buf;
    for (;;) {
        if (!stopSent && abort && abort()) {
            sendCommand(CMD_STOP, nullptr, 0);
            stopSent = true;
        }
        PipeHeader h{};
        if (!readHeader(&h, 30000)) {
            disconnect();
            if (error) *error = L"the Infovox 210 worker stopped responding";
            return false;
        }
        if (h.size) {
            buf.resize(h.size);
            if (!readAll(buf.data(), buf.size(), 30000)) {
                disconnect();
                if (error) *error = L"the Infovox 210 worker stopped responding";
                return false;
            }
        } else {
            buf.clear();
        }
        switch (h.type) {
            case RESP_AUDIO:
                if (!stopSent && sink) {
                    if (!sink(reinterpret_cast<const int16_t*>(buf.data()), buf.size() / 2)) {
                        sendCommand(CMD_STOP, nullptr, 0);
                        stopSent = true;
                    }
                }
                break;
            case RESP_EVENT:
                if (events) {
                    size_t n = buf.size() / sizeof(EventRecord);
                    events->resize(n);
                    if (n) memcpy(events->data(), buf.data(), n * sizeof(EventRecord));
                }
                break;
            case RESP_END:
                return true;
            case RESP_ERROR: {
                std::string msg(reinterpret_cast<const char*>(buf.data()), buf.size());
                if (error) {
                    error->assign(msg.begin(), msg.end());
                }
                return false;
            }
            default:
                break;
        }
    }
}

void PipeClient::shutdownWorker() {
    HANDLE p = CreateFileW(INFOVOX_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (p == INVALID_HANDLE_VALUE) return;
    PipeHeader h{CMD_SHUTDOWN, 0};
    DWORD wrote = 0;
    WriteFile(p, &h, sizeof h, &wrote, nullptr);
    CloseHandle(p);
}

}  // namespace sapi
}  // namespace Infovox
