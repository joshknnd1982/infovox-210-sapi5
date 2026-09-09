#pragma once

#include <stdint.h>

// The Infovox 210 engine is a 1996 PowerPC Component Manager component running
// under a Unicorn CPU, and Unicorn is only available here as a 64-bit library.
// Both the 32-bit and the 64-bit SAPI engines therefore talk to one 64-bit
// worker over this pipe.  All text preparation happens on the SAPI side; the
// worker receives finished Mac Roman bytes and the handful of numbers that
// drive the synthesizer.
#define INFOVOX_PIPE_NAME L"\\\\.\\pipe\\Infovox210TTS"

// Bumped whenever the wire format changes.  A worker from a previous install
// can still be running when a new version starts speaking, so the client checks
// this on connect and replaces a worker that does not match.
#define INFOVOX_PROTOCOL_VERSION 2u

// Several pipe instances exist from the moment the worker starts, so a client
// never finds the name missing and never pays to have one created.
#define INFOVOX_PIPE_INSTANCES 8

enum InfovoxCommand : uint32_t {
    CMD_HELLO       = 1,
    CMD_LIST_VOICES = 2,
    CMD_SPEAK       = 3,
    CMD_STOP        = 4,
    CMD_SHUTDOWN    = 5,
    CMD_PING        = 6,
};

enum InfovoxResponse : uint32_t {
    RESP_HELLO   = 1,
    RESP_VOICES  = 2,
    RESP_AUDIO   = 3,
    RESP_EVENT   = 4,
    RESP_END     = 5,
    RESP_ERROR   = 6,
    RESP_PONG    = 7,
};

#pragma pack(push, 1)

struct PipeHeader {
    uint32_t type;
    uint32_t size;      // payload bytes following this header
};

struct HelloReply {
    uint32_t protocol;
    uint32_t sampleRate;
    uint32_t voiceCount;
    uint32_t bitsPerSample;
    uint32_t channels;
};

// Every parameter is a 0..100 slider where 0 is the engine's minimum and 100
// its maximum, so a screen reader's own 0..100 sliders reach both extremes.
struct SpeakCommand {
    char     voiceId[4];    // 'AM01'
    int32_t  rate;          // 0..100 -> 0..999 words per minute
    int32_t  pitch;         // 0..100 -> pitch base 0..100
    int32_t  pitchMod;      // 0..100 -> pitch modulation 0..100
    int32_t  breath;        // 0..100 -> InVx aspiration 0..100
    int32_t  volume;        // 0..100 -> software gain
    int32_t  clarity;       // 0..100 consonant clarity; 0 = engine untouched
    uint32_t spellOut;      // 1 = say each character's name (SAPI SPVA_SpellOut)
    uint32_t literalNumbers;// 1 = say digits one at a time
    uint32_t textLength;    // Mac Roman bytes following this struct
};

// Word and phoneme boundaries, so SAPI can raise bookmark/word/viseme events.
struct EventRecord {
    uint32_t kind;          // 0 word, 1 phoneme
    uint32_t sampleOffset;  // frames from the start of the utterance
    int32_t  a;             // word: byte offset  phoneme: opcode
    int32_t  b;             // word: byte length
};

#pragma pack(pop)

#define INFOVOX_BITS_PER_SAMPLE 16
#define INFOVOX_CHANNELS 1
#define INFOVOX_SAMPLE_RATE 22050
