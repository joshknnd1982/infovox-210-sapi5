// Consonant clarity: puts back the band the 1996 engine cannot synthesise.
//
// Infovox 210 runs its formant synthesizer at roughly 8 kHz internally and
// resamples to 22050 for output, so the signal is hard band-limited at about
// 4 kHz (measured: -100 dB by 4.5 kHz).  The engine has no parameter that
// changes this, and Gestalt answers make no difference.  Sibilants live at
// 4-8 kHz, which is why /s/, /f/, /h/ and /th/ arrive almost inaudible and are
// easy to confuse with one another.
//
// Two things happen here.  First a high shelf lifts everything above 2.6 kHz,
// which is where the engine puts what fricative energy it has; measured over a
// sentence the engine's own spectrum falls from 0.079 %/Hz at 0-500 Hz to
// 0.0045 %/Hz at 3-4 kHz, so consonants really are buried.  Then the octave
// above the ceiling is regenerated as shaped noise, scaled by the level of the
// band that did survive.  A gate on noisiness alone cannot find the fricatives
// (this engine's vowels are nearly as bright as its /s/), so the regenerated
// band simply tracks what is there, which is what a band extension should do.
#pragma once

#include <cmath>
#include <cstdint>

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

class ConsonantClarity {
public:
    // amount: 0..100.  0 leaves the engine's output untouched.
    void configure(double sampleRate, int amount) {
        amount_ = amount < 0 ? 0 : amount > 100 ? 100 : amount;
        double a = double(amount_) / 100.0;
        gain_ = 3.0 * a;
        shelf_.highShelf(sampleRate, 2600.0, 12.0 * a);
        // The shelf adds energy, so trim back to keep peaks where they were.
        makeup_ = 1.0 / (1.0 + 0.35 * a);
        srcHp_.highpass(sampleRate, 2300.0);
        // Shape the new noise into a sibilant band rather than letting two
        // cascaded high-passes tilt it up into a hiss at the top of the range.
        outHp_[0].highpass(sampleRate, 4500.0);
        outHp_[1].lowpass(sampleRate, 9000.0);
        wideHp_.highpass(sampleRate, 2300.0);
        // ~4 ms attack/release on the noisiness gate: fast enough to catch a
        // short fricative, slow enough not to chatter inside one.
        double tc = std::exp(-1.0 / (0.004 * sampleRate));
        smooth_ = tc;
        env_ = envHp_ = 0.0;
        gate_ = 0.0;
        rng_ = 22050;
        dc_.configure(sampleRate);
        reset();
    }

    bool active() const { return amount_ > 0; }

    void reset() {
        srcHp_.reset(); outHp_[0].reset(); outHp_[1].reset(); wideHp_.reset();
        shelf_.reset();
        dc_.reset();
        env_ = envHp_ = 0.0;
        gate_ = 0.0;
    }

    // In place, one buffer at a time; filter state carries across buffers so
    // there is no seam at a buffer boundary.
    void process(int16_t* samples, size_t count) {
        // The DC pedestal is removed whatever the clarity setting: it is a
        // defect to correct, not an effect to choose.
        if (!active()) {
            for (size_t i = 0; i < count; ++i) {
                double y = dc_.process(double(samples[i]));
                samples[i] = int16_t(y > 32767.0 ? 32767 : y < -32768.0 ? -32768 : y);
            }
            return;
        }
        for (size_t i = 0; i < count; ++i) {
            double x = shelf_.process(dc_.process(double(samples[i])));

            // How noise-like is this instant?  Fricatives put a large share of
            // their energy above 2.3 kHz; vowels put very little there.
            double hp = wideHp_.process(x);
            env_ = env_ * smooth_ + std::fabs(x) * (1.0 - smooth_);
            envHp_ = envHp_ * smooth_ + std::fabs(hp) * (1.0 - smooth_);
            double ratio = envHp_ / (env_ + 1e-9);
            double want = (ratio - 0.12) / 0.30;
            want = want < 0.0 ? 0.0 : want > 1.0 ? 1.0 : want;
            gate_ = gate_ * smooth_ + want * (1.0 - smooth_);

            // A fricative *is* noise, so the honest way to restore the octave
            // the synthesizer could not reach is to make noise there and give
            // it the level and timing of the band that did survive.  Folding
            // the low band up by rectification was far too quiet to help.
            double n = noise();
            double band = outHp_[1].process(outHp_[0].process(n));
            double y = (x + gain_ * gate_ * envHp_ * band) * makeup_;
            samples[i] = int16_t(y > 32767.0 ? 32767 : y < -32768.0 ? -32768 : y);
        }
    }

private:
    // Deterministic white noise: the same text renders the same audio, which
    // matters for anyone diffing output between builds.
    double noise() {
        rng_ = rng_ * 1664525u + 1013904223u;
        return double(int32_t(rng_)) * (1.0 / 2147483648.0);
    }

    Biquad srcHp_, outHp_[2], wideHp_, shelf_;
    DcBlocker dc_;
    uint32_t rng_ = 22050;
    double gain_ = 0.0, smooth_ = 0.0, makeup_ = 1.0;
    double env_ = 0.0, envHp_ = 0.0, gate_ = 0.0;
    int amount_ = 0;
};

}  // namespace ppc
