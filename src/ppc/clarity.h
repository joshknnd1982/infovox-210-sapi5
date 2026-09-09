// Consonant clarity: puts back the frication the 1996 engine does not produce.
//
// What the engine actually does, measured on its own output:
//
//   * It emits no frication at all.  Rendered on the American male voice, the
//     first 50 ms of "see", "fee", "thief", "he" and "heed" have the same
//     spectrum to within 2 dB and the same glottal pulse train at F0 -- the
//     engine substitutes voicing wherever a voiceless fricative belongs.  Its
//     own aspiration control ('aspi') confirms it: raising it scales the voice
//     down and adds nothing, and at full scale the output is digital silence,
//     which is what a synthesizer sounds like when its noise source returns
//     zero.  Inside the emulated synthesizer six of the twenty-three per-sample
//     filter states -- one whole resonator branch -- stay at exactly 0.0 for an
//     entire utterance.
//
//   * So the audio carries no cue to where the fricatives are.  Four different
//     detectors were measured over a 39-phone inventory (high-band ratio, high
//     band against 0-700 Hz, high band against 300-1200 Hz, and aperiodicity):
//     none separated /s/ /S/ /f/ /T/ /h/ from the vowels.  The best margin was
//     0.72x -- worse than chance.  The previous version of this file gated on
//     high-band ratio and, measured per phone, opened further on /aI/ (0.48),
//     /eI/ (0.42) and /m/ (peak 0.93) than on /s/ (0.19) or /S/ (0.34), while
//     giving /z/ /v/ /Z/ /D/ nothing at all.  It was putting its noise on the
//     diphthongs and the nasals.
//
// The engine does, however, say what it is speaking: it fires a phoneme
// callback per phone, and each language pack carries its own symbol table.  So
// the frication is scheduled from that stream instead of guessed from the
// audio.  The caller classifies each phoneme symbol (see infovox.cpp) and hands
// the class in with a sample-accurate offset; this file synthesises the noise.
//
// Two fixed noise bands are mixed rather than retuning a filter per phone: a
// retune inside an utterance clicks, a crossfade does not.  Levels follow a
// slow envelope of the engine's own speech, so frication tracks the voice,
// the volume setting and the speaking rate without further calibration.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace ppc {

class Biquad {
public:
    void highpass(double fs, double fc, double q = 0.70710678) {
        double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        double c = std::cos(w0), s = std::sin(w0);
        double alpha = s / (2.0 * q);
        double a0 = 1.0 + alpha;
        b0_ = ((1.0 + c) / 2.0) / a0;
        b1_ = (-(1.0 + c)) / a0;
        b2_ = ((1.0 + c) / 2.0) / a0;
        a1_ = (-2.0 * c) / a0;
        a2_ = (1.0 - alpha) / a0;
        z1_ = z2_ = 0.0;
    }
    double process(double x) {
        double y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }
    // RBJ high shelf: lifts everything above fc by gainDb.
    void highShelf(double fs, double fc, double gainDb, double s = 0.9) {
        double A = std::pow(10.0, gainDb / 40.0);
        double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        double c = std::cos(w0), sn = std::sin(w0);
        double alpha = sn / 2.0 * std::sqrt((A + 1.0 / A) * (1.0 / s - 1.0) + 2.0);
        double tsa = 2.0 * std::sqrt(A) * alpha;
        double a0 = (A + 1.0) - (A - 1.0) * c + tsa;
        b0_ = (A * ((A + 1.0) + (A - 1.0) * c + tsa)) / a0;
        b1_ = (-2.0 * A * ((A - 1.0) + (A + 1.0) * c)) / a0;
        b2_ = (A * ((A + 1.0) + (A - 1.0) * c - tsa)) / a0;
        a1_ = (2.0 * ((A - 1.0) - (A + 1.0) * c)) / a0;
        a2_ = ((A + 1.0) - (A - 1.0) * c - tsa) / a0;
        z1_ = z2_ = 0.0;
    }
    void lowpass(double fs, double fc, double q = 0.70710678) {
        double w0 = 2.0 * 3.14159265358979323846 * fc / fs;
        double c = std::cos(w0), s = std::sin(w0);
        double alpha = s / (2.0 * q);
        double a0 = 1.0 + alpha;
        b0_ = ((1.0 - c) / 2.0) / a0;
        b1_ = (1.0 - c) / a0;
        b2_ = ((1.0 - c) / 2.0) / a0;
        a1_ = (-2.0 * c) / a0;
        a2_ = (1.0 - alpha) / a0;
        z1_ = z2_ = 0.0;
    }
    void reset() { z1_ = z2_ = 0.0; }

private:
    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, z1_ = 0, z2_ = 0;
};

// The engine's output sits on a large, speech-dependent DC pedestal: measured
// per 1024-frame buffer it walks up to about 860 counts as an utterance starts.
// That is inaudible while it plays, but a screen reader abandons an utterance
// on every keypress, and cutting from a pedestal straight to digital silence is
// a click.  This takes the pedestal out so a cut starts from zero.
class DcBlocker {
public:
    void configure(double sampleRate, double fc = 25.0) {
        // One-pole differentiator/integrator pair; 25 Hz is well below the
        // lowest F0 the engine produces (about 66 Hz at the bottom of its
        // pitch range) and settles in a few milliseconds.
        r_ = 1.0 - (2.0 * 3.14159265358979323846 * fc / sampleRate);
        reset();
    }
    void reset() { x1_ = y1_ = 0.0; }
    double process(double x) {
        double y = x - x1_ + r_ * y1_;
        x1_ = x;
        y1_ = y;
        return y;
    }

private:
    double r_ = 0.9929, x1_ = 0.0, y1_ = 0.0;
};

// What kind of noise a phoneme needs.  The caller maps each language's own
// symbols onto these; anything not listed is Voiced, which makes no noise.
enum class Fric : uint8_t {
    Voiced = 0,   // vowels, nasals, glides, voiced stops: nothing to add
    S,            // /s/          sharp, high
    Z,            // /z/          the same band, half level: it is partly voiced
    Sh,           // /S/          lower and broader than /s/
    Zh,           // /Z/
    Ch,           // /tS/         an affricate: shorter, sharper, brighter than /S/
    Jh,           // /dZ/
    F,            // /f/          weak, flat, spread wide
    V,            // /v/
    Th,           // /T/          weaker still
    Dh,           // /D/
    H,            // /h/          breathy, low, shaped like the vowel it leads
    X,            // German ach-Laut, Swedish sj, Spanish jota
};

class ConsonantClarity {
public:
    // amount: 0..100.  0 leaves the engine's output untouched.  `wordsPerMinute`
    // is the rate the engine is speaking at, which sets how long a phone lasts.
    void configure(double sampleRate, int amount, double wordsPerMinute) {
        fs_ = sampleRate;
        // The last phoneme of an utterance has no successor to switch the
        // schedule, so a word-final fricative would otherwise hiss until some
        // arbitrary cap ran out -- "pass" held its /s/ for a third of a second.
        // A fricative runs about 140 ms at the engine's default 150 wpm, and
        // scales with the rate from there.
        double wpm = wordsPerMinute < 40.0 ? 40.0
                   : wordsPerMinute > 999.0 ? 999.0 : wordsPerMinute;
        maxRun_ = 0.140 * (150.0 / wpm);
        if (maxRun_ < 0.070) maxRun_ = 0.070;
        if (maxRun_ > 0.400) maxRun_ = 0.400;
        amount_ = amount < 0 ? 0 : amount > 100 ? 100 : amount;
        double a = double(amount_) / 100.0;
        gain_ = 1.35 * a;
        shelf_.highShelf(sampleRate, 2600.0, 9.0 * a);
        // The shelf adds energy, so trim back to keep peaks where they were.
        makeup_ = 1.0 / (1.0 + 0.30 * a);

        // Two fixed noise bands, crossfaded per phone.  Between them they span
        // 1.6 kHz to the top of the band, which is where every fricative this
        // engine has to make lives.
        lowBand_[0].highpass(sampleRate, 1900.0);
        lowBand_[1].lowpass(sampleRate, 5200.0);
        highBand_[0].highpass(sampleRate, 4200.0);
        highBand_[1].lowpass(sampleRate, 9500.0);

        // Envelope of the engine's own speech, used as the level reference so
        // frication scales with the voice without further calibration.  The
        // long release is what makes it a reference rather than a waveform
        // follower: a 4 ms envelope swings from zero to full inside a single
        // glottal period, and noise multiplied by that buzzes.
        speechUp_   = 1.0 - std::exp(-1.0 / (0.030 * sampleRate));
        speechDown_ = 1.0 - std::exp(-1.0 / (0.400 * sampleRate));
        // Onset and offset of one phone's noise.  Both are short -- a fricative
        // starts and stops abruptly -- and the offset is the shorter of the
        // two, so nothing trails into the silence of a following stop closure.
        attack_  = 1.0 - std::exp(-1.0 / (0.005 * sampleRate));
        release_ = 1.0 - std::exp(-1.0 / (0.0022 * sampleRate));

        rng_ = 22050;
        dc_.configure(sampleRate);
        beginUtterance();
        reset();
    }

    bool active() const { return amount_ > 0; }

    void reset() {
        lowBand_[0].reset(); lowBand_[1].reset();
        highBand_[0].reset(); highBand_[1].reset();
        shelf_.reset();
        dc_.reset();
        speech_ = peak_ = 0.0;
        curGain_ = curMix_ = 0.0;
    }

    // Clears the phoneme schedule and the playback position.  Call once per
    // utterance, before the first buffer.
    void beginUtterance() {
        sched_.clear();
        next_ = 0;
        pos_ = 0;
        target_ = Fric::Voiced;
        phoneEnd_ = 0;
    }

    // A phoneme starts at `sampleOffset` frames into the utterance.  Offsets
    // must not go backwards; anything already played is dropped.
    void addPhoneme(uint64_t sampleOffset, Fric kind) {
        if (!sched_.empty() && sampleOffset < sched_.back().at)
            sampleOffset = sched_.back().at;
        sched_.push_back({sampleOffset, kind});
    }

    // In place, one buffer at a time; filter state carries across buffers so
    // there is no seam at a buffer boundary.
    void process(int16_t* samples, size_t count) {
        // The DC pedestal is removed whatever the clarity setting: it is a
        // defect to correct, not an effect to choose.
        if (!active()) {
            for (size_t i = 0; i < count; ++i) {
                double y = dc_.process(double(samples[i]));
                samples[i] = clip(y);
                ++pos_;
            }
            return;
        }
        for (size_t i = 0; i < count; ++i) {
            double x = shelf_.process(dc_.process(double(samples[i])));

            // Track the engine's own loudness: fast up so a phrase starts on
            // time, slow down so the reference does not follow every glottal
            // period.  Multiplying noise by a rippling envelope is what made
            // the old version buzz.
            double mag = std::fabs(x);
            speech_ += (mag - speech_) * (mag > speech_ ? speechUp_ : speechDown_);
            if (speech_ > peak_) peak_ = speech_;

            // A voiceless fricative can be pure digital silence in this engine
            // -- the /s/ that opens a Danish sentence measures 20 counts rms --
            // so the level reference cannot come from the audio alone or the
            // first consonant of an utterance would get nothing.  The floor is
            // about a third of ordinary speech, and real speech overrides it
            // within 30 ms of the utterance starting.
            double ref = speech_ > kLevelFloor ? speech_ : kLevelFloor;
            // Once the utterance is well and truly over, stop: the last phone
            // has no successor to switch the schedule, and trailing silence
            // must stay silent.
            double duck = peak_ > 0.0 ? speech_ / (0.05 * peak_) : 1.0;
            if (duck > 1.0) duck = 1.0;

            // Advance the phoneme schedule.
            while (next_ < sched_.size() && sched_[next_].at <= pos_) {
                target_ = sched_[next_].kind;
                phoneEnd_ = pos_ + maxRun(target_);
                ++next_;
            }
            // A phone that outlives its plausible duration stops: the engine
            // may follow it with silence and no further callback.
            Fric want = (pos_ >= phoneEnd_) ? Fric::Voiced : target_;

            double wantGain = 0.0, wantMix = 0.0;
            shapeOf(want, &wantGain, &wantMix);
            wantGain *= duck;
            curGain_ += (wantGain - curGain_) * (wantGain > curGain_ ? attack_ : release_);
            curMix_  += (wantMix  - curMix_)  * attack_;

            double y = x;
            if (curGain_ > 1e-4) {
                double n = noise();
                double lo = lowBand_[1].process(lowBand_[0].process(n));
                double hi = highBand_[1].process(highBand_[0].process(n));
                // Both bands are normalised to about unit RMS so `gain_` means
                // the same thing whichever is selected.
                // Equal-power crossfade.  Measured over 200k samples, the two
                // bands come out at 0.311 and 0.392 RMS from a 0.577 RMS
                // source, so those are the gains that make each of them unit
                // level; and because a shared source run through a 1.9-5.2 kHz
                // and a 4.2-9.5 kHz filter is anti-correlated (rho = -0.248),
                // a plain crossfade loses 4.2 dB in the middle of its travel.
                // That was quietest for exactly the phones that needed it most
                // -- /tS/, /S/, /f/ and /v/ all sit near the middle.
                double w1 = 1.0 - curMix_, w2 = curMix_;
                double p = w1 * w1 + w2 * w2 - 0.496 * w1 * w2;
                double band = (w1 * lo * 3.218 + w2 * hi * 2.554) /
                              std::sqrt(p > 1e-6 ? p : 1e-6);
                y += gain_ * curGain_ * ref * band;
            } else {
                // Keep the filters running so a phone never starts on a
                // transient from stale state.
                double n = noise();
                lowBand_[1].process(lowBand_[0].process(n));
                highBand_[1].process(highBand_[0].process(n));
            }
            samples[i] = clip(y * makeup_);
            ++pos_;
        }
    }

private:
    struct Sched { uint64_t at; Fric kind; };

    // Level a fricative is given when the engine has produced nothing to
    // scale it to.  A voiceless fricative is often digital silence here, so an
    // utterance that opens with one -- "spot", "see", "Sofie" -- has no
    // reference at all, and the old 1150 left those 11.7 dB below the same
    // consonant mid-utterance, which is what made a word-initial /s/ before a
    // stop sound dropped.  Measured over 485 fricative onsets on all 55 voices,
    // the reference lands at a median of 4412 (p10 2301, p90 7314); 3000 sits
    // just under that without shouting on the quietest voices.
    static constexpr double kLevelFloor = 3000.0;

    static int16_t clip(double y) {
        return int16_t(y > 32767.0 ? 32767 : y < -32768.0 ? -32768 : y);
    }

    // Level relative to the running speech level, and where in the two bands
    // the energy sits (0 = the low band alone, 1 = the high band alone).
    // Levels are the ones a formant synthesizer uses for these phones: /s/ and
    // /S/ carry frication as loud as a weak vowel, /f/ and /T/ much less, and
    // the voiced members of each pair about half, because their voicing is
    // already there in the engine's output.
    static void shapeOf(Fric k, double* gain, double* mix) {
        switch (k) {
            case Fric::S:      *gain = 1.00; *mix = 0.95; break;
            case Fric::Z:      *gain = 0.58; *mix = 0.95; break;
            case Fric::Sh:     *gain = 0.95; *mix = 0.30; break;
            case Fric::Zh:     *gain = 0.48; *mix = 0.30; break;
            case Fric::Ch:     *gain = 0.92; *mix = 0.52; break;
            case Fric::Jh:     *gain = 0.62; *mix = 0.52; break;
            case Fric::F:      *gain = 0.52; *mix = 0.62; break;
            case Fric::V:      *gain = 0.44; *mix = 0.62; break;
            case Fric::Th:     *gain = 0.42; *mix = 0.70; break;
            case Fric::Dh:     *gain = 0.30; *mix = 0.70; break;
            case Fric::H:      *gain = 0.38; *mix = 0.20; break;
            case Fric::X:      *gain = 0.50; *mix = 0.12; break;
            default:           *gain = 0.00; *mix = 0.00; break;
        }
    }

    // Longest a phone may keep making noise, in frames.  The last phoneme of an
    // utterance has no successor to switch the schedule, so without this a
    // word-final /s/ hisses until the cap runs out -- which is what made
    // "pass" hold its /s/ for a third of a second.
    //
    // The cap has to follow the speaking rate: at 50 words per minute a phone
    // is three times as long as at 150, and a fixed figure would either cut
    // slow speech off or let fast speech run on.  The engine's own phoneme
    // timing gives it for nothing -- the median gap between callbacks is the
    // length of a typical phone, and a fricative runs a little longer than one.
    uint64_t maxRun(Fric k) const {
        return k == Fric::Voiced ? 0 : uint64_t(maxRun_ * fs_);
    }

    // Deterministic white noise: the same text renders the same audio, which
    // matters for anyone diffing output between builds.  The top bits are used
    // because the low bits of a power-of-two linear congruential generator are
    // not random at all -- bit 0 of this one simply alternates, at Nyquist.
    double noise() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return double(int32_t(rng_ & 0xFFFFFF00u)) * (1.0 / 2147483648.0);
    }

    Biquad lowBand_[2], highBand_[2], shelf_;
    DcBlocker dc_;
    uint32_t rng_ = 22050;
    double fs_ = 22050.0;
    double gain_ = 0.0, makeup_ = 1.0;
    double speechUp_ = 0.0, speechDown_ = 0.0, attack_ = 0.0, release_ = 0.0;
    double speech_ = 0.0, peak_ = 0.0, curGain_ = 0.0, curMix_ = 0.0;
    double maxRun_ = 0.140;
    int amount_ = 0;

    std::vector<Sched> sched_;
    size_t   next_ = 0;
    uint64_t pos_ = 0;
    uint64_t phoneEnd_ = 0;
    Fric     target_ = Fric::Voiced;
};

}  // namespace ppc
