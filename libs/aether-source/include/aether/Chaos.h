#pragma once
// Chaos Voice: GENDYN-style dynamic stochastic synthesis.
//
// Idea from I. Xenakis (Formalized Music, 1992 / GENDYN) as described by
// M. Serra, "Stochastic composition and stochastic timbre: GENDYN by Iannis
// Xenakis", Perspectives of New Music 31(1), 1993, and S. Hoffmann, "The
// Implementation of Xenakis' GENDYN", 2000: one period of the waveform is a
// polygon of N breakpoints; every period each breakpoint's amplitude and each
// segment's duration take a random-walk step drawn from a probability
// distribution, reflected back by elastic barriers.
//
// Written from the published description (no code from other GENDYN
// implementations is used). Design decisions:
//  * the first breakpoint is pinned to 0 so the polygon is closed and continuous
//    across cycles (no wrap click, no DC drift from the anchor point);
//  * segment durations are relative weights normalised to the period, so the
//    voice stays pitch-locked to the MIDI note while the shape evolves;
//  * an optional momentum term turns the walk into a second-order walk.
#include <aether/VoiceEngine.h>

namespace aether::source {

inline constexpr int maxSegments = 32;
enum class ChaosDistribution : int { cauchy, logistic, hyperbolicCosine, arcsine, exponential, uniform, count };

// Reflect x into [lo, hi] (elastic barriers); non finite input maps to the centre.
inline double mirror(double x, double lo, double hi) noexcept {
  const double range = hi - lo;
  if (!(range > 0) || !std::isfinite(x)) return std::isfinite(lo) && range > 0 ? lo + range * .5 : lo;
  double t = std::fmod(x - lo, 2 * range);
  if (t < 0) t += 2 * range;
  if (t > range) t = 2 * range - t;
  return lo + t;
}

// Inverse-CDF sampling of a symmetric step; |result| <= 8, typical scale about 1.
inline double chaosDraw(Pcg32& rng, int distribution) noexcept {
  const double u = rng.uniform();
  double x = 0;
  switch (static_cast<ChaosDistribution>(distribution)) {
  case ChaosDistribution::cauchy: x = std::tan(pi * (u - .5)); break;
  case ChaosDistribution::logistic: x = .5 * std::log(u / (1 - u)); break;
  case ChaosDistribution::hyperbolicCosine: x = std::log(std::tan(.5 * pi * u)); break;
  case ChaosDistribution::arcsine: x = std::sin(pi * (u - .5)); break;
  case ChaosDistribution::exponential: x = (u < .5 ? 1. : -1.) * -std::log(1 - 2 * std::abs(u - .5)); break;
  default: x = 2 * u - 1; break;
  }
  return std::clamp(x, -8., 8.);
}

struct ChaosParameters : CommonParameters {
  int segments = 12;       // 2..32 polygon segments per period
  int distribution = 0;    // ChaosDistribution
  float ampStep = 0.15f;   // 0..1 amplitude walk step scale (0 = frozen shape)
  float durStep = 0.1f;    // 0..1 duration walk step scale
  float durRange = 0.5f;   // 0..1 how far segment weights may wander
  float momentum = 0;      // 0..0.95 second-order walk
  float ampBarrier = 1;    // 0.05..1 amplitude barrier
};

struct ChaosTraits {
  using Params = ChaosParameters;
  struct Voice : VoiceBase {
    double phase = 0;
    int count = 2, segment = 0;
    std::array<float, maxSegments> amplitude{}, drift{};
    std::array<double, maxSegments> weight{}, inverse{};
    std::array<double, maxSegments + 1> edge{};
  };
  static constexpr const char* name = "Chaos";
  static constexpr std::array<float Params::*, 6> smoothed{
      &Params::gain, &Params::ampStep, &Params::durStep, &Params::durRange, &Params::momentum, &Params::ampBarrier};
  static void sanitize(Params& p) noexcept {
    p.segments = std::clamp(p.segments, 2, maxSegments);
    p.distribution = std::clamp(p.distribution, 0, static_cast<int>(ChaosDistribution::count) - 1);
    p.ampStep = bound(p.ampStep, 0, 1, .15f);
    p.durStep = bound(p.durStep, 0, 1, .1f);
    p.durRange = bound(p.durRange, 0, 1, .5f);
    p.momentum = bound(p.momentum, 0, .95f);
    p.ampBarrier = bound(p.ampBarrier, .05f, 1, 1);
  }
  static void layout(Voice& v) noexcept {
    double sum = 0;
    for (int i = 0; i < v.count; ++i) sum += v.weight[static_cast<std::size_t>(i)];
    double at = 0;
    for (int i = 0; i < v.count; ++i) {
      const auto k = static_cast<std::size_t>(i);
      v.edge[k] = at / sum;
      const double length = v.weight[k] / sum;
      v.inverse[k] = 1 / length;
      at += v.weight[k];
    }
    v.edge[static_cast<std::size_t>(v.count)] = 1;
  }
  static void walk(Voice& v, const Params& p) noexcept {
    const int n = std::clamp(p.segments, 2, maxSegments);
    for (int i = v.count; i < n; ++i) { // grown polygon: new points start neutral
      const auto k = static_cast<std::size_t>(i);
      v.amplitude[k] = 0;
      v.drift[k] = 0;
      v.weight[k] = 1;
    }
    v.count = n;
    const double barrier = p.ampBarrier, lo = 1 - .9 * p.durRange, hi = 1 + 3. * p.durRange;
    for (int i = 0; i < n; ++i) {
      const auto k = static_cast<std::size_t>(i);
      if (i > 0) { // breakpoint 0 stays at zero
        const double step = chaosDraw(v.rng, p.distribution) * p.ampStep * .25 * barrier;
        const double speed = p.momentum * v.drift[k] + step;
        v.drift[k] = static_cast<float>(speed);
        v.amplitude[k] = static_cast<float>(mirror(v.amplitude[k] + speed, -barrier, barrier));
      }
      const double dstep = chaosDraw(v.rng, p.distribution) * p.durStep * .25;
      v.weight[k] = mirror(v.weight[k] + dstep, lo, hi);
    }
    layout(v);
  }
  static void start(Voice& v, const Params& p) noexcept {
    v.phase = 0;
    v.segment = 0;
    v.count = std::clamp(p.segments, 2, maxSegments);
    for (int i = 0; i < v.count; ++i) {
      const auto k = static_cast<std::size_t>(i);
      v.amplitude[k] = i == 0 ? 0.f : static_cast<float>((2 * v.rng.uniform() - 1) * p.ampBarrier);
      v.drift[k] = 0;
      v.weight[k] = 1;
    }
    layout(v);
  }
  static float tick(Voice& v, const Params& p, const Tables&) noexcept {
    v.phase += v.increment;
    if (v.phase >= 1) {
      v.phase -= 1;
      v.segment = 0;
      walk(v, p);
    }
    while (v.segment + 1 < v.count && v.phase >= v.edge[static_cast<std::size_t>(v.segment) + 1]) ++v.segment;
    const auto k = static_cast<std::size_t>(v.segment);
    const float a = v.amplitude[k];
    const float b = v.segment + 1 < v.count ? v.amplitude[k + 1] : 0.f; // closes on the pinned zero point
    const auto t = static_cast<float>((v.phase - v.edge[k]) * v.inverse[k]);
    return a + (b - a) * std::clamp(t, 0.f, 1.f);
  }
};

class ChaosSource final : public VoiceEngine<ChaosTraits> {};
} // namespace aether::source