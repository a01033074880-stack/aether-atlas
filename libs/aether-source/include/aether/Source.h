#pragma once
// aether-source: polyphonic sound sources that sit next to aether::fm::Synth.
//
// Contract shared by every source in this library:
//  * Construction and prepare() are the only places that may do work that is not
//    strictly bounded; the object owns no heap storage at all, so processing,
//    noteOn/noteOff and setParameters never allocate, lock or perform I/O.
//  * All random numbers come from PCG32 streams seeded from CommonParameters::seed
//    plus the note-on serial number, so a render is bit-for-bit reproducible and
//    does not depend on the host block size.
//  * Every state update is per host sample (no block-rate control work), which is
//    what makes the output independent of how the host slices the timeline.
//  * Anti-aliasing is 4x oversampling with a 12th order Butterworth decimator
//    (switchable with CommonParameters::antiAlias so the benefit can be measured).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace aether::source {

inline constexpr int voiceCount = 16;
inline constexpr int oversamplingFactor = 4;
inline constexpr std::uint32_t defaultSeed = 0x41455448u; // "AETH"
inline constexpr double pi = 3.14159265358979323846;

inline float finiteOr(float x, float fallback = 0) noexcept {
  return std::isfinite(x) ? x : fallback;
}
inline float bound(float x, float lo, float hi, float fallback = 0) noexcept {
  return std::clamp(finiteOr(x, fallback), lo, hi);
}

// PCG-XSH-RR 32 (M. E. O'Neill, pcg-random.org; algorithm is public, this is an
// independent implementation of the published recurrence).
class Pcg32 {
public:
  constexpr Pcg32() noexcept { seed(0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL); }
  constexpr Pcg32(std::uint64_t state, std::uint64_t sequence) noexcept { seed(state, sequence); }
  constexpr void seed(std::uint64_t initialState, std::uint64_t sequence) noexcept {
    state_ = 0;
    increment_ = (sequence << 1u) | 1u;
    next();
    state_ += initialState;
    next();
  }
  constexpr std::uint32_t next() noexcept {
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + increment_;
    const auto xorShifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rotation = static_cast<std::uint32_t>(old >> 59u);
    return (xorShifted >> rotation) | (xorShifted << ((0u - rotation) & 31u));
  }
  // Uniform in the open interval (0,1): never returns exactly 0 or 1.
  double uniform() noexcept {
    return (static_cast<double>(next()) + 0.5) * (1.0 / 4294967296.0);
  }

private:
  std::uint64_t state_ = 0, increment_ = 1;
};

constexpr std::uint64_t splitmix64(std::uint64_t x) noexcept {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}

// Read-only lookup tables, built once (function-local static, no heap).
inline constexpr int cosTableSize = 4096;
inline constexpr int walshOrder = 64;       // rows/columns of the Walsh matrix
inline constexpr int walshTableSize = 1024; // samples per period in the oscillator table
struct Tables {
  std::array<float, cosTableSize + 1> cosine{};                                  // cos(2*pi*x), guard point
  std::array<std::array<float, walshTableSize + 1>, walshOrder> walsh{};         // sequency ordered
  std::array<std::array<float, walshOrder + 1>, walshOrder> walshCum{};          // running sum of the cell values, for ADAA
};
const Tables& tables() noexcept;

// Biquad (RBJ low-pass, i.e. bilinear transform with pre-warp) in double precision.
struct Biquad {
  double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void design(double cutoffCyclesPerSample, double q) noexcept {
    const double w = 2 * pi * cutoffCyclesPerSample, c = std::cos(w),
                 alpha = std::sin(w) / (2 * q), a0 = 1 + alpha;
    b0 = (1 - c) * .5 / a0;
    b1 = (1 - c) / a0;
    b2 = b0;
    a1 = -2 * c / a0;
    a2 = (1 - alpha) / a0;
    z1 = z2 = 0;
  }
  float tick(float x) noexcept {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    if (std::abs(z1) < 1e-25) z1 = 0;
    if (std::abs(z2) < 1e-25) z2 = 0;
    return static_cast<float>(y);
  }
  void clear() noexcept { z1 = z2 = 0; }
};

// 12th order Butterworth (six biquads) running at the oversampled rate, corner at
// 0.40 of the host rate: about -23 dB at host Nyquist, -42 dB at 0.6 of the host
// rate where content that folds back below 0.4 begins.
struct DecimationFilter {
  static constexpr int sections = 6;
  static constexpr double corner = 0.40; // fraction of host rate
  std::array<Biquad, sections> stage{};
  void prepare() noexcept {
    constexpr int order = 2 * sections;
    for (int k = 1; k <= sections; ++k) {
      const double q = 1.0 / (2.0 * std::sin((2 * k - 1) * pi / (2.0 * order)));
      stage[static_cast<std::size_t>(k - 1)].design(corner / oversamplingFactor, q);
    }
  }
  float tick(float x) noexcept {
    for (auto& s : stage) x = s.tick(x);
    return x;
  }
  void clear() noexcept {
    for (auto& s : stage) s.clear();
  }
};

struct DcBlocker {
  double r = 0.999, x1 = 0, y1 = 0;
  void prepare(double sampleRate) noexcept { r = std::exp(-2 * pi * 5.0 / sampleRate); clear(); }
  float tick(float x) noexcept {
    const double y = x - x1 + r * y1;
    x1 = x;
    y1 = std::abs(y) < 1e-25 ? 0 : y;
    return static_cast<float>(y1);
  }
  void clear() noexcept { x1 = y1 = 0; }
};

struct Adsr {
  enum Stage : std::uint8_t { off, attack, decay, sustain, release } stage = off;
  double value = 0, releaseStep = 0;
  void gate() noexcept { value = 0; stage = attack; }
  void ungate(double samples) noexcept {
    if (stage != off && stage != release) {
      releaseStep = value / std::max(1.0, samples);
      stage = release;
    }
  }
  float tick(double attackStep, double decayStep, float sustainLevel) noexcept {
    switch (stage) {
    case attack:
      value += attackStep;
      if (value >= 1) { value = 1; stage = decay; }
      break;
    case decay:
      value -= decayStep;
      if (value <= sustainLevel) { value = sustainLevel; stage = sustain; }
      break;
    case sustain: value = sustainLevel; break;
    case release:
      value -= releaseStep;
      if (value <= 1e-8) { value = 0; stage = off; }
      break;
    case off: break;
    }
    return static_cast<float>(value);
  }
};

struct CommonParameters {
  bool enabled = true;
  bool antiAlias = true; // 4x oversampling + decimation filter; false = native rate
  float attack = 0.005f, decay = 0.3f, sustain = 0.7f, release = 0.3f;
  float gain = 0.65f, spread = 0.25f, transpose = 0;
  std::uint32_t seed = defaultSeed;
};
inline void sanitizeCommon(CommonParameters& p) noexcept {
  p.attack = bound(p.attack, .001f, 5, .005f);
  p.decay = bound(p.decay, .01f, 5, .3f);
  p.sustain = bound(p.sustain, 0, 1, .7f);
  p.release = bound(p.release, .01f, 12, .3f);
  p.gain = bound(p.gain, 0, 1, .65f);
  p.spread = bound(p.spread, 0, 1, .25f);
  p.transpose = bound(p.transpose, -48, 48);
}

// Abstract voice-allocating source. Parameters are engine specific, so each concrete
// engine adds a typed setParameters(); everything the host loop needs is here.
class Source {
public:
  virtual ~Source() = default;
  virtual const char* name() const noexcept = 0;
  virtual void prepare(double sampleRate, int maximumBlockSize) = 0;
  virtual void reset() noexcept = 0;
  virtual void noteOn(int note, float velocity, int channel = 1) noexcept = 0;
  virtual void noteOff(int note, int channel = 1) noexcept = 0;
  virtual void sustain(bool down, int channel = 1) noexcept = 0;
  virtual void pitchBend(float semitones, int channel = 1) noexcept = 0;
  virtual void allNotesOff() noexcept = 0;
  // Replaces both channels (distinct pointers). No heap/locks/I/O.
  virtual void process(float* left, float* right, int samples) noexcept = 0;
  virtual int activeVoices() const noexcept = 0;
  // Performance values for aether::modular::GraphEngine::setPerformance().
  virtual float lastVelocity() const noexcept = 0;
  virtual float lastPitchHz() const noexcept = 0;
};
} // namespace aether::source