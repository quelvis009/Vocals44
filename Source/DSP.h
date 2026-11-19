#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>
#include <vector>

namespace v44
{
inline float toDb (float lin) noexcept { return 20.0f * std::log10 (std::max (lin, 1.0e-6f)); }
inline float toLin (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float msCoef (float ms, double sr) noexcept { return std::exp (-1.0f / (0.001f * ms * (float) sr)); }

//==============================================================================
// GATE
class Gate
{
public:
    void prepare (double sr) { sampleRate = sr; env = 0.0f; gain = 1.0f; }

    void process (juce::AudioBuffer<float>& b, float threshDb)
    {
        const int n = b.getNumSamples(), nch = b.getNumChannels();
        const float openC = msCoef (1.0f, sampleRate), closeC = msCoef (120.0f, sampleRate), detC = msCoef (40.0f, sampleRate);

        for (int i = 0; i < n; ++i)
        {
            float lvl = 0.0f;
            for (int c = 0; c < nch; ++c)
                lvl = std::max (lvl, std::abs (b.getReadPointer (c)[i]));

            env = lvl > env ? lvl : detC * env + (1.0f - detC) * lvl;
            const float target = toDb (env) < threshDb ? 0.0316f : 1.0f; // -30 dB
            const float k = target > gain ? openC : closeC;
            gain = k * gain + (1.0f - k) * target;

            for (int c = 0; c < nch; ++c)
                b.getWritePointer (c)[i] *= gain;
        }
    }

private:
    double sampleRate = 44100.0;
    float env = 0.0f, gain = 1.0f;
};

//==============================================================================
// COMPRESSOR (una etapa)
class CompStage
{
public:
    void prepare (double sr) { sampleRate = sr; smoothed = 0.0f; }
    void reset() { smoothed = 0.0f; }

    // devuelve la reduccion maxima en dB (positivo)
    float process (juce::AudioBuffer<float>& b, float thresh, float ratio, float attMs, float relMs, float knee, float makeupDb)
    {
        const int n = b.getNumSamples(), nch = b.getNumChannels();
        const float aC = msCoef (attMs, sampleRate), rC = msCoef (relMs, sampleRate);
        const float slope = 1.0f / ratio - 1.0f;
        const float makeup = toLin (makeupDb);
        knee = std::max (knee, 0.1f);
        float maxGr = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float lvl = 0.0f;
            for (int c = 0; c < nch; ++c)
                lvl = std::max (lvl, std::abs (b.getReadPointer (c)[i]));

            const float over = toDb (lvl) - thresh;
            float gr;
            if (2.0f * over < -knee)
                gr = 0.0f;
            else if (2.0f * std::abs (over) <= knee)
            {
                const float t = over + knee * 0.5f;
                gr = slope * t * t / (2.0f * knee);
            }
            else
                gr = slope * over;

            const float k = gr < smoothed ? aC : rC;
            smoothed = k * smoothed + (1.0f - k) * gr;
            maxGr = std::max (maxGr, -smoothed);

            const float g = toLin (smoothed) * makeup;
            for (int c = 0; c < nch; ++c)
                b.getWritePointer (c)[i] *= g;
        }
        return maxGr;
    }

private:
    double sampleRate = 44100.0;
    float smoothed = 0.0f;
};

inline float colorSat (float x, float color) noexcept
{
    const float d = 1.0f + color * 3.0f;
    const float bias = 0.06f * color;
    const float y = std::tanh (d * x + bias) - std::tanh (bias);
    return x * (1.0f - color) + (y / d) * (1.0f + color * 0.5f) * color;
}

// DYNAMICS: OPTO / FET / OPTO+FET + COLOR
class Dynamics
{
public:
    void prepare (double sr) { opto.prepare (sr); fet.prepare (sr); }

    float process (juce::AudioBuffer<float>& b, float amount, float color, int mode)
    {
        float gr = 0.0f;
        auto makeup = [] (float thr, float ratio) { return -thr * (1.0f - 1.0f / ratio) * 0.35f; };

        if (amount > 0.001f)
        {
            const float a = (mode == 2) ? amount * 0.65f : amount;

            if (mode == 0 || mode == 2)
            {
                const float thr = -6.0f - a * 30.0f, ratio = 1.6f + a * 3.0f;
                gr += opto.process (b, thr, ratio, 12.0f, 230.0f, 10.0f, makeup (thr, ratio));
            }
            else opto.reset();

            if (mode == 1 || mode == 2)
            {
                const float thr = -4.0f - a * 28.0f, ratio = 2.0f + a * 8.0f;
                gr += fet.process (b, thr, ratio, 0.6f, 70.0f, 2.0f, makeup (thr, ratio));
            }
            else fet.reset();
        }
        else { opto.reset(); fet.reset(); }

        if (color > 0.001f)
            for (int c = 0; c < b.getNumChannels(); ++c)
            {
                auto* d = b.getWritePointer (c);
                for (int i = 0; i < b.getNumSamples(); ++i)
                    d[i] = colorSat (d[i], color);
            }

        return gr;
    }

private:
    CompStage opto, fet;
};

//==============================================================================
// DE-ESSER (banda dividida)
class DeEsser
{
public:
    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, maxBlock), 2 };
        bp.prepare (spec);
        bp.setType (juce::dsp::StateVariableTPTFilterType::bandpass);
        bp.setResonance (0.8f);
        env = red = 0.0f;
    }

    float process (juce::AudioBuffer<float>& b, float amount, float freq)
    {
        if (amount < 0.001f) { red = 0.0f; env = 0.0f; return 0.0f; }

        bp.setCutoffFrequency (juce::jlimit (1000.0f, (float) sampleRate * 0.45f, freq));
        const float thresh = -10.0f - 26.0f * amount;
        const float eA = msCoef (0.4f, sampleRate), eR = msCoef (30.0f, sampleRate);
        const float rA = msCoef (0.5f, sampleRate), rR = msCoef (60.0f, sampleRate);
        const int n = b.getNumSamples();
        float maxRed = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            float band[2] = { 0.0f, 0.0f };
            float lvl = 0.0f;
            for (int c = 0; c < 2; ++c)
            {
                band[c] = bp.processSample (c, b.getReadPointer (c)[i]);
                lvl = std::max (lvl, std::abs (band[c]));
            }

            const float ek = lvl > env ? eA : eR;
            env = ek * env + (1.0f - ek) * lvl;

            const float over = toDb (env) - thresh;
            const float target = over > 0.0f ? std::min (over * 0.75f, 18.0f) : 0.0f;
            const float rk = target > red ? rA : rR;
            red = rk * red + (1.0f - rk) * target;
            maxRed = std::max (maxRed, red);

            const float g = toLin (-red);
            for (int c = 0; c < 2; ++c)
                b.getWritePointer (c)[i] -= band[c] * (1.0f - g);
        }
        return maxRed;
    }

private:
    double sampleRate = 44100.0;
    juce::dsp::StateVariableTPTFilter<float> bp;
    float env = 0.0f, red = 0.0f;
};

//==============================================================================
// TONE EQ
struct EqModeFreqs { float low, mid, high, air, midQ; };
inline const EqModeFreqs& eqMode (int m)
{
    static const EqModeFreqs modes[4] = {
        { 180.0f, 1000.0f, 5000.0f, 12000.0f, 0.70f },   // Modern
        { 140.0f,  700.0f, 3500.0f, 10000.0f, 0.60f },   // Warm
        { 100.0f, 2000.0f, 6500.0f, 14000.0f, 0.80f },   // Bright
        { 120.0f,  900.0f, 4500.0f, 11000.0f, 0.90f } }; // Vintage
    return modes[juce::jlimit (0, 3, m)];
}

using CoeffPtr = juce::dsp::IIR::Coefficients<float>::Ptr;

inline std::array<CoeffPtr, 4> makeEqCoeffs (double sr, int mode, float lowDb, float midDb, float highDb, float airDb)
{
    const auto& f = eqMode (mode);
    return { juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, f.low,  0.707f, toLin (lowDb)),
             juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, f.mid,  f.midQ, toLin (midDb)),
             juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, f.high, 0.707f, toLin (highDb)),
             juce::dsp::IIR::Coefficients<float>::makeHighShelf (sr, f.air,  0.707f, toLin (airDb)) };
}

class ToneEq
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        for (auto& band : f) for (auto& ch : band) ch.reset();
        cache = { -999.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    }

    void process (juce::AudioBuffer<float>& b, int mode, float low, float mid, float high, float air)
    {
        const std::array<float, 5> now { (float) mode, low, mid, high, air };
        if (now != cache)
        {
            cache = now;
            auto co = makeEqCoeffs (sampleRate, mode, low, mid, high, air);
            for (int i = 0; i < 4; ++i)
                for (int c = 0; c < 2; ++c)
                    f[i][c].coefficients = co[(size_t) i];
        }

        for (int c = 0; c < 2; ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                float x = d[i];
                for (int k = 0; k < 4; ++k) x = f[k][c].processSample (x);
                d[i] = x;
            }
        }
    }

private:
    double sampleRate = 44100.0;
    juce::dsp::IIR::Filter<float> f[4][2];
    std::array<float, 5> cache { -999.0f, 0.0f, 0.0f, 0.0f, 0.0f };
};

//==============================================================================
// SPACE: reverb + delay con ducking
class SpaceFx
{
public:
    struct Params { float revMix, revTime, dlyMix; int dlyRate; bool ping; float vrbDuck, dlyDuck; int mode; double bpm; };
    struct Meters { float vrbIn = 0, vrbDuck = 0, vrbOut = 0, dlyIn = 0, dlyDuck = 0, dlyOut = 0; };

    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        reverb.setSampleRate (sr);
        reverb.reset();
        wet.setSize (2, juce::jmax (1, maxBlock));
        const int len = (int) (sr * 3.2) + 8;
        for (auto& d : dl) d.assign ((size_t) len, 0.0f);
        wp = 0;
        curDelay = (float) (0.25 * sr);
        lpL = lpR = env = 0.0f;
    }

    void process (juce::AudioBuffer<float>& b, const Params& p, Meters& m)
    {
        const int n = b.getNumSamples();
        if (wet.getNumSamples() < n) wet.setSize (2, n, false, false, true);
        wet.copyFrom (0, 0, b, 0, 0, n);
        wet.copyFrom (1, 0, b, 1, 0, n);

        static const float damp[4] = { 0.78f, 0.30f, 0.55f, 0.62f };
        static const float tmul[4] = { 1.0f, 0.8f, 0.35f, 1.25f };
        const int mode = juce::jlimit (0, 3, p.mode);
        const float t = juce::jlimit (0.2f, 6.0f, p.revTime * tmul[mode]);

        juce::Reverb::Parameters rp;
        rp.roomSize = juce::jmap (t, 0.2f, 6.0f, 0.35f, 0.985f);
        rp.damping = damp[mode];
        rp.wetLevel = 0.33f;
        rp.dryLevel = 0.0f;
        rp.width = 1.0f;
        rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        reverb.processStereo (wet.getWritePointer (0), wet.getWritePointer (1), n);

        static const float beats[6] = { 0.25f, 0.3333f, 0.5f, 0.75f, 1.0f, 2.0f };
        const double bpm = juce::jlimit (50.0, 300.0, p.bpm);
        const float targetDelay = (float) (beats[juce::jlimit (0, 5, p.dlyRate)] * 60.0 / bpm * sampleRate);
        const int len = (int) dl[0].size();
        const float lpA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5500.0f / (float) sampleRate);
        const float relC = msCoef (180.0f, sampleRate);
        const float fb = 0.38f;

        float inPk = 0, duckPk = 0, vOutPk = 0, dOutPk = 0;

        for (int i = 0; i < n; ++i)
        {
            const float L = b.getReadPointer (0)[i], R = b.getReadPointer (1)[i];
            const float lvl = std::max (std::abs (L), std::abs (R));
            env = lvl > env ? lvl : relC * env + (1.0f - relC) * lvl;
            const float duckN = juce::jlimit (0.0f, 1.0f, env * 3.0f);
            inPk = std::max (inPk, lvl);
            duckPk = std::max (duckPk, duckN);

            curDelay += (targetDelay - curDelay) * 0.0004f;
            float rpos = (float) wp - curDelay;
            while (rpos < 0.0f) rpos += (float) len;
            int i0 = (int) rpos;
            const float fr = rpos - (float) i0;
            const int i1 = (i0 + 1) % len;
            i0 %= len;

            const float rdL = dl[0][(size_t) i0] * (1.0f - fr) + dl[0][(size_t) i1] * fr;
            const float rdR = dl[1][(size_t) i0] * (1.0f - fr) + dl[1][(size_t) i1] * fr;
            lpL += lpA * (rdL - lpL);
            lpR += lpA * (rdR - lpR);

            if (p.ping) { dl[0][(size_t) wp] = 0.5f * (L + R) + fb * lpR; dl[1][(size_t) wp] = fb * lpL; }
            else        { dl[0][(size_t) wp] = L + fb * lpL;              dl[1][(size_t) wp] = R + fb * lpR; }
            wp = (wp + 1) % len;

            const float rvGain = p.revMix * (1.0f - p.vrbDuck * duckN);
            const float dyGain = p.dlyMix * (1.0f - p.dlyDuck * duckN);

            const float wl = wet.getReadPointer (0)[i] * rvGain;
            const float wr = wet.getReadPointer (1)[i] * rvGain;
            const float dL = lpL * dyGain, dR = lpR * dyGain;

            vOutPk = std::max (vOutPk, std::max (std::abs (wl), std::abs (wr)));
            dOutPk = std::max (dOutPk, std::max (std::abs (dL), std::abs (dR)));

            b.getWritePointer (0)[i] = L + wl + dL;
            b.getWritePointer (1)[i] = R + wr + dR;
        }

        m.vrbIn = m.dlyIn = inPk;
        m.vrbDuck = m.dlyDuck = duckPk;
        m.vrbOut = vOutPk;
        m.dlyOut = dOutPk;
    }

private:
    double sampleRate = 44100.0;
    juce::Reverb reverb;
    juce::AudioBuffer<float> wet;
    std::vector<float> dl[2];
    int wp = 0;
    float curDelay = 0.0f, lpL = 0.0f, lpR = 0.0f, env = 0.0f;
};

//==============================================================================
// SFX: Pitch Widener / Chorus / Doubler / Lo-Fi Radio / Exciter
class Sfx
{
public:
    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        size = (int) (sr * 0.1) + 8;
        buf.assign ((size_t) size, 0.0f);
        wp = 0;
        ph[0] = 0.0f; ph[1] = 0.25f;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) juce::jmax (1, maxBlock), 2 };
        hpf.prepare (spec); lpf.prepare (spec);
        hpf.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        lpf.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    }

    void process (juce::AudioBuffer<float>& b, int mode, float amount)
    {
        const int n = b.getNumSamples();
        if (amount < 0.001f) return;

        struct Cfg { float baseL, depL, rateL, baseR, depR, rateR, gain; };
        static const Cfg cfgs[3] = {
            { 11.0f, 1.8f, 0.35f, 14.0f, 2.0f, 0.43f, 0.85f },   // Pitch Widener
            { 18.0f, 5.0f, 0.80f, 22.0f, 5.0f, 0.65f, 0.70f },   // Chorus
            { 24.0f, 1.2f, 0.15f, 31.0f, 1.5f, 0.12f, 0.90f } }; // Doubler

        if (mode <= 2)
        {
            const auto& c = cfgs[mode];
            const float sr = (float) sampleRate, twoPi = juce::MathConstants<float>::twoPi;
            for (int i = 0; i < n; ++i)
            {
                float* l = b.getWritePointer (0);
                float* r = b.getWritePointer (1);
                buf[(size_t) wp] = 0.5f * (l[i] + r[i]);

                const float dL = (c.baseL + c.depL * std::sin (twoPi * ph[0])) * sr * 0.001f;
                const float dR = (c.baseR + c.depR * std::sin (twoPi * ph[1])) * sr * 0.001f;
                ph[0] += c.rateL / sr; if (ph[0] >= 1.0f) ph[0] -= 1.0f;
                ph[1] += c.rateR / sr; if (ph[1] >= 1.0f) ph[1] -= 1.0f;

                l[i] += amount * c.gain * read (dL);
                r[i] += amount * c.gain * read (dR);
                wp = (wp + 1) % size;
            }
        }
        else if (mode == 3) // Lo-Fi Radio
        {
            hpf.setCutoffFrequency (380.0f);
            lpf.setCutoffFrequency (3400.0f);
            for (int c = 0; c < 2; ++c)
            {
                auto* d = b.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                {
                    float x = lpf.processSample (c, hpf.processSample (c, d[i]));
                    x = std::tanh (2.0f * x) * 0.7f;
                    d[i] = d[i] * (1.0f - amount) + x * amount;
                }
            }
        }
        else // Exciter
        {
            hpf.setCutoffFrequency (3500.0f);
            for (int c = 0; c < 2; ++c)
            {
                auto* d = b.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                {
                    const float h = hpf.processSample (c, d[i]);
                    d[i] += amount * std::tanh (3.0f * h) * 0.6f;
                }
            }
        }
    }

private:
    float read (float delaySamples) const
    {
        float rp = (float) wp - delaySamples;
        while (rp < 0.0f) rp += (float) size;
        int i0 = (int) rp;
        const float fr = rp - (float) i0;
        const int i1 = (i0 + 1) % size;
        i0 %= size;
        return buf[(size_t) i0] * (1.0f - fr) + buf[(size_t) i1] * fr;
    }

    double sampleRate = 44100.0;
    std::vector<float> buf;
    int size = 1, wp = 0;
    float ph[2] { 0.0f, 0.25f };
    juce::dsp::StateVariableTPTFilter<float> hpf, lpf;
};

} // namespace v44
