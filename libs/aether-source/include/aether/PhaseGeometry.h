#pragma once
// Phase Geometry Voice: a phase-shaped oscillator.
//
//   ramp -> [phase multiplier] -> VPS breakpoint map -> Casio-CZ style phase
//   distortion (with optional resonance window) -> cosine / Walsh table -> amp
//
//  * VPS follows Kleimola, Lazzarini, Timoney, Valimaki, "Vector Phaseshaping
//    Synthesis", DAFx-11 (equation 6): phi(x) = v x / d for x < d, otherwise
//    v + (1 - v)(x - d)/(1 - d). d == v is the identity; v == 0.5 is classic PD.
//    Implemented independently from the published formula.
//  * The phase-distortion shapes (saw, square, pulse and three resonant windows)
//    are original piecewise-linear phase maps in the style of Casio's CZ series;
//    no CZ firmware or third-party code is used.
//  * The Walsh table holds the 64 Walsh functions in sequency order (row n has n
//    sign changes per period). A fractional sequency cross-fades adjacent rows.
#include <aether/VoiceEngine.h>

namespace aether::source {

using WalshRow = std::array<std::int8_t, walshOrder>;
// Walsh function of the given sequency (0..63) as +/-1 values, sequency ordering
// obtained from the natural (Hadamard) ordering by Gray code + bit reversal.
WalshRow walshSequencyRow(int sequency) noexcept;
int walshHadamardIndex(int sequency) noexcept;

// Vector phase shaping breakpoint map; d is clamped away from 0/1.
inline float vpsPhase(float x, float d, float v) noexcept {
  d = std::clamp(d, 0.001f, 0.999f);
  return x < d ? v * x / d : v + (1 - v) * (x - d) / (1 - d);
}

enum class CzShape : int { saw, square, pulse, resonantSaw, resonantTriangle, resonantTrapezoid, count };

// Casio CZ style phase distortion. depth 0 is the identity (window = 1);
// depth 1 is the most extreme shape. Returns the distorted phase in [0,1].
inline float czPhase(int shape, float x, float depth, float& window) noexcept {
  window = 1;
  if (depth <= 0) return x;
  const float m = std::max(0.5f * (1 - depth), 0.01f); // knee position
  switch (static_cast<CzShape>(shape)) {
  case CzShape::saw:
    return x < m ? 0.5f * x / m : 0.5f + 0.5f * (x - m) / (1 - m);
  case CzShape::square:
    return x < 0.5f ? std::min(0.5f * x / m, 0.5f) : 0.5f + std::min(0.5f * (x - 0.5f) / m, 0.5f);
  case CzShape::pulse:
    return x < m ? 0.5f * x / m : (x < 2 * m ? 0.5f + 0.5f * (x - m) / m : 1.f);
  case CzShape::resonantSaw:
  case CzShape::resonantTriangle:
  case CzShape::resonantTrapezoid: {
    const float y = x * (1 + 15 * depth);
    const float phase = y - std::floor(y);
    if (shape == static_cast<int>(CzShape::resonantSaw))
      window = 1 - depth * x;
    else if (shape == static_cast<int>(CzShape::resonantTriangle))
      window = 1 - depth * std::abs(2 * x - 1);
    else
      window = 1 - depth * (1 - std::min(1.f, 4 * std::min(x, 1 - x)));
    return phase;
  }
  default: return x;
  }
}

struct PhaseGeometryParameters : CommonParameters {
  float walshSequency = 0; // 0..63, fractional = cross-fade between rows
  float walshMix = 0;      // 0 = cosine only, 1 = Walsh only
  int czShape = 0;         // CzShape
  float czDepth = 0.5f;    // 0..1 phase distortion depth ("DCW")
  float czEnvelope = 0;    // 0..1 how much the amplitude envelope scales czDepth
  float czVelocity = 0;    // 0..1 how much velocity scales czDepth
  float vpsD = 0.5f, vpsV = 0.5f; // VPS breakpoint (d == v: identity)
  float vpsRatio = 1;      // 1..16 phase multiplier before the VPS map
  float phaseRandom = 0;   // 0..1 random start phase per note
};

struct PhaseGeometryTraits {
  using Params = PhaseGeometryParameters;
  struct Voice : VoiceBase {
    double phase = 0;
  };
  static constexpr const char* name = "Phase Geometry";
  static constexpr std::array<float Params::*, 8> smoothed{
      &Params::gain,   &Params::walshSequency, &Params::walshMix, &Params::czDepth,
      &Params::vpsD,   &Params::vpsV,          &Params::vpsRatio, &Params::czEnvelope};
  static void sanitize(Params& p) noexcept {
    p.walshSequency = bound(p.walshSequency, 0, 63);
    p.walshMix = bound(p.walshMix, 0, 1);
    p.czShape = std::clamp(p.czShape, 0, static_cast<int>(CzShape::count) - 1);
    p.czDepth = bound(p.czDepth, 0, 1, .5f);
    p.czEnvelope = bound(p.czEnvelope, 0, 1);
    p.czVelocity = bound(p.czVelocity, 0, 1);
    p.vpsD = bound(p.vpsD, 0.001f, 0.999f, .5f);
    p.vpsV = bound(p.vpsV, 0, 1, .5f);
    p.vpsRatio = bound(p.vpsRatio, 1, 16, 1);
    p.phaseRandom = bound(p.phaseRandom, 0, 1);
  }
  static void start(Voice& v, const Params& p) noexcept {
    v.phase = p.phaseRandom > 0 ? static_cast<double>(p.phaseRandom) * v.rng.uniform() : 0.0;
  }
  static float tick(Voice& v, const Params& p, const Tables& t) noexcept {
    v.phase += v.increment;
    if (v.phase >= 1) v.phase -= 1;
    float x = static_cast<float>(v.phase);
    if (p.vpsRatio > 1) {
      const float y = x * p.vpsRatio;
      x = y - std::floor(y);
    }
    x = vpsPhase(x, p.vpsD, p.vpsV);
    float depth = p.czDepth;
    if (p.czEnvelope > 0) depth *= 1 - p.czEnvelope + p.czEnvelope * v.level;
    if (p.czVelocity > 0) depth *= 1 - p.czVelocity + p.czVelocity * v.velocity;
    float window = 1;
    x = czPhase(p.czShape, x, std::clamp(depth, 0.f, 1.f), window);
    x = std::clamp(x, 0.f, 1.f);
    float out = 0;
    if (p.walshMix < 1) {
      const float pos = x * cosTableSize;
      const int i = std::min(static_cast<int>(pos), cosTableSize - 1);
      const float f = pos - static_cast<float>(i);
      out = (1 - p.walshMix) * (t.cosine[static_cast<std::size_t>(i)] +
                                f * (t.cosine[static_cast<std::size_t>(i) + 1] - t.cosine[static_cast<std::size_t>(i)]));
    }
    if (p.walshMix > 0) {
      const float pos = x * walshTableSize;
      const int i = std::min(static_cast<int>(pos), walshTableSize - 1);
      const float f = pos - static_cast<float>(i);
      const int row = std::min(static_cast<int>(p.walshSequency), walshOrder - 1);
      const float blend = p.walshSequency - static_cast<float>(row);
      const auto& a = t.walsh[static_cast<std::size_t>(row)];
      const auto& b = t.walsh[static_cast<std::size_t>(std::min(row + 1, walshOrder - 1))];
      const float wa = a[static_cast<std::size_t>(i)] + f * (a[static_cast<std::size_t>(i) + 1] - a[static_cast<std::size_t>(i)]);
      const float wb = b[static_cast<std::size_t>(i)] + f * (b[static_cast<std::size_t>(i) + 1] - b[static_cast<std::size_t>(i)]);
      out += p.walshMix * (wa + blend * (wb - wa));
    }
    return out * window;
  }
};

class PhaseGeometrySource final : public VoiceEngine<PhaseGeometryTraits> {};
} // namespace aether::source