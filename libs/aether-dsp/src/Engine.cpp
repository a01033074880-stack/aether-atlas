// AETHER DSP, MIT licensed. Original lightweight algorithms; no external model or sample data.
#include <aether/Engine.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace aether
{
namespace
{
constexpr double pi = 3.14159265358979323846;
float clean(float x, float limit = 32.f) noexcept
{
    return std::isfinite(x) ? std::clamp(x, -limit, limit) : 0.f;
}
float bounded(float x, float lo, float hi, float fallback) noexcept
{
    return std::isfinite(x) ? std::clamp(x, lo, hi) : fallback;
}
float db(float x) noexcept
{
    return std::pow(10.f, x * .05f);
}
float zap(float x) noexcept
{
    return std::abs(x) < 1e-20f ? 0.f : x;
}
struct Biquad
{
    float b0 = 0, b2 = 0, a1 = 0, a2 = 0;
    std::array<float, 2> z1{}, z2{};
    void configure(double sr, double f, double q) noexcept
    {
        const double w = 2 * pi * std::min(f, sr * .45) / sr, alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
        b0 = float(alpha / a0);
        b2 = -b0;
        a1 = float(-2 * std::cos(w) / a0);
        a2 = float((1 - alpha) / a0);
    }
    float tick(float x, int ch) noexcept
    {
        float y = b0 * x + z1[ch];
        z1[ch] = zap(-a1 * y + z2[ch]);
        z2[ch] = zap(b2 * x - a2 * y);
        return y;
    }
    void reset() noexcept
    {
        z1 = {};
        z2 = {};
    }
};
struct Voice
{
    bool active = false, held = false, deferred = false;
    int note = 0, channel = 0;
    float velocity = 0, envelope = 0;
    double phase = 0;
    std::uint64_t age = 0;
};
struct Mode
{
    double re = 0, im = 0, c = 1, s = 0, r = 0;
};
float shape(float x, int model) noexcept
{
    if (model == 1)
        return x / std::sqrt(1 + x * x);
    if (model == 2)
        return x / (1 + std::abs(x));
    return std::tanh(x);
}
double primitive(double x, int model) noexcept
{
    if (model == 1)
        return std::sqrt(1 + x * x);
    double a = std::abs(x);
    if (model == 2)
        return a - std::log1p(a);
    return a + std::log1p(std::exp(-2 * a)) - std::log(2.);
}
Parameters sanitise(Parameters p) noexcept
{
    p.inputDb = bounded(p.inputDb, -60, 24, 0);
    p.outputDb = bounded(p.outputDb, -60, 24, 0);
    p.mix = bounded(p.mix, 0, 1, 1);
    p.mode = std::clamp(p.mode, 0, 2);
    p.order = std::clamp(p.order, 0, 23);
    p.spectralAmount = bounded(p.spectralAmount, 0, 1, .35f);
    p.spectralThreshold = bounded(p.spectralThreshold, -90, 0, -24);
    p.spectralFocus = bounded(p.spectralFocus, 20, 20000, 3500);
    p.transientProtect = bounded(p.transientProtect, 0, 1, .65f);
    p.modalMix = bounded(p.modalMix, 0, 1, 0);
    p.modalDecay = bounded(p.modalDecay, .03f, 30, 1.2f);
    p.modalTune = bounded(p.modalTune, -48, 48, 0);
    p.modalBrightness = bounded(p.modalBrightness, 0, 1, .5f);
    p.modalMaterial = std::clamp(p.modalMaterial, 0, 3);
    p.driveDb = bounded(p.driveDb, 0, 36, 0);
    p.driveTone = bounded(p.driveTone, 0, 1, .5f);
    p.driveBias = bounded(p.driveBias, -1, 1, 0);
    p.driveModel = std::clamp(p.driveModel, 0, 2);
    p.spaceMix = bounded(p.spaceMix, 0, 1, .12f);
    p.spaceSize = bounded(p.spaceSize, 0, 1, .5f);
    p.spaceDecay = bounded(p.spaceDecay, .1f, 30, 2.5f);
    p.spaceDamping = bounded(p.spaceDamping, 0, 1, .5f);
    p.dispersion = bounded(p.dispersion, 0, .95f, .2f);
    p.masterWidth = bounded(p.masterWidth, 0, 2, 1);
    p.masterCeiling = bounded(p.masterCeiling, -24, 0, -.3f);
    p.masterRelease = bounded(p.masterRelease, 5, 2000, 100);
    p.dither = std::clamp(p.dither, 0, 2);
    p.modRate = bounded(p.modRate, .01f, 20, .5f);
    p.modDepth = bounded(p.modDepth, 0, 1, 0);
    p.modTarget = std::clamp(p.modTarget, 0, 4);
    p.modShape = std::clamp(p.modShape, 0, 2);
    if (!std::isfinite(p.tempo))
        p.tempo = 120;
    p.tempo = std::clamp(p.tempo, 20., 400.);
    return p;
}
} // namespace
struct Engine::Impl
{
    double sr = 48000;
    bool prepared = false;
    Parameters p{}, s{};
    float smooth = .004f, envAttack = .004f, envSlow = .0002f, trackCoeff = .2f, midiAttack = .004f,
          midiRelease = .0003f;
    bool dirty = true;
    std::array<Biquad, 24> filters{};
    std::array<float, 24> frequencies{}, levels{}, reductions{}, focusWeight{};
    std::array<std::array<Mode, 12>, 2> modes{};
    std::array<Voice, 8> voices{};
    std::array<bool, 16> pedal{};
    std::array<float, 16> bends{};
    std::uint64_t age = 0;
    std::array<std::vector<float>, 8> delays{};
    std::array<size_t, 8> positions{};
    std::array<int, 8> lengths{};
    std::array<float, 8> damp{}, diffuse{}, feedback{};
    std::array<float, 2> prevDrive{}, tone{}, dcX{}, dcY{};
    std::uint32_t noise = 0x51a79d3bu;
    float synthNoise = 0;
    float dcPole = .9987f; // 10 Hz DC rejection, calculated from sample rate in prepare.
    float inputGain = 1, outputGain = 1, driveGain = 1, toneCoeff = .5f, release = .001f, ceiling = 1, limiter = 1,
          gr = 0;
    float envelope = 0, slowEnvelope = 0, pitch = 0, trackerLP = 0, lastTracker = 0;
    double crossing = 0, lastCross = -1;
    std::uint64_t clock = 0;
    int goodCrossings = 0;
    double modPhase = 0, resynthPhase = 0;
    float hold = 0, modValue = 0;
    std::uint32_t control = 0;
    std::array<int, 4> order{0, 1, 2, 3};
    float random() noexcept
    {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;
        return float(noise >> 8) * (1.f / 16777216.f);
    }
    void reset() noexcept
    {
        s = p;
        for (auto &f : filters)
            f.reset();
        levels = {};
        reductions = {};
        modes = {};
        voices = {};
        pedal = {};
        bends = {};
        age = 0;
        for (auto &d : delays)
        {
            std::fill(d.begin(), d.end(), 0.f);
        }
        positions = {};
        lengths = {};
        damp = {};
        diffuse = {};
        prevDrive = {};
        tone = {};
        dcX = {};
        dcY = {};
        noise = 0x51a79d3bu;
        synthNoise = 0;
        limiter = 1;
        gr = 0;
        envelope = slowEnvelope = pitch = trackerLP = lastTracker = 0;
        crossing = 0;
        lastCross = -1;
        clock = 0;
        goodCrossings = 0;
        modPhase = resynthPhase = 0;
        hold = 0;
        modValue = 0;
        control = 0;
        coefficients();
    }
    void smoothParameters() noexcept
    {
#define AETHER_SMOOTH(x)                                                                                               \
    {                                                                                                                  \
        float next = s.x + smooth * (p.x - s.x);                                                                       \
        dirty = dirty || next != s.x;                                                                                  \
        s.x = next;                                                                                                    \
    }
        AETHER_SMOOTH(inputDb);
        AETHER_SMOOTH(outputDb);
        AETHER_SMOOTH(mix);
        AETHER_SMOOTH(spectralAmount);
        AETHER_SMOOTH(spectralThreshold);
        AETHER_SMOOTH(spectralFocus);
        AETHER_SMOOTH(transientProtect);
        AETHER_SMOOTH(modalMix);
        AETHER_SMOOTH(modalDecay);
        AETHER_SMOOTH(modalTune);
        AETHER_SMOOTH(modalBrightness);
        AETHER_SMOOTH(driveDb);
        AETHER_SMOOTH(driveTone);
        AETHER_SMOOTH(driveBias);
        AETHER_SMOOTH(spaceMix);
        AETHER_SMOOTH(spaceSize);
        AETHER_SMOOTH(spaceDecay);
        AETHER_SMOOTH(spaceDamping);
        AETHER_SMOOTH(dispersion);
        AETHER_SMOOTH(masterWidth);
        AETHER_SMOOTH(masterRelease);
        AETHER_SMOOTH(modRate);
        AETHER_SMOOTH(modDepth);
#undef AETHER_SMOOTH
    }
    void coefficients() noexcept
    {
        inputGain = db(s.inputDb);
        outputGain = db(s.outputDb);
        ceiling = db(p.masterCeiling);
        release = float(1 - std::exp(-1 / (.001 * s.masterRelease * sr)));
        float drive = s.driveDb + (p.modTarget == 1 ? modValue * 18 : 0);
        driveGain = db(std::clamp(drive, 0.f, 36.f));
        toneCoeff = float(1 - std::exp(-2 * pi * (700 + 17000 * s.driveTone * s.driveTone) / sr));
        float focus = s.spectralFocus * std::exp2(p.modTarget == 4 ? modValue * 2 : 0);
        for (size_t i = 0; i < 24; ++i)
            focusWeight[i] = .25f + .75f * std::exp(-.5f * std::pow(std::log2(frequencies[i] / focus) / 1.7f, 2.f));
        constexpr double ratios[4][12] = {
            {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12},
            {1, 1.594, 2.135, 2.653, 2.917, 3.598, 4.06, 4.53, 5.08, 5.43, 6.1, 6.77},
            {1, 2.756, 5.404, 8.933, 13.34, 18.64, 24.81, 31.87, 39.81, 48.62, 58.31, 68.87},
            {1, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.6, 3.65, 4.06, 4.23}};
        double base = (p.mode == 1 && pitch > 35 ? pitch : 110) *
                      std::exp2((s.modalTune + (p.modTarget == 2 ? modValue * 12 : 0)) / 12.);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 12; ++i)
            {
                auto &m = modes[ch][i];
                double frequency = base * ratios[p.modalMaterial][i] * (ch ? 1.001 : 1.);
                frequency = std::min(frequency, sr * .44);
                double w = 2 * pi * frequency / sr;
                m.c = std::cos(w);
                m.s = std::sin(w);
                m.r = std::exp(-6.907755 / (sr * s.modalDecay / (1 + i * (1 - s.modalBrightness) * .3)));
            }
        constexpr double seconds[8] = {.0297, .0371, .0411, .0437, .0533, .0617, .0713, .0799};
        for (size_t i = 0; i < 8; ++i)
        {
            if (delays[i].empty())
                continue;
            if (!p.freeze || lengths[i] < 1)
                lengths[i] =
                    std::clamp(int(sr * seconds[i] * (.45 + 1.35 * s.spaceSize)), 1, int(delays[i].size()) - 1);
            feedback[i] = p.freeze ? .9999995f : float(std::exp(-6.907755 * lengths[i] / (sr * s.spaceDecay)));
        }
    }
    void analyse(float x) noexcept
    {
        float a = std::abs(x);
        envelope += envAttack * (a - envelope);
        slowEnvelope += envSlow * (a - slowEnvelope);
        // A lowpass crossing tracker: deterministic, low latency, intended for monophonic sources.
        trackerLP += trackCoeff * (x - trackerLP);
        if (lastTracker <= 0 && trackerLP > 0 && envelope > .002f)
        {
            double now = double(clock) - trackerLP / (trackerLP - lastTracker);
            if (lastCross >= 0)
            {
                double period = now - lastCross;
                float hz = float(sr / period);
                if (hz >= 35 && hz <= 2000)
                {
                    if (pitch == 0 || std::abs(hz - pitch) < pitch * .2f)
                    {
                        pitch = pitch == 0 ? hz : pitch + .15f * (hz - pitch);
                        ++goodCrossings;
                    }
                    else if (++goodCrossings > 12)
                    {
                        pitch = hz;
                        goodCrossings = 0;
                    }
                }
            }
            lastCross = now;
        }
        lastTracker = trackerLP;
        if (envelope < 1e-5f)
        {
            pitch = 0;
            lastCross = -1;
            goodCrossings = 0;
        }
        ++clock;
    }
    float voicesSample() noexcept
    {
        float sum = 0;
        for (auto &v : voices)
        {
            if (!v.active)
                continue;
            float target = v.held || v.deferred ? 1.f : 0.f;
            float a = target > v.envelope ? midiAttack : midiRelease;
            v.envelope += a * (target - v.envelope);
            if (target == 0 && v.envelope < 1e-6f)
            {
                v = {};
                continue;
            }
            double f = 440 * std::exp2((v.note - 69 + bends[v.channel] + s.modalTune) / 12.);
            f = std::min(f, sr * .42);
            v.phase += f / sr;
            if (v.phase >= 1)
                v.phase -= 1;
            double angle = 2 * pi * v.phase;
            float fundamental = float(std::sin(angle));
            float h2 = f * 2 < sr * .45 ? float(std::sin(angle * 2)) : 0;
            float h3 = f * 3 < sr * .45 ? float(std::sin(angle * 3)) : 0;
            sum += v.velocity * v.envelope * (fundamental + s.modalBrightness * (.35f * h2 + .15f * h3)) * .15f;
        }
        return sum;
    }
    void spectral(float &l, float &r, bool apply = true) noexcept
    {
        float correctionL = 0, correctionR = 0;
        float threshold = db(s.spectralThreshold);
        float transient = std::clamp((envelope - slowEnvelope) / (slowEnvelope + .005f), 0.f, 1.f);
        float protect = 1 - s.transientProtect * transient;
        for (size_t i = 0; i < 24; ++i)
        {
            float a = filters[i].tick(l, 0), b = filters[i].tick(r, 1);
            float amplitude = std::max(std::abs(a), std::abs(b));
            levels[i] += float(amplitude > levels[i] ? .01 : .0005) * (amplitude - levels[i]);
            float reduction = levels[i] > threshold ? 1 - threshold / (levels[i] + 1e-12f) : 0;
            reductions[i] += .002f * (reduction - reductions[i]);
            float g = reductions[i] * s.spectralAmount * focusWeight[i] * protect;
            correctionL += a * g;
            correctionR += b * g;
        }
        if (apply)
        {
            l -= .72f * correctionL;
            r -= .72f * correctionR;
        }
    }
    // Generated source is produced once, before all four ordered effects. Modal never overwrites it.
    float generatedSource(float midi) noexcept
    {
        if (p.mode == 2)
            return midi;
        resynthPhase += (pitch > 0 ? pitch : 110) / sr;
        resynthPhase -= std::floor(resynthPhase);
        synthNoise += .18f * ((random() * 2 - 1) - synthNoise);
        return std::min(envelope * 2, 1.f) * (float(std::sin(2 * pi * resynthPhase)) * .65f + synthNoise * .15f) + midi;
    }
    void modal(float &l, float &r, float excitation) noexcept
    {
        float out[2]{};
        for (int ch = 0; ch < 2; ++ch)
        {
            double coupled = 0;
            for (int i = 0; i < 12; ++i)
            {
                auto &m = modes[ch][i];
                double re = m.r * (m.re * m.c - m.im * m.s), im = m.r * (m.re * m.s + m.im * m.c);
                double injection = excitation * .04 / (1 + i * (1 - s.modalBrightness) * .45) + coupled * .00005;
                m.re = std::clamp(re + injection, -4., 4.);
                m.im = std::clamp(im, -4., 4.);
                if (std::abs(m.re) + std::abs(m.im) < 1e-20)
                    m.re = m.im = 0;
                coupled = im;
                out[ch] += float(m.re) * .18f;
            }
        }
        l += (out[0] - l) * s.modalMix;
        r += (out[1] - r) * s.modalMix;
    }
    float colour(float x, int ch) noexcept
    {
        float u = std::clamp(x * driveGain + s.driveBias, -128.f, 128.f);
        double delta = double(u) - prevDrive[ch];
        float y = std::abs(delta) > 1e-4
                      ? float((primitive(u, p.driveModel) - primitive(prevDrive[ch], p.driveModel)) / delta)
                      : shape((u + prevDrive[ch]) * .5f, p.driveModel);
        prevDrive[ch] = u;
        y -= shape(s.driveBias, p.driveModel);
        tone[ch] += toneCoeff * (y - tone[ch]);
        float dc = tone[ch] - dcX[ch] + dcPole * dcY[ch];
        dcX[ch] = tone[ch];
        dcY[ch] = zap(dc);
        return dc;
    }
    void space(float &l, float &r) noexcept
    {
        std::array<float, 8> taps{};
        float sum = 0;
        for (size_t i = 0; i < 8; ++i)
        {
            taps[i] = delays[i][positions[i]];
            sum += taps[i];
        }
        float wetL = 0, wetR = 0;
        for (size_t i = 0; i < 8; ++i)
        {
            float y = taps[i] - .25f * sum;
            if (!p.freeze)
            {
                float a = .08f + .9f * (1 - s.spaceDamping);
                damp[i] += a * (y - damp[i]);
                y = damp[i];
            }
            float a = s.dispersion * .7f;
            float spread = -a * y + diffuse[i];
            diffuse[i] = zap(y + a * spread);
            float input = p.freeze ? 0.f : ((i & 1) ? r : l) * .18f;
            delays[i][positions[i]] = zap(clean(input + spread * feedback[i], 8));
            if (++positions[i] >= size_t(lengths[i]))
                positions[i] = 0;
            wetL += taps[i] * (i & 1 ? -.25f : .25f);
            wetR += taps[i] * (i & 2 ? -.25f : .25f);
        }
        float mix = std::clamp(s.spaceMix + (p.modTarget == 3 ? modValue * .5f : 0), 0.f, 1.f);
        l = (1 - mix) * l + mix * wetL;
        r = (1 - mix) * r + mix * wetR;
    }
    void master(float &l, float &r) noexcept
    {
        float mid = .5f * (l + r), side = .5f * (l - r) * s.masterWidth;
        l = mid + side;
        r = mid - side;
        float peak = std::max(std::abs(l), std::abs(r));
        float needed = peak > ceiling ? ceiling / peak : 1;
        if (needed < limiter)
            limiter = needed;
        else
            limiter += release * (needed - limiter);
        l *= limiter;
        r *= limiter;
        gr = -20 * std::log10(std::max(limiter, 1e-8f));
        if (p.dither)
        {
            float step = p.dither == 1 ? 1.f / 32768 : 1.f / 8388608;
            l += (random() - random()) * step;
            r += (random() - random()) * step;
        }
        l = std::clamp(l, -ceiling, ceiling);
        r = std::clamp(r, -ceiling, ceiling);
    }
};
Engine::Engine() : impl(std::make_unique<Impl>())
{
}
Engine::~Engine() = default;
void Engine::prepare(double sampleRate, int maxBlockSize)
{
    (void)maxBlockSize;
    auto &d = *impl;
    d.sr = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000., 384000.) : 48000;
    d.smooth = float(1 - std::exp(-1 / (d.sr * .005)));
    d.envAttack = d.smooth;
    d.envSlow = float(1 - std::exp(-1 / (d.sr * .08)));
    d.trackCoeff = float(1 - std::exp(-2 * pi * 1800 / d.sr));
    d.dcPole = float(std::exp(-2 * pi * 10 / d.sr));
    d.midiAttack = float(1 - std::exp(-1 / (d.sr * .004)));
    d.midiRelease = float(1 - std::exp(-1 / (d.sr * .07)));
    for (size_t i = 0; i < 24; ++i)
    {
        d.frequencies[i] = float(35 * std::pow(std::min(18000., d.sr * .44) / 35, double(i) / 23));
        d.filters[i].configure(d.sr, d.frequencies[i], 4.0);
    }
    for (auto &line : d.delays)
        line.assign(size_t(d.sr * .17) + 4, 0);
    d.prepared = true;
    d.reset();
}
void Engine::reset() noexcept
{
    impl->reset();
}
void Engine::setParameters(const Parameters &p) noexcept
{
    auto &d = *impl;
    d.p = sanitise(p);
    d.dirty = true;
    d.order = {0, 1, 2, 3};
    for (int i = 0; i < d.p.order; ++i)
        std::next_permutation(d.order.begin(), d.order.end());
}
void Engine::process(float *left, float *right, const float *sidechain, int samples) noexcept
{
    if (samples <= 0 || !left || !right)
        return;
    auto &d = *impl;
    for (int i = 0; i < samples; ++i)
    {
        float dryL = clean(left[i]), dryR = clean(right[i]);
        if (!d.prepared)
        {
            left[i] = dryL;
            right[i] = dryR;
            continue;
        }
        d.smoothParameters();
        double rate = d.p.modSync ? d.p.tempo / 60 * d.s.modRate : d.s.modRate;
        d.modPhase += rate / d.sr;
        if (d.modPhase >= 1)
        {
            d.modPhase -= 1;
            d.hold = d.random() * 2 - 1;
        }
        float wave = d.s.modDepth == 0   ? 0
                     : d.p.modShape == 1 ? float(1 - 4 * std::abs(d.modPhase - .5))
                     : d.p.modShape == 2 ? d.hold
                                         : float(std::sin(2 * pi * d.modPhase));
        d.modValue = wave * d.s.modDepth;
        if (d.control++ % 32 == 0 && (d.dirty || d.s.modDepth > 0 || d.p.mode == 1))
        {
            d.coefficients();
            d.dirty = false;
        }
        float l = dryL * d.inputGain, r = dryR * d.inputGain;
        float excitation = sidechain ? clean(sidechain[i]) : .5f * (l + r);
        d.analyse(excitation);
        float midi = d.voicesSample() * d.inputGain;
        if (d.p.mode != 0)
            l = r = d.generatedSource(midi);
        if (!d.p.enabled[0])
            d.spectral(l, r, false);

        for (int stage : d.order)
        {
            if (!d.p.enabled[stage])
                continue;
            switch (stage)
            {
            case 0:
                d.spectral(l, r);
                break;
            case 1:
                // External sidechain intentionally overrides normal serial signal excitation.
                d.modal(l, r, sidechain ? excitation : .5f * (l + r));
                break;
            case 2:
                l = d.colour(l, 0);
                r = d.colour(r, 1);
                break;
            case 3:
                d.space(l, r);
                break;
            }
        }
        l = ((1 - d.s.mix) * dryL + d.s.mix * l) * d.outputGain;
        r = ((1 - d.s.mix) * dryR + d.s.mix * r) * d.outputGain;
        if (d.p.enabled[4])
            d.master(l, r);
        left[i] = clean(l);
        right[i] = clean(r);
    }
}
void Engine::noteOn(int note, float velocity, int channel) noexcept
{
    if (note < 0 || note > 127 || channel < 1 || channel > 16)
        return;
    if (!std::isfinite(velocity) || velocity <= 0)
    {
        noteOff(note, channel);
        return;
    }
    auto &d = *impl;
    Voice *chosen = nullptr;
    for (auto &v : d.voices)
        if (!v.active)
        {
            chosen = &v;
            break;
        }
    if (!chosen)
        chosen = &*std::min_element(d.voices.begin(), d.voices.end(),
                                    [](const Voice &a, const Voice &b) { return a.age < b.age; });
    *chosen = {true, true, false, note, channel - 1, std::clamp(velocity, 0.f, 1.f), 0, 0, ++d.age};
}
void Engine::noteOff(int note, int channel) noexcept
{
    if (channel < 1 || channel > 16)
        return;
    auto &d = *impl;
    for (auto &v : d.voices)
        if (v.active && v.note == note && v.channel == channel - 1)
        {
            v.held = false;
            v.deferred = d.pedal[channel - 1];
        }
}
void Engine::sustain(bool down, int channel) noexcept
{
    if (channel < 1 || channel > 16)
        return;
    impl->pedal[channel - 1] = down;
    if (!down)
        for (auto &v : impl->voices)
            if (v.channel == channel - 1)
                v.deferred = false;
}
void Engine::pitchBend(float semitones, int channel) noexcept
{
    if (channel >= 1 && channel <= 16)
        impl->bends[channel - 1] = bounded(semitones, -48, 48, 0);
}
void Engine::allNotesOff() noexcept
{
    impl->voices = {};
    impl->pedal = {};
}
std::array<float, 24> Engine::bandLevels() const noexcept
{
    return impl->levels;
}
float Engine::pitchHz() const noexcept
{
    return impl->pitch;
}
float Engine::gainReductionDb() const noexcept
{
    return impl->gr;
}
} // namespace aether
