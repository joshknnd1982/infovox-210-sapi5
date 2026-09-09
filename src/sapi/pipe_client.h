#pragma once

#include <functional>
#include <string>
#include <vector>

#include <windows.h>

#include "pipe_protocol.h"

namespace Infovox {
namespace sapi {

struct RemoteVoice {
    std::string  id;        // 'AM01'
    std::string  pack;      // "american"
    std::wstring name;
    int variant = 1, gender = 0, age = 30, language = 0, region = 0, version = 0;
};

// Talks to infovox_host.exe.  Both SAPI architectures use this: the emulator is
// 64-bit only, so even the 64-bit engine goes out of process rather than
// loading a JIT into every application that speaks.
class PipeClient {
public:
    PipeClient();
    ~PipeClient();

    PipeClient(const PipeClient&) = delete;
    PipeClient& operator=(const PipeClient&) = delete;

    // Connects, starting the worker if it is not already running.
    bool ensureConnected();
    void disconnect();
    bool connected() const { return pipe_ != INVALID_HANDLE_VALUE; }

    bool listVoices(std::vector<RemoteVoice>* out);

    using AudioSink = std::function<bool(const int16_t* frames, size_t count)>;
    using AbortFn = std::function<bool()>;

    // Streams one utterance.  `abort` is polled between chunks; when it first
    // returns true a CMD_STOP goes out and the response stream is drained, so
    // the connection stays usable for the next utterance.
    bool speak(const SpeakCommand& cmd, const std::string& macRomanText,
               const AudioSink& sink, const AbortFn& abort,
               std::vector<EventRecord>* events, std::wstring* error);

    static void shutdownWorker();

private:
    bool writeAll(const void* data, size_t len);
    bool readAll(void* data, size_t len, DWORD timeoutMs);
    bool sendCommand(uint32_t type, const void* payload, size_t len);
    bool readHeader(PipeHeader* h, DWORD timeoutMs);
    bool handshake();
    static bool startWorker();

    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE readEvent_ = nullptr;
    HANDLE writeEvent_ = nullptr;
    uint32_t sampleRate_ = INFOVOX_SAMPLE_RATE;
};

// Directory this dll was loaded from, with a trailing backslash.
std::wstring installDir();

}  // namespace sapi
}  // namespace Infovox
