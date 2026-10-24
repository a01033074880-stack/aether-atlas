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
template <class E, class P> AliasStat aliasStat(P p, bool antiAlias, int noteNumber, double rate = 48000) {
  constexpr std::size_t n = 65536;
  p.antiAlias = antiAlias;
  p.attack = .001f; p.sustain = 1; p.decay = .01f; p.spread = 0; p.gain = 1;
  auto e = make<E>(rate);
  e->setParameters(p);
  e->noteOn(noteNumber, 1);
  std::vector<float> l(n + 20000), r(n + 20000);
  e->process(l.data(), r.data(), static_cast<int>(l.size()));
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
