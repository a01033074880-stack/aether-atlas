#include <Control/phasor.h>
#include <Effects/chorus.h>
#include <aether/Modular.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <vector>
using namespace aether::modular;
static bool trackAllocations = false;
static size_t allocations = 0;
void *operator new(std::size_t n) {
  if (trackAllocations)
    ++allocations;
  if (auto p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
void *operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
static int failures = 0;
static void check(bool ok, const std::string &s) {
  if (!ok) {
    std::cerr << "FAIL " << s << '\n';
    ++failures;
  }
}
static Parameters single(ModuleType t) {
  Parameters p;
  p.input[0] = p.output[0] = 1;
  assignModule(p, 0, t);
  return p;
}
static std::vector<float> render(GraphEngine &e, int count, double rate = 48000,
                                 int block = 137, bool impulse = false) {
  std::vector<float> l(count), r(count);
  for (int i = 0; i < count; ++i) {
    l[i] = impulse ? (i == 0 ? .7f : 0.f)
                   : float(.23 * std::sin(6.283185307179586 * 440 * i / rate) +
                           .08 * std::sin(6.283185307179586 * 1297 * i / rate));
    r[i] = l[i] * .8f;
  }
  for (int i = 0; i < count; i += block)
    e.process(l.data() + i, r.data() + i, nullptr, std::min(block, count - i));
  for (auto x : l)
    if (!std::isfinite(x) || std::abs(x) > 8) {
      check(false, "finite bounded render");
      break;
    }
  return l;
}
static double energy(const std::vector<float> &x, int from = 0) {
  double s = 0;
  for (size_t i = from; i < x.size(); ++i)
    s += x[i] * x[i];
  return s;
}
static double difference(const std::vector<float> &a,
                         const std::vector<float> &b) {
  double s = 0;
  for (size_t i = 0; i < a.size(); ++i)
    s += std::abs(a[i] - b[i]);
  return s / a.size();
}
static double magnitude(const std::vector<float> &a, double frequency,
                        int from = 12000, double sr = 48000) {
  double re = 0, im = 0;
  for (size_t i = from; i < a.size(); ++i) {
    double w =
        .5 - .5 * std::cos(6.283185307179586 * (i - from) / (a.size() - from));
    re += w * a[i] * std::cos(6.283185307179586 * frequency * i / sr);
    im += w * a[i] * std::sin(6.283185307179586 * frequency * i / sr);
  }
  return std::hypot(re, im);
}
static void chorusRegressions() {
  // Empty upstream constructors must not let stale object storage select the
  // initial direction. Both byte patterns represent finite float values.
  alignas(daisysp::ChorusEngine)
      std::array<std::byte, sizeof(daisysp::ChorusEngine)>
          positiveStorage;
  alignas(daisysp::ChorusEngine)
      std::array<std::byte, sizeof(daisysp::ChorusEngine)>
          negativeStorage;
  std::memset(positiveStorage.data(), 0x3f, positiveStorage.size());
  std::memset(negativeStorage.data(), 0xbf, negativeStorage.size());
  auto *positive = new (positiveStorage.data()) daisysp::ChorusEngine;
  auto *negative = new (negativeStorage.data()) daisysp::ChorusEngine;
  positive->Init(48000);
  negative->Init(48000);
  double initializationDifference = 0;
  for (int n = 0; n < 4096; ++n) {
    float input = .3f * std::sin(float(n) * .071f);
    initializationDifference +=
        std::abs(positive->Process(input) - negative->Process(input));
  }
  check(initializationDifference < 1e-7,
        "Chorus Init ignores poisoned storage LFO direction");
  // Reinitialization after a turning point must restore the same trajectory.
  positive->SetLfoFreq(5);
  for (int n = 0; n < 2400; ++n)
    positive->Process(0);
  positive->Init(48000);
  negative->Init(48000);
  double resetDifference = 0;
  for (int n = 0; n < 4096; ++n) {
    float input = .3f * std::sin(float(n) * .071f);
    resetDifference +=
        std::abs(positive->Process(input) - negative->Process(input));
  }
  check(resetDifference < 1e-7, "Chorus Init resets previous LFO direction");
  positive->~ChorusEngine();
  negative->~ChorusEngine();

  GraphEngine graph;
  graph.prepare(48000, 256);
  auto p = single(ModuleType::Chorus);
  graph.setParameters(p);
  graph.reset();
  const auto reference = render(graph, 12000, 48000, 16);
  graph.reset();
  std::vector<float> edited(12000), right(12000);
  for (int n = 0; n < 12000; ++n) {
    edited[n] = float(.23 * std::sin(6.283185307179586 * 440 * n / 48000) +
                      .08 * std::sin(6.283185307179586 * 1297 * n / 48000));
    right[n] = .8f * edited[n];
  }
  graph.process(edited.data(), right.data(), nullptr, 16);
  // Negative balance affects only R. Any L difference is a hidden change to
  // the delay/depth trajectory, not the intended pan operation.
  p.slots[0].pan = -.2f;
  graph.setParameters(p);
  graph.process(edited.data() + 16, right.data() + 16, nullptr, 11984);
  check(difference(reference, edited) < 1e-7,
        "Chorus unrelated pan edit preserves LFO depth trajectory");
}
int main() {
  chorusRegressions();
  daisysp::Phasor phasor;
  phasor.Init(48000, 24000, 0);
  for (int i = 0; i < 8; ++i) {
    float phase = phasor.Process();
    check(phase >= 0 && phase < 1,
          "Daisy phasor half-open phase range protects grain envelope");
  }
  GraphEngine e;
  e.prepare(48000, 256);
  std::set<std::string> topology;
  for (int a = 0; a < 32; ++a) {
    Parameters p;
    applyAlgorithm(a, p);
    std::string key;
    for (float x : p.input)
      key += std::to_string(x) + ",";
    for (float x : p.output)
      key += std::to_string(x) + ",";
    for (auto row : p.connection)
      for (float x : row)
        key += std::to_string(x) + ",";
    topology.insert(key);
    check(algorithmName(a) && *algorithmName(a), "algorithm name");
    e.setParameters(p);
    e.reset();
    check(energy(render(e, 4096)) > 1e-5,
          "algorithm routes audible " + std::to_string(a));
  }
  check(topology.size() == 32, "32 distinct routing topologies");
  e.setParameters(single(ModuleType::Thru));
  e.reset();
  auto dry = render(e, 24000);
  for (int type = 1; type < int(ModuleType::Count); ++type) {
    auto t = ModuleType(type);
    auto p = single(t);
    const auto &info = moduleInfo(t);
    check(info.name && *info.name && info.provenance && *info.provenance,
          "metadata");
    for (auto m : info.macros)
      check(m.defaultValue >= 0 && m.defaultValue <= 1 && m.maximum > m.minimum,
            "normalized metadata");
    e.setParameters(p);
    e.reset();
    auto y = render(e, 24000);
    check(energy(y) > 1e-6, "audible " + std::string(info.name));
    check(difference(y, dry) > 1e-5, "nontrivial " + std::string(info.name));
    for (int m = 0; m < 4; ++m) {
      p.slots[0].macro[m] = .13f;
      e.setParameters(p);
      e.reset();
      auto low = render(e, 24000);
      p.slots[0].macro[m] = .87f;
      e.setParameters(p);
      e.reset();
      auto high = render(e, 24000);
      check(difference(low, high) > 1e-6,
            "macro " + std::string(info.name) + " " + std::to_string(m));
      p = single(t);
    }
  }
  auto p = single(ModuleType::PrismPitch);
  p.slots[0].macro = {.75f, .5f, .5f, 0.f};
  e.setParameters(p);
  e.reset();
  auto pitch = render(e, 48000);
  check(e.intrinsicLatencySamples(0) > 0, "spectral latency exposed");
  check(magnitude(pitch, 880) > magnitude(pitch, 440) * 5,
        "pitch octave actually doubles fundamental");
  p = single(ModuleType::FrequencyShift);
  p.slots[0].macro = {.6f, 0.f, 0.f, 1.f};
  e.setParameters(p);
  e.reset();
  auto shifted = render(e, 48000);
  check(magnitude(shifted, 540) > magnitude(shifted, 340) * 4,
        "frequency shifter rejects opposite sideband");
  check(magnitude(shifted, 1397) > magnitude(shifted, 1197) * 4,
        "frequency shift adds same Hz to second partial");
  std::cout << "Octave target/original ratio: "
            << magnitude(pitch, 880) / std::max(1e-12, magnitude(pitch, 440))
            << "; shift upper/lower rejection dB: "
            << 20 * std::log10(magnitude(shifted, 540) /
                               std::max(1e-12, magnitude(shifted, 340)))
            << '\n';
  p = single(ModuleType::PrismPitch);
  p.slots[0].macro = {.5f, .5f, 1.f, .5f};
  e.setParameters(p);
  e.reset();
  auto latencyProbe = render(e, 12000, 48000, 137, true);
  auto peak = std::max_element(
      latencyProbe.begin(), latencyProbe.end(),
      [](float a, float b) { return std::abs(a) < std::abs(b); });
  check(std::abs(int(peak - latencyProbe.begin()) -
                 e.intrinsicLatencySamples(0)) < 16,
        "spectral impulse peak agrees with intrinsic delay");
  std::cout << "Prism intrinsic latency: " << e.intrinsicLatencySamples(0)
            << " samples; measured impulse peak: "
            << int(peak - latencyProbe.begin()) << '\n';
  for (auto frozenType : {ModuleType::GrainCloud, ModuleType::SpectralFreeze}) {
    p = single(frozenType);
    e.setParameters(p);
    e.reset();
    render(e, 48000);
    int freezeMacro = moduleInfo(frozenType).freezeMacro;
    check(freezeMacro >= 0, "freeze metadata");
    p.slots[0].macro[freezeMacro] = 1;
    e.setParameters(p);
    std::vector<float> tail(48000), right(48000);
    e.process(tail.data(), right.data(), nullptr, int(tail.size()));
    check(energy(tail, 24000) > 1e-3,
          "freeze retains captured audio " +
              std::string(moduleInfo(frozenType).name));
    p.slots[0].macro[freezeMacro] = 0;
    e.setParameters(p);
    std::fill(tail.begin(), tail.end(), 0);
    std::fill(right.begin(), right.end(), 0);
    e.process(tail.data(), right.data(), nullptr, int(tail.size()));
    check(energy(tail, 42000) < 1e-4, "unfreeze releases captured audio");
    p.slots[0].macro[freezeMacro] = 1;
    e.setParameters(p);
    e.reset();
    auto frozenA = render(e, 24000);
    e.reset();
    auto frozenB = render(e, 24000);
    check(difference(frozenA, frozenB) < 1e-7, "frozen reset deterministic");
  }
  for (int a = 0; a < 32; ++a) {
    applyAlgorithm(a, p);
    for (int n = 0; n < 12; ++n)
      assignModule(p, n, ModuleType::Comb);
    e.setParameters(p);
    e.reset();
    check(energy(render(e, 4096)) > 1e-8,
          "duplicates topology " + std::to_string(a));
  }
  for (int type = 1; type < int(ModuleType::Count); ++type) {
    p = single(ModuleType(type));
    e.setParameters(p);
    e.reset();
    auto solo = render(e, 12000);
    applyAlgorithm(1, p);
    for (int n = 0; n < 12; ++n)
      assignModule(p, n, ModuleType(type));
    e.setParameters(p);
    e.reset();
    auto twelve = render(e, 12000);
    check(difference(solo, twelve) < 1e-5,
          "12 independent repeated module instances " +
              std::string(moduleInfo(ModuleType(type)).name));
  }
  p = {};
  p.input[0] = p.output[1] = 1;
  p.connection[1][0] = 1;
  e.setParameters(p);
  e.reset();
  std::array<float, 4> forwardL{.1f, 0, 0, 0}, forwardR = forwardL;
  e.process(forwardL.data(), forwardR.data(), nullptr, 4);
  check(std::abs(forwardL[0] - .1f) < 1e-6, "forward route current sample");
  p = {};
  p.input[1] = p.output[0] = 1;
  p.connection[0][1] = 1;
  p.feedback = .8f;
  e.setParameters(p);
  e.reset();
  forwardL = {.1f, 0, 0, 0};
  forwardR = forwardL;
  e.process(forwardL.data(), forwardR.data(), nullptr, 4);
  check(forwardL[0] == 0 && forwardL[1] > .07f,
        "backward route previous sample with bounded feedback");
  p = single(ModuleType::Thru);
  p.connection[0][0] = 1;
  p.feedback = .95f;
  e.setParameters(p);
  e.reset();
  auto fb = render(e, 48000, 48000, 137, true);
  check(energy(fb, 24000) < 1e-8, "feedback decays");
  p = single(ModuleType::Thru);
  p.input[0] = -1;
  e.setParameters(p);
  e.reset();
  auto neg = render(e, 24000);
  check(std::abs(neg[100] + dry[100]) < 1e-5, "signed edges preserve polarity");
  p = single(ModuleType::Thru);
  p.modulation[0] = {1, 6, 1};
  p.lfo[0].rate = 3;
  e.setParameters(p);
  e.reset();
  auto mod = render(e, 24000);
  check(difference(mod, dry) > 1e-3, "modulation changes actual destination");
  for (int target = 1; target <= 84; ++target) {
    p = {};
    int node = (target - 1) / 7, attr = (target - 1) % 7;
    p.input[node] = p.output[node] = 1;
    assignModule(p, node, ModuleType::Colour);
    p.slots[node].mix = .5f;
    e.setPerformance(1, 440);
    e.setParameters(p);
    e.reset();
    auto a = render(e, 2048);
    p.modulation[15] = {6, target, attr == 5 ? .25f : .3f};
    e.setParameters(p);
    e.reset();
    auto b = render(e, 2048);
    check(difference(a, b) > 1e-6,
          "all 84 destinations connected " + std::to_string(target));
  }
  for (double sr : {8000., 44100., 96000., 192000.}) {
    e.prepare(sr, 256);
    for (int type = 1; type < int(ModuleType::Count); ++type) {
      p = single(ModuleType(type));
      p.lfo[2] = {1.7f, .2f, 2, false};
      p.modulation[7] = {3, 2, .1f};
      e.setParameters(p);
      e.reset();
      auto a = render(e, 4096, sr, 1);
      e.reset();
      auto b = render(e, 4096, sr, 251);
      check(difference(a, b) < 1e-7, "block independent reset at " +
                                         std::to_string(sr) + " " +
                                         moduleInfo(ModuleType(type)).name);
    }
  }
  p = single(ModuleType::Thru);
  p.lfo[0] = {1.f, 0, 0, true};
  p.tempo = 120;
  p.modulation[0] = {1, 6, .5f};
  e.prepare(48000, 256);
  e.setParameters(p);
  e.reset();
  render(e, 6000);
  auto sources = e.sourceLevels();
  check(std::abs(sources[1] - 1) < .001f, "tempo sync cycles per beat");
  check(e.nodeLevels()[0] > 0, "node meters");
  e.setPerformance(.7f, 440);
  std::array<float, 1> one{0}, two{0}, side{1};
  e.process(one.data(), two.data(), side.data(), 1);
  sources = e.sourceLevels();
  check(sources[5] > 0 && sources[6] == .7f && sources[7] > 0,
        "envelope and performance source meters");
  e.prepare(48000, 256);
  std::array<float, 256> l{}, r{};
  allocations = 0;
  trackAllocations = true;
  for (int t = 0; t < int(ModuleType::Count); ++t) {
    p = single(ModuleType(t));
    e.setParameters(p);
    e.setPerformance(.8f, 220);
    e.reset();
    for (int b = 0; b < 100; ++b) {
      for (int i = 0; i < 256; ++i)
        l[i] = r[i] = .2f * std::sin(float(i + b * 256) * .07f);
      e.process(l.data(), r.data(), nullptr, 256);
    }
  }
  trackAllocations = false;
  check(allocations == 0,
        "no heap allocations after prepare: " + std::to_string(allocations));
  p = single(ModuleType::Ladder);
  p.slots[0].macro.fill(std::numeric_limits<float>::quiet_NaN());
  p.connection[0][0] = std::numeric_limits<float>::infinity();
  e.setParameters(p);
  e.reset();
  render(e, 4096);
  if (failures) {
    std::cerr << failures << " failures\n";
    return 1;
  }
  std::cout << "All modular DSP tests passed\n";
}
