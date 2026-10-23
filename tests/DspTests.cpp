#include <aether/Engine.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <vector>
static std::atomic<size_t> allocations{0};
// Keep replacements opaque to GCC's allocation-pair diagnostic; malloc/free are deliberately paired.
#if defined(_MSC_VER)
#define AETHER_TEST_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define AETHER_TEST_NOINLINE __attribute__((noinline))
#else
#define AETHER_TEST_NOINLINE
#endif
void *operator new(std::size_t n)
{
    ++allocations;
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n)
{
    return ::operator new(n);
}
AETHER_TEST_NOINLINE void operator delete(void *p) noexcept
{
    std::free(p);
}
AETHER_TEST_NOINLINE void operator delete[](void *p) noexcept
{
    std::free(p);
}
AETHER_TEST_NOINLINE void operator delete(void *p, std::size_t) noexcept
{
    std::free(p);
}
AETHER_TEST_NOINLINE void operator delete[](void *p, std::size_t) noexcept
{
    std::free(p);
}
namespace
{
int failures = 0;
void check(bool ok, const char *name)
{
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok)
        ++failures;
}
float rms(const std::vector<float> &a, size_t begin = 0)
{
    double s = 0;
    for (size_t i = begin; i < a.size(); ++i)
        s += double(a[i]) * a[i];
    return float(std::sqrt(s / std::max(size_t(1), a.size() - begin)));
}
aether::Parameters bare()
{
    aether::Parameters p;
    for (auto &e : p.enabled)
        e = false;
    return p;
}
void run(aether::Engine &e, std::vector<float> &l, std::vector<float> &r, int block = 127, const float *sc = nullptr)
{
    for (int i = 0; i < int(l.size()); i += block)
        e.process(l.data() + i, r.data() + i, sc ? sc + i : nullptr, std::min(block, int(l.size()) - i));
}
std::vector<float> tone(size_t n, float hz = 1000, float amp = .25f, float sr = 48000)
{
    std::vector<float> a(n);
    for (size_t i = 0; i < n; ++i)
        a[i] = amp * std::sin(6.28318530718f * hz * float(i) / sr);
    return a;
}
} // namespace
int main()
{
    {
        aether::Engine e;
        e.prepare(48000, 16);
        e.setParameters(bare());
        auto l = tone(20000), r = l, original = l;
        run(e, l, r, 701);
        check(l == original && r == original, "disabled stages are sample transparent for oversized blocks");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        auto p = bare();
        p.enabled[0] = true;
        p.spectralAmount = 1;
        p.spectralThreshold = -50;
        p.spectralFocus = 1000;
        p.transientProtect = 0;
        e.setParameters(p);
        auto l = tone(48000), r = l;
        run(e, l, r);
        check(rms(l, 24000) < .145f, "spectral stage attenuates sustained 1 kHz resonance");
        auto bands = e.bandLevels();
        check(*std::max_element(bands.begin(), bands.end()) > .02f, "analyser measures actual audio");
        check(e.pitchHz() > 950 && e.pitchHz() < 1050, "pitch tracker follows 1 kHz tone");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        auto p = bare();
        p.enabled[1] = true;
        p.modalMix = 1;
        p.modalDecay = .35f;
        e.setParameters(p);
        std::vector<float> l(96000), r(l);
        l[2000] = r[2000] = 1;
        run(e, l, r);
        double early = 0, late = 0;
        for (size_t i = 3000; i < 12000; ++i)
            early += l[i] * l[i];
        for (size_t i = 80000; i < 96000; ++i)
            late += l[i] * l[i];
        check(early > 1e-6 && late < early * .01, "modal impulse rings and decays");
    }
    {
        aether::Engine e;
        e.prepare(48000, 128);
        auto p = bare();
        p.mode = 2;
        p.enabled[1] = true;
        e.setParameters(p);
        std::vector<float> l(12000), r(l);
        run(e, l, r);
        check(rms(l) == 0, "synth is silent before MIDI");
        e.noteOn(69, 1);
        std::fill(l.begin(), l.end(), 0.f);
        r = l;
        run(e, l, r);
        check(rms(l) > .01, "MIDI creates audible A4 voice");
        e.sustain(true);
        e.noteOff(69);
        std::fill(l.begin(), l.end(), 0.f);
        r = l;
        run(e, l, r);
        check(rms(l) > .01, "sustain holds released MIDI voice");
        e.sustain(false);
        l.assign(96000, 0);
        r = l;
        run(e, l, r);
        check(rms(l, 90000) < 1e-5, "MIDI release becomes silent");
        e.noteOn(60, 1);
        e.allNotesOff();
        l.assign(1000, 0);
        r = l;
        run(e, l, r);
        check(rms(l) == 0, "panic kills all voice state");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        auto p = bare();
        p.enabled[2] = true;
        p.driveDb = 30;
        p.driveBias = .7f;
        e.setParameters(p);
        auto l = tone(24000, 440, 2), r = l;
        run(e, l, r);
        bool finite = true;
        for (auto x : l)
            finite &= std::isfinite(x) && std::abs(x) < 2;
        check(finite && rms(l) > .1f, "saturation stays bounded under hot biased input");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        auto p = bare();
        p.enabled[3] = true;
        p.spaceMix = 1;
        p.spaceDecay = .7f;
        e.setParameters(p);
        std::vector<float> l(12000), r(l);
        l[3000] = r[3000] = 1;
        run(e, l, r);
        check(rms(l, 10000) > 1e-5, "FDN creates audible stereo tail");
        p.freeze = true;
        e.setParameters(p);
        l.assign(96000, 0);
        r = l;
        run(e, l, r);
        check(rms(l, 72000) > 1e-6 && rms(l, 72000) < .2f, "freeze preserves bounded tail");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        auto p = bare();
        p.enabled[4] = true;
        p.masterCeiling = -6;
        e.setParameters(p);
        auto l = tone(24000, 440, 4), r = l;
        run(e, l, r);
        float peak = 0;
        for (auto x : l)
            peak = std::max(peak, std::abs(x));
        check(peak <= .502f && e.gainReductionDb() > 1, "linked limiter enforces ceiling");
        p.dither = 1;
        e.setParameters(p);
        l.assign(24000, 0);
        r = l;
        run(e, l, r);
        check(rms(l, 12000) > 1e-6f && rms(l, 12000) < .0001f, "16-bit TPDF dither generates expected low noise");
    }
    {
        aether::Engine e;
        e.prepare(48000, 1);
        auto p = bare();
        e.setParameters(p);
        std::vector<float> l = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()},
                           r = l;
        run(e, l, r);
        check(std::all_of(l.begin(), l.end(), [](float x) { return std::isfinite(x); }), "invalid input is sanitized");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        aether::Parameters p;
        p.modalMix = .5f;
        e.setParameters(p);
        auto l = tone(40000), r = l;
        run(e, l, r);
        e.reset();
        l = tone(40000);
        r = l;
        run(e, l, r);
        auto a = l;
        e.reset();
        l = tone(40000);
        r = l;
        run(e, l, r);
        check(a == l, "reset is deterministic including noise and analysis");
        auto before = allocations.load();
        e.setParameters(p);
        e.noteOn(60, .5);
        e.pitchBend(2);
        e.sustain(true);
        e.noteOff(60);
        e.sustain(false);
        e.process(l.data(), r.data(), nullptr, 40000);
        e.reset();
        check(allocations.load() == before, "process parameter reset and MIDI calls allocate no heap memory");
    }
    {
        bool good = true;
        for (double sr : {8000., 22050., 44100., 96000., 192000.})
        {
            aether::Engine e;
            e.prepare(sr, 32);
            aether::Parameters p;
            p.modalMix = .7f;
            p.driveDb = 36;
            p.spaceDecay = 30;
            p.freeze = true;
            e.setParameters(p);
            std::vector<float> l(12000, 10), r(l);
            run(e, l, r, 997);
            for (auto x : l)
                good &= std::isfinite(x) && std::abs(x) <= 1.001f;
        }
        check(good, "all stages stable across sample rates and extreme settings");
    }
    {
        aether::Engine e;
        e.reset();
        e.prepare(48000, 64);
        auto p = bare();
        p.mode = 2;
        p.enabled[1] = true;
        p.modalMix = 1;
        e.setParameters(p);
        e.reset();
        e.noteOn(69, 1);
        std::vector<float> l(24000), r(l);
        run(e, l, r);
        check(rms(l, 12000) > .001f, "full modal mix retains MIDI instrument excitation");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        e.setParameters(bare());
        auto l = tone(24000), r = l;
        run(e, l, r);
        auto b = e.bandLevels();
        check(*std::max_element(b.begin(), b.end()) > .02f, "spectrum works with spectral processing bypassed");
    }
    {
        aether::Engine a, b;
        a.prepare(48000, 64);
        b.prepare(48000, 64);
        auto p = bare();
        p.mode = 2;
        p.enabled[1] = true;
        a.setParameters(p);
        b.setParameters(p);
        for (int n = 60; n < 69; ++n)
            a.noteOn(n, .8f);
        for (int n = 61; n < 69; ++n)
            b.noteOn(n, .8f);
        std::vector<float> al(12000), ar(al), bl(al), br(al);
        run(a, al, ar);
        run(b, bl, br);
        float stealError = 0;
        for (size_t i = 0; i < al.size(); ++i)
            stealError = std::max(stealError, std::abs(al[i] - bl[i]));
        check(stealError < 1e-6f, "ninth note steals oldest of eight voices");
        a.pitchBend(12);
        al.assign(24000, 0);
        ar = al;
        run(a, al, ar);
        bool finite = std::all_of(al.begin(), al.end(), [](float x) { return std::isfinite(x); });
        check(finite && rms(al) > .01f, "polyphonic pitch bend remains audible and finite");
    }
    {
        aether::Engine a, b;
        a.prepare(48000, 64);
        b.prepare(48000, 64);
        auto p = bare();
        p.enabled[1] = true;
        p.modalMix = 1;
        a.setParameters(p);
        b.setParameters(p);
        a.reset();
        b.reset();
        std::vector<float> al(24000), ar(al), bl(al), br(al), sc(al);
        sc[1000] = 1;
        run(a, al, ar, 127, sc.data());
        run(b, bl, br);
        check(rms(al) > .00001f && rms(bl) == 0, "external sidechain excites resonators without main audio");
    }
    {
        bool finite = true;
        float difference = 0;
        std::vector<float> first;
        for (int order = 0; order < 24; ++order)
        {
            aether::Engine e;
            e.prepare(48000, 64);
            aether::Parameters p;
            p.order = order;
            p.modalMix = .6f;
            p.driveDb = 20;
            p.spaceMix = .5f;
            e.setParameters(p);
            e.reset();
            auto l = tone(4000), r = l;
            run(e, l, r);
            for (auto x : l)
                finite &= std::isfinite(x) && std::abs(x) <= 1;
            if (order == 0)
                first = l;
            if (order == 23)
                for (size_t i = 0; i < l.size(); ++i)
                    difference += std::abs(l[i] - first[i]);
        }
        check(finite && difference > 1, "24 stage permutations are stable and change the real signal");
    }
    {
        aether::Engine a, b;
        a.prepare(48000, 64);
        b.prepare(48000, 64);
        auto p = bare();
        p.enabled[2] = true;
        p.driveDb = 12;
        a.setParameters(p);
        p.modTarget = 1;
        p.modDepth = 1;
        p.modRate = 3;
        p.modSync = true;
        b.setParameters(p);
        auto al = tone(24000, 220), ar = al, bl = al, br = al;
        run(a, al, ar);
        run(b, bl, br);
        double diff = 0;
        for (size_t i = 0; i < al.size(); ++i)
            diff += std::abs(al[i] - bl[i]);
        check(diff > 10, "tempo-synced LFO audibly modulates drive");
    }
    {
        aether::Engine a, b;
        a.prepare(48000, 64);
        b.prepare(48000, 64);
        aether::Parameters p;
        a.setParameters(p);
        b.setParameters(p);
        auto al = tone(23001), ar = al, bl = al, br = al;
        run(a, al, ar, 1);
        run(b, bl, br, 997);
        check(al == bl && ar == br, "processing is independent of host block segmentation");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        auto p = bare();
        p.mode = 2;
        p.enabled[1] = true;
        e.setParameters(p);
        e.noteOn(69, 1, 2);
        std::vector<float> l(24000), r(l);
        run(e, l, r);
        e.noteOff(69, 1);
        l.assign(24000, 0);
        r = l;
        run(e, l, r);
        check(rms(l) > .01f, "MIDI note off is isolated by channel");
        e.pitchBend(12, 2);
        l.assign(24000, 0);
        r = l;
        run(e, l, r);
        int crossings = 0;
        for (size_t i = 12001; i < l.size(); ++i)
            if (l[i - 1] <= 0 && l[i] > 0)
                ++crossings;
        check(crossings >= 218 && crossings <= 222, "channel pitch bend raises A4 by exactly one octave");
    }
    {
        aether::Engine a, b;
        a.prepare(48000, 64);
        b.prepare(48000, 64);
        auto p = bare();
        p.enabled[3] = true;
        p.spaceMix = 1;
        p.spaceDecay = 3;
        a.setParameters(p);
        b.setParameters(p);
        a.reset();
        b.reset();
        std::vector<float> al(12000), ar(al), bl(al), br(al);
        al[0] = ar[0] = bl[0] = br[0] = 1;
        run(a, al, ar);
        run(b, bl, br);
        p.freeze = true;
        a.setParameters(p);
        b.setParameters(p);
        al.assign(24000, .9f);
        ar = al;
        bl.assign(24000, 0);
        br = bl;
        run(a, al, ar);
        run(b, bl, br);
        check(al == bl && ar == br, "freeze excludes all new input excitation");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        auto p = bare();
        p.mode = 1;
        p.enabled[1] = true;
        e.setParameters(p);
        auto l = tone(24000, 220), r = l;
        run(e, l, r);
        check(rms(l, 12000) > .03f && e.pitchHz() > 210 && e.pitchHz() < 230,
              "deterministic resynthesis follows monophonic audio");
        l.assign(96000, 0);
        r = l;
        run(e, l, r);
        check(rms(l, 90000) < 1e-5f, "resynthesis envelope gates after source silence");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        aether::Parameters p;
        p.inputDb = std::numeric_limits<float>::infinity();
        p.outputDb = std::numeric_limits<float>::quiet_NaN();
        p.spectralFocus = -1;
        p.modalDecay = -1;
        p.spaceDecay = std::numeric_limits<float>::quiet_NaN();
        p.driveTone = 1000;
        p.modRate = std::numeric_limits<float>::infinity();
        p.tempo = std::numeric_limits<double>::quiet_NaN();
        p.order = 2147483647;
        p.modalMaterial = -1;
        e.setParameters(p);
        e.reset();
        auto l = tone(12000), r = l;
        run(e, l, r);
        check(std::all_of(l.begin(), l.end(), [](float x) { return std::isfinite(x) && std::abs(x) <= 1; }),
              "invalid serialized parameters are bounded and sanitized");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        auto p = bare();
        p.enabled[4] = true;
        p.masterCeiling = -6;
        e.setParameters(p);
        e.reset();
        std::vector<float> l(12000, .8f), r(12000, .2f);
        run(e, l, r);
        check(std::abs(l.back() / r.back() - 4) < .0001f, "linked limiter preserves stereo amplitude ratio");
        p.dither = 2;
        e.setParameters(p);
        l.assign(12000, 0);
        r = l;
        run(e, l, r);
        check(rms(l) > 1e-8f && rms(l) < 1e-6f, "24-bit TPDF dither generates expected low noise");
    }
    {
        aether::Engine e;
        e.prepare(48000, 64);
        std::vector<float> l(12000), r(l);
        run(e, l, r);
        check(rms(l) == 0 && rms(r) == 0, "default FX processes silence without self-generated voices");
    }
    {
        aether::Engine e;
        e.prepare(48000, 256);
        auto l = tone(256), r = l;
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 1000; ++i)
            e.process(l.data(), r.data(), nullptr, 256);
        auto sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "BENCH 256000 stereo samples in " << sec << " s; realtime ratio " << sec / (256000. / 48000.)
                  << '\n';
    }
    {
        aether::Engine e;
        auto p = bare();
        p.enabled[3] = true;
        p.spaceMix = 1;
        p.freeze = true;
        e.setParameters(p);
        e.prepare(192000, 64);
        e.prepare(8000, 64);
        std::vector<float> l(24000, .5f), r(l);
        run(e, l, r);
        check(rms(l) == 0, "reprepare with freeze clears tails and adapts delay capacity");
    }
    std::cout << "Failures: " << failures << '\n';
    return failures ? 1 : 0;
}

