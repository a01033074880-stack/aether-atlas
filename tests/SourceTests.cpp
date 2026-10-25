// Behavioural tests for libs/aether-source (Phase Geometry Voice and Chaos Voice).
// Everything here is objective: no test depends on listening.
#include <aether/Chaos.h>
#include <aether/Modular.h>
#include <aether/PhaseGeometry.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

// ---- allocation counter: replaces the global allocation functions -------------
namespace {
std::atomic<std::size_t> g_allocations{0};
std::atomic<bool> g_tracking{false};
void* counted(std::size_t n) {
  if (g_tracking.load(std::memory_order_relaxed)) g_allocations.fetch_add(1, std::memory_order_relaxed);
  if (void* p = std::malloc(n ? n : 1)) return p;
  throw std::bad_alloc();
}
} // namespace
void* operator new(std::size_t n) { return counted(n); }
void* operator new[](std::size_t n) { return counted(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace aether::source;

namespace {
int passed = 0, failed = 0;
void require(bool ok, const std::string& why) {
  if (!ok) throw std::runtime_error(why);
}
template <class F> void test(const std::string& name, F f) {
  try {
    f();
    ++passed;
    std::printf("PASS %s\n", name.c_str());
  } catch (const std::exception& e) {
    ++failed;
    std::fprintf(stderr, "FAIL %s: %s\n", name.c_str(), e.what());
  }
}
std::string fmt(const char* f, double a = 0, double b = 0, double c = 0, double d = 0) {
  char buf[512];
  std::snprintf(buf, sizeof buf, f, a, b, c, d);
  return buf;
}
void note(const std::string& s) { std::printf("  %s\n", s.c_str()); }

// ---- simple FFT (radix-2, double) ------------------------------------------
void fft(std::vector<std::complex<double>>& a) {
  const std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2 * 3.14159265358979323846 / static_cast<double>(len);
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<double> w(1);
      for (std::size_t j = 0; j < len / 2; ++j) {
        const auto u = a[i + j], v = a[i + j + len / 2] * w;
        a[i + j] = u + v;
        a[i + j + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}
// Power spectrum with a 4-term Blackman-Harris window.
std::vector<double> powerSpectrum(const float* x, std::size_t n) {
  std::vector<std::complex<double>> a(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double t = 2 * 3.14159265358979323846 * static_cast<double>(i) / static_cast<double>(n);
    const double w = .35875 - .48829 * std::cos(t) + .14128 * std::cos(2 * t) - .01168 * std::cos(3 * t);
    a[i] = x[i] * w;
  }
  fft(a);
  std::vector<double> p(n / 2 + 1);
  for (std::size_t i = 0; i < p.size(); ++i) p[i] = std::norm(a[i]);
  return p;
}

// ---- generic scheduling harness -------------------------------------------
enum class Kind { noteOn, noteOff, param, bend, pedal, allOff };
struct Event {
  int pos;
  Kind kind;
  int a = 0;
  float b = 0;
};
template <class E, class P>
void render(E& e, const std::vector<P>& variants, const std::vector<Event>& events, int total, int block,
            std::vector<float>& l, std::vector<float>& r) {
  l.assign(static_cast<std::size_t>(total), 0);
  r.assign(static_cast<std::size_t>(total), 0);
  std::size_t next = 0;
  int pos = 0;
  while (pos < total) {
    while (next < events.size() && events[next].pos <= pos) {
      const auto& ev = events[next++];
      switch (ev.kind) {
      case Kind::noteOn: e.noteOn(ev.a, ev.b); break;
      case Kind::noteOff: e.noteOff(ev.a); break;
      case Kind::param: e.setParameters(variants[static_cast<std::size_t>(ev.a)]); break;
      case Kind::bend: e.pitchBend(ev.b); break;
      case Kind::pedal: e.sustain(ev.a != 0); break;
      case Kind::allOff: e.allNotesOff(); break;
      }
    }
    int end = std::min(total, pos + block);
    if (next < events.size()) end = std::min(end, events[next].pos);
    e.process(l.data() + pos, r.data() + pos, end - pos);
    pos = end;
  }
}
std::vector<Event> script() {
  std::vector<Event> ev{{0, Kind::param, 0},       {0, Kind::noteOn, 60, .8f},   {1000, Kind::noteOn, 64, .6f},
                        {3333, Kind::noteOn, 67, 1}, {9000, Kind::param, 1},       {12345, Kind::bend, 0, 1.5f},
                        {20000, Kind::noteOff, 64},  {30000, Kind::pedal, 1},      {31000, Kind::noteOff, 60},
                        {40000, Kind::pedal, 0},     {50000, Kind::noteOn, 72, .5f}};
  for (int i = 0; i < 20; ++i) ev.push_back({60000 + i * 7, Kind::noteOn, 40 + i * 3, .7f}); // forces voice stealing
  ev.push_back({75000, Kind::allOff});
  return ev;
}

// ---- engine specific parameter sets -----------------------------------------
std::vector<PhaseGeometryParameters> pgVariants() {
  PhaseGeometryParameters a, b;
  a.attack = .002f;
  a.czShape = 0; a.czDepth = .7f; a.vpsD = .3f; a.vpsV = .6f; a.walshMix = .3f; a.walshSequency = 5.5f; a.spread = .6f;
  b = a;
  b.antiAlias = false; // switching the oversampling mid-stream is part of the schedule
  b.czShape = 4; b.czDepth = .4f; b.vpsRatio = 3.3f; b.walshMix = .8f; b.walshSequency = 33.3f; b.phaseRandom = .7f; b.gain = .5f;
  return {a, b};
}
std::vector<ChaosParameters> chaosVariants() {
  ChaosParameters a, b;
  a.attack = .002f; a.segments = 12; a.distribution = 0; a.ampStep = .3f; a.spread = .6f;
  b = a;
  b.antiAlias = false;
  b.segments = 24; b.distribution = 3; b.ampStep = .6f; b.durStep = .4f; b.momentum = .6f; b.durRange = .8f; b.gain = .5f;
  return {a, b};
}

template <class E> std::unique_ptr<E> make(double rate = 48000) {
  auto e = std::make_unique<E>();
  e->prepare(rate, 512);
  return e;
}
bool sameBits(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}
double maxDiff(const std::vector<float>& a, const std::vector<float>& b, std::size_t& differing) {
  double m = 0;
  differing = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const double d = std::abs(static_cast<double>(a[i]) - b[i]);
    if (a[i] != b[i]) ++differing;
    m = std::max(m, d);
  }
  return m;
}
double energy(const std::vector<float>& x) {
  double e = 0;
  for (float v : x) e += static_cast<double>(v) * v;
  return e;
}
double hzOf(int note) { return 440 * std::pow(2.0, (note - 69) / 12.0); }

// Ratio (dB) of non-harmonic ("alias") power to harmonic power for a held note.
// ratio = non-harmonic / harmonic power in dB; aliasAbs / harmAbs = the same powers in dB relative to a +/-1 square
// wave (mean square 1), so a ratio computed from almost no harmonic energy can be told apart from a real problem.
struct AliasStat { double ratio, aliasAbs, harmAbs; };
template <class E, class P> AliasStat aliasStat(P p, bool antiAlias, int noteNumber, double rate = 48000, bool driven = false) {
  constexpr std::size_t n = 65536;
  p.antiAlias = antiAlias;
  p.attack = .001f; p.sustain = 1; p.decay = .01f; p.spread = 0; p.gain = 1;
  auto e = make<E>(rate);
  e->setParameters(p);
  std::vector<float> l(n + 20000), r(n + 20000);
  if (driven) { // Resynth drive: constant pitch, full level (same loudness as the note above)
    std::vector<float> hz(l.size(), static_cast<float>(hzOf(noteNumber))), level(l.size(), 1.f);
    e->processDriven(l.data(), r.data(), static_cast<int>(l.size()), hz.data(), level.data());
  } else {
    e->noteOn(noteNumber, 1);
    e->process(l.data(), r.data(), static_cast<int>(l.size()));
  }
  const auto spectrum = powerSpectrum(l.data() + 20000, n);
  const double binHz = rate / static_cast<double>(n), f0 = hzOf(noteNumber);
  std::vector<char> harmonic(spectrum.size(), 0), guard(spectrum.size(), 0);
  for (int k = 1; k * f0 < rate / 2; ++k) {
    const long c = std::lround(k * f0 / binHz);
    for (long b = c - 8; b <= c + 8; ++b)
      if (b >= 0 && b < static_cast<long>(spectrum.size())) guard[static_cast<std::size_t>(b)] = 1;
    for (long b = c - 6; b <= c + 6; ++b)
      if (b >= 0 && b < static_cast<long>(spectrum.size())) harmonic[static_cast<std::size_t>(b)] = 1;
  }
  double h = 0, a = 0, total = 0, ms = 0;
  for (double v : spectrum) total += v;
  for (std::size_t i = 0; i < n; ++i) ms += static_cast<double>(l[20000 + i]) * l[20000 + i];
  ms /= static_cast<double>(n);
  for (std::size_t i = 12; i < spectrum.size(); ++i) {
    if (harmonic[i]) h += spectrum[i];
    else if (!guard[i]) a += spectrum[i];
  }
  const auto db = [&](double x) { return 10 * std::log10(std::max(x * ms / total, 1e-30)); };
  return {10 * std::log10(std::max(a, 1e-30) / std::max(h, 1e-30)), db(a), db(h)};
}
template <class E, class P> double aliasDb(P p, bool antiAlias, int noteNumber, double rate = 48000) {
  return aliasStat<E, P>(p, antiAlias, noteNumber, rate).ratio;
}

template <class E, class P> void suite(const char* label, std::vector<P> variants, std::function<P(P)> aliasPrep) {
  const std::string L = label;
  test(L + ": zero allocations after prepare", [&] {
    auto e = make<E>();
    e->setParameters(variants[0]);
    // The counter itself must work, otherwise "0" would prove nothing.
    g_allocations = 0;
    g_tracking = true;
    { std::vector<int> probe(100); probe[0] = 1; }
    g_tracking = false;
    require(g_allocations.load() > 0, "allocation counter does not observe operator new");
    std::vector<float> l(700), r(700);
    g_allocations = 0;
    g_tracking = true;
    for (int i = 0; i < 24; ++i) e->noteOn(36 + i * 3, .7f, 1 + i % 3);
    for (int b = 0; b < 40; ++b) {
      e->setParameters(variants[static_cast<std::size_t>(b % 2)]);
      e->process(l.data(), r.data(), 1 + (b * 37) % 700);
      if (b % 7 == 0) { e->pitchBend(.5f * static_cast<float>(b % 5), 1 + b % 3); e->sustain(b % 2 == 0, 1); }
    }
    for (int i = 0; i < 24; ++i) e->noteOff(36 + i * 3, 1 + i % 3);
    e->process(l.data(), r.data(), 700);
    e->allNotesOff();
    e->reset();
    e->process(l.data(), r.data(), 700);
    g_tracking = false;
    note(fmt("allocations during noteOn/noteOff/setParameters/process/reset window: %.0f", static_cast<double>(g_allocations.load())));
    require(g_allocations.load() == 0, "heap allocation after prepare");
  });
  test(L + ": seed and antiAlias changes between blocks (allocation, seed semantics, antiAlias transient)", [&] {
    auto base = variants[0];
    base.attack = .001f; base.sustain = 1; base.decay = .01f; base.spread = 0;
    if constexpr (std::is_same_v<P, PhaseGeometryParameters>) base.phaseRandom = 1; // make the seed audible for Phase Geometry
    // 1) no heap allocation while seed and antiAlias change on every block, with notes starting
    {
      auto e = make<E>();
      e->setParameters(base);
      std::vector<float> l(64), r(64);
      bool allFinite = true;
      g_allocations = 0;
      g_tracking = true;
      for (int b = 0; b < 400; ++b) {
        auto q = base;
        q.seed = defaultSeed + static_cast<std::uint32_t>(b % 7);
        q.antiAlias = (b % 2) == 0;
        e->setParameters(q);
        if (b % 5 == 0) e->noteOn(48 + (b / 5) % 30, .7f);
        e->process(l.data(), r.data(), 64);
        for (float v : l) allFinite = allFinite && std::isfinite(v); // no std::string/require inside the tracked window
      }
      g_tracking = false;
      require(allFinite, "non-finite output while automating seed/antiAlias");
      note(fmt("allocations over 400 blocks with seed and antiAlias changing every block: %.0f", static_cast<double>(g_allocations.load())));
      require(g_allocations.load() == 0, "heap allocation while changing seed/antiAlias");
    }
    // 2) seed only affects notes started afterwards
    {
      auto run = [&](bool changeSeed, std::vector<float>& out) {
        auto e = make<E>();
        e->setParameters(base);
        out.assign(14400, 0);
        std::vector<float> r(14400);
        e->noteOn(60, .8f);
        e->process(out.data(), r.data(), 4800);
        if (changeSeed) { auto q = base; q.seed = defaultSeed + 777; e->setParameters(q); }
        e->process(out.data() + 4800, r.data() + 4800, 4800);
        e->noteOn(64, .8f);
        e->process(out.data() + 9600, r.data() + 9600, 4800);
      };
      std::vector<float> a, b;
      run(false, a); run(true, b);
      const std::vector<float> a1(a.begin(), a.begin() + 9600), b1(b.begin(), b.begin() + 9600);
      require(sameBits(a1, b1), "changing seed altered an already sounding voice");
      std::size_t differing = 0;
      const std::vector<float> a2(a.begin() + 9600, a.end()), b2(b.begin() + 9600, b.end());
      const double d = maxDiff(a2, b2, differing);
      note(fmt("seed change with a held note: first 9600 samples bit-identical; after the next note on, %.0f samples differ (max %.3g)", static_cast<double>(differing), d));
      require(differing > 0, "seed has no effect on a note started after the change");
    }
    // 3) antiAlias toggled at a block boundary: size and length of the transient against the steady state of the new mode
    for (const bool toOn : {false, true}) {
      constexpr int total = 48000, S = 24000;
      auto steady = [&](bool aa, std::vector<float>& out) {
        auto e = make<E>();
        auto q = base; q.antiAlias = aa;
        e->setParameters(q);
        out.assign(total, 0);
        std::vector<float> r(total);
        e->noteOn(72, .8f);
        for (int p = 0; p < total; p += 480) e->process(out.data() + p, r.data() + p, 480);
      };
      std::vector<float> oldMode, newMode, sw(total, 0), r(total);
      steady(!toOn, oldMode); steady(toOn, newMode);
      {
        auto e = make<E>();
        auto q = base; q.antiAlias = !toOn;
        e->setParameters(q);
        e->noteOn(72, .8f);
        for (int p = 0; p < total; p += 480) {
          if (p == S) { q.antiAlias = toOn; e->setParameters(q); }
          e->process(sw.data() + p, r.data() + p, 480);
        }
      }
      double peak = 0, steadyStep = 0, switchStep = 0;
      for (int i = S - 4800; i < S; ++i) { peak = std::max(peak, static_cast<double>(std::abs(oldMode[static_cast<std::size_t>(i)]))); steadyStep = std::max(steadyStep, static_cast<double>(std::abs(oldMode[static_cast<std::size_t>(i)] - oldMode[static_cast<std::size_t>(i) - 1]))); }
      for (int i = S; i < S + 8; ++i) switchStep = std::max(switchStep, static_cast<double>(std::abs(sw[static_cast<std::size_t>(i)] - sw[static_cast<std::size_t>(i) - 1])));
      double e0 = 0, e1 = 0, e2 = 0;
      for (int i = S; i < S + 64; ++i) e0 = std::max(e0, static_cast<double>(std::abs(sw[static_cast<std::size_t>(i)] - newMode[static_cast<std::size_t>(i)])));
      for (int i = S + 64; i < S + 480; ++i) e1 = std::max(e1, static_cast<double>(std::abs(sw[static_cast<std::size_t>(i)] - newMode[static_cast<std::size_t>(i)])));
      for (int i = S + 480; i < S + 4800; ++i) e2 = std::max(e2, static_cast<double>(std::abs(sw[static_cast<std::size_t>(i)] - newMode[static_cast<std::size_t>(i)])));
      for (float v : sw) require(std::isfinite(v), "non-finite output after antiAlias toggle");
      note(fmt(toOn ? "AA off->on: peak %.3f, steady max step %.4f, max step in first 8 samples after switch %.4f" : "AA on->off: peak %.3f, steady max step %.4f, max step in first 8 samples after switch %.4f", peak, steadyStep, switchStep));
      note(fmt("   |switched - steady new mode| max: first 64 samples %.4f, samples 64..480 %.5f, 480..4800 %.6f", e0, e1, e2));
      require(e2 < 1e-3 * std::max(peak, 1e-3), "output did not converge to the new mode's steady state within 100 ms");
      require(switchStep < 4 * std::max(peak, 1e-3), "antiAlias toggle produced an out-of-range step");
    }
  });
  test(L + ": deterministic, same seed/params render bit-identical twice", [&] {
    std::vector<float> l1, r1, l2, r2;
    auto a = make<E>();
    render(*a, variants, script(), 96000, 480, l1, r1);
    auto b = make<E>();
    render(*b, variants, script(), 96000, 480, l2, r2);
    require(energy(l1) > 1e-3, "render is silent");
    require(sameBits(l1, l2) && sameBits(r1, r2), "two renders differ");
    // reset() on a used instance must return to the same state
    a->reset();
    std::vector<float> l3, r3;
    render(*a, variants, script(), 96000, 480, l3, r3);
    require(sameBits(l1, l3) && sameBits(r1, r3), "render after reset differs");
    // a different seed must change the result
    auto seeded = variants;
    for (auto& v : seeded) v.seed = 12345;
    auto c = make<E>();
    std::vector<float> l4, r4;
    render(*c, seeded, script(), 96000, 480, l4, r4);
    require(!sameBits(l1, l4), "seed has no effect");
    note(fmt("96000 samples per channel compared bitwise, signal energy %.3f", energy(l1)));
  });
  test(L + ": output independent of host block size (1, 64, 97, 480, whole)", [&] {
    std::vector<float> rl, rr;
    auto ref = make<E>();
    render(*ref, variants, script(), 96000, 480, rl, rr);
    for (int block : {1, 64, 97, 4096, 96000}) {
      std::vector<float> l, r;
      auto e = make<E>();
      render(*e, variants, script(), 96000, block, l, r);
      std::size_t dl = 0, dr = 0;
      const double ml = maxDiff(rl, l, dl), mr = maxDiff(rr, r, dr);
      note(fmt("block %.0f vs 480: differing samples L/R = %.0f/%.0f, max abs diff = %.3g", block, static_cast<double>(dl), static_cast<double>(dr), std::max(ml, mr)));
      require(sameBits(rl, l) && sameBits(rr, r), "block size changed the output");
    }
  });
  test(L + ": extreme / hostile parameters stay finite and bounded", [&] {
    Pcg32 rng(7, 3);
    auto e = make<E>();
    const float hostile[] = {-1e30f, -100, -1, 0, .25f, .5f, 1, 7, 1e30f, std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
    double peak = 0;
    std::vector<float> l(701), r(701);
    for (int iteration = 0; iteration < 300; ++iteration) {
      P p = variants[static_cast<std::size_t>(iteration % 2)];
      auto pick = [&] { return hostile[rng.next() % (sizeof hostile / sizeof hostile[0])]; };
      p.attack = pick(); p.decay = pick(); p.sustain = pick(); p.release = pick();
      p.gain = (iteration % 3 == 0) ? 1.f : pick(); p.spread = pick(); p.transpose = pick();
      p.antiAlias = rng.next() & 1;
      p.seed = rng.next();
      if constexpr (std::is_same_v<P, PhaseGeometryParameters>) {
        p.walshSequency = pick(); p.walshMix = pick(); p.czShape = static_cast<int>(rng.next() % 12) - 3;
        p.czDepth = pick(); p.czEnvelope = pick(); p.czVelocity = pick(); p.vpsD = pick(); p.vpsV = pick();
        p.vpsRatio = pick(); p.phaseRandom = pick();
      } else {
        p.segments = static_cast<int>(rng.next() % 80) - 10; p.distribution = static_cast<int>(rng.next() % 12) - 3;
        p.ampStep = pick(); p.durStep = pick(); p.durRange = pick(); p.momentum = pick(); p.ampBarrier = pick();
      }
      e->setParameters(p);
      if (iteration % 5 == 0) e->pitchBend(pick(), 1);
      for (int k = 0; k < 6; ++k) e->noteOn(static_cast<int>(rng.next() % 128), pick(), 1);
      if (iteration % 11 == 0) e->noteOn(static_cast<int>(rng.next() % 128), .9f, 17); // invalid channel
      e->process(l.data(), r.data(), 701);
      for (std::size_t i = 0; i < 701; ++i) {
        require(std::isfinite(l[i]) && std::isfinite(r[i]), "non finite sample");
        peak = std::max({peak, std::abs(static_cast<double>(l[i])), std::abs(static_cast<double>(r[i]))});
      }
      if (iteration % 9 == 0) e->allNotesOff();
    }
    note(fmt("300 hostile parameter sets, peak |x| = %.4f (limit 16)", peak));
    require(peak <= 16.0, "unbounded output");
    // every MIDI note at many sample rates (including invalid ones)
    double peakRate = 0;
    for (double rate : {8000., 22050., 44100., 48000., 96000., 192000., 5., 1e9, std::numeric_limits<double>::quiet_NaN()}) {
      auto f = make<E>(rate);
      auto p = variants[0];
      p.gain = 1;
      f->setParameters(p);
      for (int n = 0; n < 128; ++n) f->noteOn(n, 1);
      for (int b = 0; b < 4; ++b) {
        f->process(l.data(), r.data(), 701);
        for (std::size_t i = 0; i < 701; ++i) {
          require(std::isfinite(l[i]) && std::isfinite(r[i]), "non finite at sample rate");
          peakRate = std::max(peakRate, static_cast<double>(std::abs(l[i])));
        }
      }
    }
    note(fmt("all 128 notes at sample rates 8k..192k (+5 Hz, 1e9, NaN): peak |x| = %.4f", peakRate));
    require(peakRate <= 16.0, "unbounded output at some sample rate");
    e->process(nullptr, nullptr, 10);
    e->process(l.data(), r.data(), 0);
    e->process(l.data(), r.data(), -5);
  });
  test(L + ": voice lifecycle (release, pedal, stealing, performance values)", [&] {
    auto e = make<E>();
    auto p = variants[0];
    p.release = .05f;
    e->setParameters(p);
    std::vector<float> l(48000), r(48000);
    e->noteOn(60, .8f);
    require(e->activeVoices() == 1, "voice count after noteOn");
    require(std::abs(e->lastVelocity() - .8f) < 1e-6f, "lastVelocity");
    require(std::abs(e->lastPitchHz() - 261.6256f) < .01f, "lastPitchHz");
    e->process(l.data(), r.data(), 4800);
    e->noteOff(60);
    e->process(l.data(), r.data(), 48000);
    require(e->activeVoices() == 0, "voice not freed after release");
    e->sustain(true);
    e->noteOn(62, 1);
    e->noteOff(62);
    e->process(l.data(), r.data(), 48000);
    require(e->activeVoices() == 1, "pedal did not hold the note");
    e->sustain(false);
    e->process(l.data(), r.data(), 48000);
    require(e->activeVoices() == 0, "pedal release did not free the note");
    for (int i = 0; i < 40; ++i) e->noteOn(30 + i, .5f);
    require(e->activeVoices() == voiceCount, "polyphony is not capped at 16");
    auto q = p;
    q.enabled = false;
    e->setParameters(q);
    for (int b = 0; b < 4; ++b) e->process(l.data(), r.data(), 4800); // allow the mute to settle
    require(energy(std::vector<float>(l.begin(), l.begin() + 4800)) == 0.0, "disabled source is not silent");
  });
  test(L + ": anti-aliasing measured by FFT at high pitch", [&] {
    // GENDYN is not periodic while its random walk runs, so the non-harmonic power of a walking
    // waveform is signal, not aliasing. aliasPrep freezes such parameters for this measurement.
    double worstTypical = 1e9, worstHarsh = 1e9;
    for (int noteNumber : {84, 96, 103}) {
      for (std::size_t v = 0; v < variants.size(); ++v) {
        const auto p = aliasPrep(variants[v]);
        const double off = aliasDb<E, P>(p, false, noteNumber), on = aliasDb<E, P>(p, true, noteNumber);
        note(fmt("note %.0f (%.1f Hz) variant %.0f: alias/harmonic = %.1f dB without AA, ", noteNumber, hzOf(noteNumber), static_cast<double>(v), off) +
             fmt("%.1f dB with 4x AA -> improvement %.1f dB", on, off - on));
        (v == 0 ? worstTypical : worstHarsh) = std::min(v == 0 ? worstTypical : worstHarsh, off - on);
      }
    }
    note(fmt("worst improvement: typical settings %.1f dB, harsh settings %.1f dB", worstTypical, worstHarsh));
    require(worstTypical >= 10.0, fmt("anti-aliasing improvement below 10 dB for typical settings (%.1f)", worstTypical));
    require(worstHarsh >= 3.0, fmt("anti-aliasing improvement below 3 dB for harsh settings (%.1f)", worstHarsh));
    if (std::string(label) == "Phase Geometry") { // band-limited Walsh is the default; it is what these margins protect
      require(worstTypical >= 14.0, fmt("Phase Geometry typical improvement below 14 dB (%.1f)", worstTypical));
      require(worstHarsh >= 18.0, fmt("Phase Geometry harsh improvement below 18 dB (%.1f)", worstHarsh));
    }
  });
}
// ---- external continuous drive (Resynth) -------------------------------------
// Deterministic drive script: silence, steady pitch, glide, pitch lost (hz 0), return, level dip, junk values.
void driveScript(std::vector<float>& hz, std::vector<float>& level, int total) {
  hz.assign(static_cast<std::size_t>(total), 0.f);
  level.assign(static_cast<std::size_t>(total), 0.f);
  for (int i = 0; i < total; ++i) {
    const auto k = static_cast<std::size_t>(i);
    if (i < 4000) { hz[k] = 0; level[k] = 0; }
    else if (i < 24000) { hz[k] = i < 12000 ? 220.f : 220.f + 110.f * static_cast<float>(i - 12000) / 8000.f; level[k] = .8f * std::min(1.f, static_cast<float>(i - 4000) / 400.f); }
    else if (i < 40000) { hz[k] = 0; level[k] = .8f; } // pitch lost while the envelope is still high
    else if (i < 60000) { hz[k] = 440.f; level[k] = .6f + .2f * std::sin(static_cast<float>(i) * .001f); }
    else if (i < 64000) { hz[k] = 330.f; level[k] = 0; }
    else { // junk: NaN, inf, negative, absurd, sub-audio, above Nyquist
      const float junk[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -50.f, 1e9f, .5f, 30000.f, 90.f, 0.f};
      hz[k] = junk[(i / 700) % 8];
      level[k] = (i / 900) % 5 == 0 ? std::numeric_limits<float>::quiet_NaN() : static_cast<float>((i / 450) % 4) * 1.5f - .5f; // out of 0..1 on purpose
    }
  }
}
template <class E>
void renderDriven(E& e, const std::vector<float>& hz, const std::vector<float>& level, int block, std::vector<float>& l, std::vector<float>& r) {
  const int total = static_cast<int>(hz.size());
  l.assign(hz.size(), 0); r.assign(hz.size(), 0);
  for (int pos = 0; pos < total; pos += block) {
    const int n = std::min(block, total - pos);
    e.processDriven(l.data() + pos, r.data() + pos, n, hz.data() + pos, level.data() + pos);
  }
}
double maxStep(const std::vector<float>& x, int from, int to) {
  double m = 0;
  for (int i = std::max(1, from); i < to && i < static_cast<int>(x.size()); ++i) m = std::max(m, static_cast<double>(std::abs(x[static_cast<std::size_t>(i)] - x[static_cast<std::size_t>(i) - 1])));
  return m;
}
double peakAbs(const std::vector<float>& x, int from, int to) {
  double m = 0;
  for (int i = from; i < to && i < static_cast<int>(x.size()); ++i) m = std::max(m, static_cast<double>(std::abs(x[static_cast<std::size_t>(i)])));
  return m;
}

template <class E, class P> void driveSuite(const char* label, std::vector<P> variants) {
  const std::string L = std::string(label) + " driven";
  constexpr int total = 80000;
  test(L + ": finite, bounded, non-silent; silence in = exact silence out", [&] {
    std::vector<float> hz, level, l, r;
    driveScript(hz, level, total);
    for (std::size_t v = 0; v < variants.size(); ++v) {
      auto e = make<E>();
      e->setParameters(variants[v]);
      renderDriven(*e, hz, level, 480, l, r);
      bool finite = true;
      double peak = 0;
      for (std::size_t i = 0; i < l.size(); ++i) { finite = finite && std::isfinite(l[i]) && std::isfinite(r[i]); peak = std::max(peak, static_cast<double>(std::max(std::abs(l[i]), std::abs(r[i])))); }
      require(finite, "non finite sample");
      require(peak <= 16.0, "sample beyond the engine bound");
      require(peakAbs(l, 6000, 20000) > .05, "driven voice is silent");
      bool silent = true;
      for (int i = 0; i < 4000; ++i) silent = silent && l[static_cast<std::size_t>(i)] == 0.f && r[static_cast<std::size_t>(i)] == 0.f;
      require(silent, "silence in did not give exact silence");
      note(fmt("variant %.0f: peak %.3f, steady max |x| %.3f", static_cast<double>(v), peak, peakAbs(l, 6000, 20000)));
    }
  });
  test(L + ": bit-deterministic and independent of the host block size", [&] {
    std::vector<float> hz, level, l1, r1, l2, r2;
    driveScript(hz, level, total);
    for (std::size_t v = 0; v < variants.size(); ++v) {
      auto a = make<E>();
      a->setParameters(variants[v]);
      renderDriven(*a, hz, level, 480, l1, r1);
      auto b = make<E>();
      b->setParameters(variants[v]);
      renderDriven(*b, hz, level, 480, l2, r2);
      require(sameBits(l1, l2) && sameBits(r1, r2), "two identical runs differ");
      for (int block : {1, 7, 64, 333, 512, total}) {
        auto c = make<E>();
        c->setParameters(variants[v]);
        renderDriven(*c, hz, level, block, l2, r2);
        std::size_t differing = 0;
        const double d = maxDiff(l1, l2, differing);
        require(sameBits(l1, l2) && sameBits(r1, r2), fmt("block size %.0f changes the output: ", block) + fmt("%.3g max, %.0f samples", d, static_cast<double>(differing)));
      }
    }
    note("block sizes 1, 7, 64, 333, 480, 512 and a single 80000 block give identical bits");
  });
  test(L + ": zero allocations after prepare", [&] {
    std::vector<float> hz, level, l, r;
    driveScript(hz, level, total);
    auto e = make<E>();
    e->setParameters(variants[0]);
    l.assign(hz.size(), 0); r.assign(hz.size(), 0);
    g_allocations = 0;
    g_tracking = true;
    for (int pos = 0, b = 0; pos < total; ++b) {
      const int n = std::min(1 + (b * 37) % 700, total - pos);
      e->setParameters(variants[static_cast<std::size_t>(b % 2)]);
      if (b % 11 == 0) e->noteOn(48 + b % 24, .7f);
      e->processDriven(l.data() + pos, r.data() + pos, n, hz.data() + pos, level.data() + pos);
      pos += n;
    }
    e->reset();
    e->processDriven(l.data(), r.data(), 480, hz.data() + 4000, level.data() + 4000);
    g_tracking = false;
    note(fmt("allocations while driving (changing blocks, parameters, notes, reset): %.0f", static_cast<double>(g_allocations.load())));
    require(g_allocations.load() == 0, "heap allocation in processDriven");
  });
  test(L + ": pitch lost and regained: no click, pitch held while fading, silent afterwards", [&] {
    for (std::size_t v = 0; v < variants.size(); ++v) {
      std::vector<float> hz(40000, 220.f), level(40000, .8f), l, r;
      for (int i = 0; i < 400; ++i) level[static_cast<std::size_t>(i)] = .8f * static_cast<float>(i) / 400;
      for (int i = 15000; i < 28000; ++i) hz[static_cast<std::size_t>(i)] = 0; // no valid pitch, envelope unchanged
      auto e = make<E>();
      auto p = variants[v];
      e->setParameters(p);
      renderDriven(*e, hz, level, 480, l, r);
      const double steady = maxStep(l, 2000, 15000), steadyPeak = peakAbs(l, 2000, 15000);
      const double lossStep = maxStep(l, 15000, 28000), returnStep = maxStep(l, 28000, 28000 + 3000);
      const double startStep = maxStep(l, 0, 3000);
      note(fmt("variant %.0f: steady max step %.4f (peak %.3f); at pitch loss ", static_cast<double>(v), steady, steadyPeak) +
           fmt("%.4f; at return %.4f; at start %.4f", lossStep, returnStep, startStep));
      // A fresh voice (new random shape for Chaos) may have a steeper edge than the stretch measured, but the 4 ms gate
      // ramp keeps every transition within 1.5x of the steady-state step and well below the peak.
      require(lossStep <= steady * 1.5 + 1e-6, "step at pitch loss exceeds 1.5x the steady-state step");
      require(returnStep <= steady * 1.5 + 1e-6, "step at pitch return exceeds 1.5x the steady-state step");
      require(startStep <= steady * 1.5 + 1e-6, "step at start exceeds 1.5x the steady-state step");
      // held pitch: while fading (first 100 ms after the loss) the sound keeps its 220 Hz period
      int crossings = 0;
      for (int i = 15001; i < 15000 + 2400; ++i) crossings += (l[static_cast<std::size_t>(i)] < 0) != (l[static_cast<std::size_t>(i) - 1] < 0);
      // after the fade the voice is off and the engine output is exactly the DC blocker tail: silent
      require(peakAbs(l, 15000 + 16000, 28000) < 1e-5, "output not silent after the fade");
      require(peakAbs(l, 15000 + 1000, 15000 + 2400) > .01 * steadyPeak, "fade-out is much too fast (30 ms expected)");
      note(fmt("zero crossings in the 50 ms after pitch loss: %.0f (about ", crossings) + fmt("%.0f expected at 220 Hz for any waveform with a 220 Hz period at least)", 2 * 220 * .05));
      require(crossings >= 18, "pitch not held during the fade (too few zero crossings)");
    }
  });
  test(L + ": level scales the output linearly and transpose moves the pitch", [&] {
    std::vector<float> hz(30000, 220.f), a(30000, .8f), b(30000, .4f), l1, r1, l2, r2;
    auto p = variants[0];
    p.transpose = 0;
    auto e1 = make<E>(); e1->setParameters(p); renderDriven(*e1, hz, a, 480, l1, r1);
    auto e2 = make<E>(); e2->setParameters(p); renderDriven(*e2, hz, b, 480, l2, r2);
    double num = 0, den = 0;
    for (std::size_t i = 10000; i < 30000; ++i) { num += static_cast<double>(l1[i]) * l2[i]; den += static_cast<double>(l2[i]) * l2[i]; }
    const double gainRatio = num / den;
    note(fmt("level .8 vs .4: gain ratio %.5f", gainRatio));
    require(std::abs(gainRatio - 2.0) < 0.02, "output is not proportional to the drive level");
    if constexpr (std::is_same_v<P, PhaseGeometryParameters>) {
      auto q = PhaseGeometryParameters{};
      q.czDepth = 0; q.attack = .001f; q.sustain = 1; q.gain = 1; q.spread = 0;
      for (float transpose : {0.f, 12.f, -12.f}) {
        q.transpose = transpose;
        auto e = make<E>(); e->setParameters(q);
        std::vector<float> h(65536 + 20000, 220.f), lv(h.size(), 1.f), l(h.size()), r(h.size());
        e->processDriven(l.data(), r.data(), static_cast<int>(h.size()), h.data(), lv.data());
        const auto s = powerSpectrum(l.data() + 20000, 65536);
        std::size_t peak = 5;
        for (std::size_t i = 5; i < s.size(); ++i) if (s[i] > s[peak]) peak = i;
        const double found = static_cast<double>(peak) * 48000. / 65536., expected = 220 * std::pow(2.0, transpose / 12.0);
        note(fmt("transpose %.0f: spectral peak %.2f Hz, expected %.2f Hz", transpose, found, expected));
        require(std::abs(found - expected) < 2 * 48000. / 65536., "driven pitch does not follow hz and transpose");
      }
    }
  });
  test(L + ": Source.enabled=false is silent; MIDI note voices still mix with the driven voice", [&] {
    std::vector<float> hz(8000, 220.f), level(8000, .8f), l, r, l2, r2;
    auto p = variants[0];
    p.enabled = false;
    auto e = make<E>(); e->setParameters(p); renderDriven(*e, hz, level, 480, l, r);
    require(peakAbs(l, 0, 8000) == 0.0, "disabled source produced sound");
    p.enabled = true;
    auto m = make<E>(); m->setParameters(p); m->noteOn(60, 1); renderDriven(*m, hz, level, 480, l, r);
    auto d = make<E>(); d->setParameters(p); renderDriven(*d, hz, level, 480, l2, r2);
    require(peakAbs(l, 2000, 8000) > 0 && energy(l) != energy(l2), "note voice does not add to the driven voice");
  });
}
} // namespace

int main() {
  try {
    test("PCG32 matches the published reference sequence (seed 42, stream 54)", [] {
      Pcg32 rng(42, 54);
      const std::uint32_t expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
      for (auto v : expected) {
        const auto got = rng.next();
        require(got == v, fmt("PCG32 mismatch: got %.0f expected %.0f", got, v));
      }
      double lo = 1, hi = 0;
      for (int i = 0; i < 100000; ++i) { const double u = rng.uniform(); lo = std::min(lo, u); hi = std::max(hi, u); }
      require(lo > 0 && hi < 1, "uniform() left the open interval");
    });
    test("Walsh table: sequency ordering, +/-1 values and orthogonality", [] {
      for (int s = 0; s < walshOrder; ++s) {
        const auto row = walshSequencyRow(s);
        int changes = 0;
        for (std::size_t j = 1; j < walshOrder; ++j) changes += row[j] != row[j - 1];
        require(changes == s, fmt("row %.0f has %.0f sign changes", s, changes));
        for (int t = 0; t < walshOrder; ++t) {
          const auto other = walshSequencyRow(t);
          long dot = 0;
          for (std::size_t j = 0; j < walshOrder; ++j) dot += row[j] * other[j];
          require(dot == (s == t ? walshOrder : 0), "Walsh rows are not orthogonal");
        }
      }
      const auto& table = tables();
      for (int s = 0; s < walshOrder; ++s) {
        const auto row = walshSequencyRow(s);
        for (int j = 0; j < walshOrder; ++j)
          require(table.walsh[static_cast<std::size_t>(s)][static_cast<std::size_t>(j * walshTableSize / walshOrder + 4)] == static_cast<float>(row[static_cast<std::size_t>(j)]), "table does not hold the Walsh row");
      }
      note("64 rows: sign changes == sequency, Gram matrix == 64*I, oscillator table matches");
    });
    test("VPS and CZ phase maps: identity, endpoints, continuity, monotonicity", [] {
      for (float d : {.1f, .3f, .5f, .8f}) {
        for (int i = 0; i <= 100; ++i) {
          const float x = static_cast<float>(i) / 100;
          require(std::abs(vpsPhase(x, d, d) - x) < 1e-5f, "VPS with v == d must be the identity");
        }
        for (float v : {0.f, .2f, .5f, 1.f}) {
          require(std::abs(vpsPhase(0, d, v)) < 1e-6f && std::abs(vpsPhase(1, d, v) - 1) < 1e-5f, "VPS endpoints");
          require(std::abs(vpsPhase(d, d, v) - v) < 1e-5f, "VPS passes through its breakpoint");
          float prev = vpsPhase(0, d, v);
          for (int i = 1; i <= 1000; ++i) {
            const float y = vpsPhase(static_cast<float>(i) / 1000, d, v);
            require(std::abs(y - prev) < 0.5f && y >= -1e-6f && y <= 1 + 1e-6f, "VPS discontinuous or out of range");
            prev = y;
          }
        }
      }
      for (int shape = 0; shape < static_cast<int>(CzShape::count); ++shape) {
        float window = 0;
        for (int i = 0; i <= 100; ++i) {
          const float x = static_cast<float>(i) / 100;
          require(czPhase(shape, x, 0, window) == x && window == 1, "CZ depth 0 must be the identity");
        }
        for (float depth : {.25f, .6f, 1.f}) {
          float prev = -1;
          for (int i = 0; i <= 2000; ++i) {
            const float x = static_cast<float>(i) / 2000;
            const float y = czPhase(shape, x, depth, window);
            require(y >= -1e-6f && y <= 1 + 1e-6f && window >= 0 && window <= 1, "CZ out of range");
            if (shape < 3) require(y + 1e-6f >= prev, "non-resonant CZ maps must be monotonic");
            prev = y;
          }
        }
      }
    });
    test("Phase Geometry: fundamental pitch; Walsh sequency 1 gives a square-wave spectrum", [] {
      PhaseGeometryParameters p;
      p.czDepth = 0; p.attack = .001f; p.sustain = 1; p.decay = .01f; p.gain = 1; p.spread = 0;
      auto e = make<PhaseGeometrySource>();
      e->setParameters(p);
      e->noteOn(57, 1);
      std::vector<float> l(65536 + 20000), r(l.size());
      e->process(l.data(), r.data(), static_cast<int>(l.size()));
      auto s = powerSpectrum(l.data() + 20000, 65536);
      std::size_t peak = 5;
      for (std::size_t i = 5; i < s.size(); ++i) if (s[i] > s[peak]) peak = i;
      const double found = static_cast<double>(peak) * 48000. / 65536.;
      note(fmt("cosine voice: spectral peak %.2f Hz, expected %.2f Hz", found, hzOf(57)));
      require(std::abs(found - hzOf(57)) < 2 * 48000. / 65536., "wrong fundamental");
      p.walshMix = 1; p.walshSequency = 1;
      auto q = make<PhaseGeometrySource>();
      q->setParameters(p);
      q->noteOn(57, 1);
      q->process(l.data(), r.data(), static_cast<int>(l.size()));
      s = powerSpectrum(l.data() + 20000, 65536);
      auto band = [&](double hz) { double m = 0; const long c = std::lround(hz * 65536. / 48000.); for (long b = c - 6; b <= c + 6; ++b) m = std::max(m, s[static_cast<std::size_t>(b)]); return m; };
      const double f0 = hzOf(57), fund = band(f0), even = std::max(band(2 * f0), band(4 * f0)), third = band(3 * f0);
      note(fmt("square (Walsh 1): 2nd/4th harmonic %.1f dB, 3rd harmonic %.1f dB relative to fundamental", 10 * std::log10(even / fund), 10 * std::log10(third / fund)));
      require(10 * std::log10(even / fund) < -40, "even harmonics present in a square wave");
      require(10 * std::log10(third / fund) > -14 && 10 * std::log10(third / fund) < -8, "3rd harmonic of a square should be about -9.5 dB");
    });
    test("Chaos: frozen shape is exactly periodic and pitch locked; evolving shape is not", [] {
      ChaosParameters p;
      p.antiAlias = false; p.attack = .001f; p.sustain = 1; p.decay = .01f; p.gain = 1; p.spread = 0;
      p.ampStep = 0; p.durStep = 0;
      auto e = make<ChaosSource>(44000); // 440 Hz -> period of exactly 100 samples
      e->setParameters(p);
      e->noteOn(69, 1);
      std::vector<float> l(90000), r(l.size());
      e->process(l.data(), r.data(), static_cast<int>(l.size()));
      double worst = 0, rms = 0;
      for (std::size_t i = 70000; i < 89800; ++i) { worst = std::max(worst, static_cast<double>(std::abs(l[i + 100] - l[i]))); rms += static_cast<double>(l[i]) * l[i]; }
      rms = std::sqrt(rms / 19800);
      note(fmt("frozen polygon: max |y[n+100]-y[n]| = %.3g, rms = %.3f", worst, rms));
      require(rms > .05 && worst < 1e-4, "frozen GENDYN shape is not periodic at the note period");
      p.ampStep = .5f; p.durStep = .3f;
      auto f = make<ChaosSource>(44000);
      f->setParameters(p);
      f->noteOn(69, 1);
      f->process(l.data(), r.data(), static_cast<int>(l.size()));
      worst = 0;
      for (std::size_t i = 70000; i < 89800; ++i) worst = std::max(worst, static_cast<double>(std::abs(l[i + 100] - l[i])));
      note(fmt("evolving polygon: max |y[n+100]-y[n]| = %.3g", worst));
      require(worst > 0.01, "random walk did not change the waveform");
    });
    test("Chaos: step distributions are bounded and the mirror barrier keeps values inside", [] {
      Pcg32 rng(1, 1);
      for (int d = 0; d < static_cast<int>(ChaosDistribution::count); ++d) {
        double sum = 0, sq = 0, big = 0;
        const int n = 200000;
        for (int i = 0; i < n; ++i) { const double x = chaosDraw(rng, d); require(std::isfinite(x) && std::abs(x) <= 8, "draw out of range"); sum += x; sq += x * x; big = std::max(big, std::abs(x)); }
        note(fmt("distribution %.0f: mean %.4f, rms %.3f, max %.2f", d, sum / n, std::sqrt(sq / n), big));
        require(std::abs(sum / n) < .06 || d == static_cast<int>(ChaosDistribution::exponential), "distribution is not symmetric");
        require(sq / n > .05, "distribution has no spread");
      }
      for (int i = 0; i < 100000; ++i) {
        const double x = (rng.uniform() - .5) * 1e6;
        const double y = mirror(x, -.7, .9);
        require(y >= -.7 && y <= .9, "mirror escaped its barriers");
      }
      require(mirror(std::numeric_limits<double>::quiet_NaN(), -1, 1) == 0.0, "mirror(NaN)");
    });

    suite<PhaseGeometrySource, PhaseGeometryParameters>("Phase Geometry", pgVariants(), [](PhaseGeometryParameters p) { return p; });
    suite<ChaosSource, ChaosParameters>("Chaos", chaosVariants(), [](ChaosParameters p) { p.ampStep = 0; p.durStep = 0; return p; });
    driveSuite<PhaseGeometrySource, PhaseGeometryParameters>("Phase Geometry", pgVariants());
    driveSuite<ChaosSource, ChaosParameters>("Chaos", chaosVariants());
    test("driven voice: FFT alias, Phase Geometry/Chaos, 4x oversampling vs 1x, constant drive", [] {
      for (int noteNumber : {72, 84, 96, 103}) {
        std::string line = fmt("note %.0f:", noteNumber);
        for (int kind = 0; kind < 4; ++kind) {
          AliasStat on{}, off{};
          const char* tag = "";
          if (kind < 2) { auto p = pgVariants()[static_cast<std::size_t>(kind)]; tag = kind == 0 ? "PG typical" : "PG harsh"; on = aliasStat<PhaseGeometrySource, PhaseGeometryParameters>(p, true, noteNumber, 48000, true); off = aliasStat<PhaseGeometrySource, PhaseGeometryParameters>(p, false, noteNumber, 48000, true); }
          else { auto p = chaosVariants()[static_cast<std::size_t>(kind - 2)]; p.ampStep = 0; p.durStep = 0; tag = kind == 2 ? "Chaos typical" : "Chaos harsh"; on = aliasStat<ChaosSource, ChaosParameters>(p, true, noteNumber, 48000, true); off = aliasStat<ChaosSource, ChaosParameters>(p, false, noteNumber, 48000, true); }
          auto chaosFrozen = [&] { auto q = chaosVariants()[static_cast<std::size_t>(kind - 2)]; q.ampStep = 0; q.durStep = 0; return q; };
          const auto ref = kind < 2 ? aliasStat<PhaseGeometrySource, PhaseGeometryParameters>(pgVariants()[static_cast<std::size_t>(kind)], true, noteNumber) : aliasStat<ChaosSource, ChaosParameters>(chaosFrozen(), true, noteNumber);
          line += std::string(" | ") + tag + fmt(" 4x %.1f dB, ", on.ratio) + fmt("1x %.1f dB, ", off.ratio) + fmt("note-path 4x %.1f dB", ref.ratio);
          require(on.aliasAbs <= off.aliasAbs + 0.5, std::string(tag) + ": 4x oversampling is worse than 1x under drive");
          line += fmt(" (4x gain %.1f dB)", off.aliasAbs - on.aliasAbs);
          // Phase Geometry is deterministic and must match the note path; a Chaos voice draws its polygon from its own PCG32 stream, so only the 4x benefit is compared.
          if (kind < 2) require(std::abs(on.aliasAbs - ref.aliasAbs) < 1.0, std::string(tag) + ": driven alias differs from the note path");
          else require(off.aliasAbs - on.aliasAbs >= 3.0, std::string(tag) + ": 4x oversampling gains less than 3 dB under drive");
        }
        note(line);
      }
    });

  test("Phase Geometry: typical/harsh settings, plain table (mode 0) vs ADAA (mode 2), notes 84/96/103", [] {
    for (std::size_t v = 0; v < 2; ++v)
      for (int noteNumber : {84, 96, 103}) {
        auto p = pgVariants()[v];
        auto q = p; q.walshBandLimit = 0;
        const auto b = aliasStat<PhaseGeometrySource, PhaseGeometryParameters>(q, false, noteNumber), bo = aliasStat<PhaseGeometrySource, PhaseGeometryParameters>(q, true, noteNumber);
        const auto a = aliasStat<PhaseGeometrySource, PhaseGeometryParameters>(p, false, noteNumber), ao = aliasStat<PhaseGeometrySource, PhaseGeometryParameters>(p, true, noteNumber);
        note(std::string(v == 0 ? "typical" : "harsh") + fmt(" note %.0f: ", noteNumber) + fmt("BEFORE AAoff %.1f AAon %.1f | ", b.ratio, bo.ratio) + fmt("AFTER AAoff %.1f AAon %.1f dB (alias/harmonic)", a.ratio, ao.ratio));
      }
  });
  test("Phase Geometry: Walsh band-limiting modes (0 table, 1 polyBLEP, 2 ADAA), sequency 33/47/63 (FFT alias)", [] {
    // "plain": undistorted phase, pure Walsh. "harsh": the harsh variant with Walsh only. Walsh sequency s has its lowest
    // partial near s/2 * f0; when that is above Nyquist the note cannot be represented at all, so the ratio to harmonic
    // power is meaningless there and absolute alias power (dB re a +/-1 square wave) is reported as well.
    for (int kind = 0; kind < 2; ++kind) {
      for (float seq : {33.f, 47.f, 63.f}) {
        for (int noteNumber : {84, 96, 103}) {
          PhaseGeometryParameters p = kind == 0 ? PhaseGeometryParameters{} : pgVariants()[1];
          if (kind == 0) { p.czDepth = 0; p.vpsD = .5f; p.vpsV = .5f; }
          p.walshMix = 1; p.walshSequency = seq;
          auto s = [](const AliasStat& a) { return fmt("%.1f (", a.ratio) + fmt("alias %.1f / harm ", a.aliasAbs) + fmt("%.1f)", a.harmAbs); };
          std::string line = std::string(kind == 0 ? "plain" : "harsh") + fmt(" seq %.0f note %.0f (low partial ", seq, noteNumber) + fmt("%.1f kHz):", seq / 2 * hzOf(noteNumber) / 1000);
          AliasStat on[3];
          for (int mode = 0; mode < 3; ++mode) {
            p.walshBandLimit = mode;
            on[mode] = aliasStat<PhaseGeometrySource, PhaseGeometryParameters>(p, true, noteNumber);
            line += fmt(" | mode %.0f ", mode) + "AAon " + s(on[mode]) + " AAoff " + s(aliasStat<PhaseGeometrySource, PhaseGeometryParameters>(p, false, noteNumber));
          }
          note(line);
          // Thresholds for the shipped mode (2, ADAA) with 4x oversampling: absolute alias power below -38 dB re a
          // full-scale square, at least 12 dB below the plain table, and where the note is representable (the table
          // path has real harmonic energy above -45 dB) alias stays 15 dB below the harmonics.
          const std::string where = std::string(kind == 0 ? "plain" : "harsh") + fmt(" seq %.0f note %.0f", seq, noteNumber);
          require(on[2].aliasAbs <= -38.0, where + fmt(": absolute alias %.1f dB above -38", on[2].aliasAbs));
          require(on[0].aliasAbs - on[2].aliasAbs >= 12.0, where + fmt(": alias only %.1f dB below the plain table", on[0].aliasAbs - on[2].aliasAbs));
          if (on[0].harmAbs > -45.0) require(on[2].ratio <= -15.0, where + fmt(": alias/harmonic %.1f dB above -15", on[2].ratio));
        }
      }
    }
  });
    test("both sources feed aether::modular::GraphEngine and drive its performance modulation sources", [] {
      namespace m = aether::modular;
      auto run = [](Source& src, float velocity) {
        m::GraphEngine graph;
        graph.prepare(48000, 480);
        m::Parameters gp;
        m::assignModule(gp, 0, m::ModuleType::Space);
        gp.input[0] = 1; gp.output[0] = 1; gp.slots[0].mix = .4f;
        gp.modulation[0] = {6, 5, 1}; // velocity source -> node 1 mix
        graph.setParameters(gp);
        src.prepare(48000, 480);
        src.noteOn(60, velocity);
        std::vector<float> l(480), r(480);
        double total = 0;
        for (int block = 0; block < 100; ++block) {
          src.process(l.data(), r.data(), 480);
          graph.setPerformance(src.lastVelocity(), src.lastPitchHz());
          graph.process(l.data(), r.data(), nullptr, 480);
          for (std::size_t i = 0; i < 480; ++i) { require(std::isfinite(l[i]) && std::isfinite(r[i]), "graph output non finite"); total += static_cast<double>(l[i]) * l[i]; }
        }
        const auto levels = graph.sourceLevels();
        require(std::abs(levels[6] - velocity) < 1e-6f, "velocity did not reach modulation source 6");
        const float expectedPitch = std::clamp(std::log2(src.lastPitchHz() / 20.f) / 10, 0.f, 1.f);
        require(std::abs(levels[7] - expectedPitch) < 1e-5f, "pitch did not reach modulation source 7");
        require(levels[5] > .01f, "audio envelope follower (source 5) did not see the source");
        return total;
      };
      PhaseGeometrySource pg1, pg2;
      ChaosSource ch;
      const double a = run(pg1, .3f), b = run(pg2, .9f), c = run(ch, .5f);
      note(fmt("graph output energy: PG vel .3 = %.3f, PG vel .9 = %.3f, Chaos vel .5 = %.3f", a, b, c));
      require(a > 1e-3 && b > 1e-3 && c > 1e-3, "graph output silent");
      require(std::abs(a - b) > 1e-3, "velocity modulation had no effect on the graph");
    });
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
    return 2;
  }
  std::printf("%d passed, %d failed\n", passed, failed);
  return failed == 0 ? 0 : 1;
}
