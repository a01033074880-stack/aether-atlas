#pragma once
// Shared polyphonic voice engine used by the concrete sources.
//
// A Traits type supplies:
//   using Params = struct derived from CommonParameters;
//   struct Voice : VoiceBase { engine specific state };
//   static constexpr const char* name;
//   static constexpr std::array<float Params::*, K> smoothed;  // one-pole smoothed members
//   static void sanitize(Params&) noexcept;                    // engine-specific clamping
//   static void start(Voice&, const Params&) noexcept;         // note-on state (voice.rng is seeded)
//   static float tick(Voice&, const Params&, const Tables&) noexcept; // one (sub)sample, |x| <= 1
#include <aether/Source.h>

namespace aether::source {

struct VoiceBase {
  bool active = false, held = false;
  int note = 0, channel = 0; // channel is zero based
  std::uint64_t age = 0;
  float velocity = 1, audible = 1, level = 0;
  double increment = 0; // cycles per (over)sampled tick
  Adsr env{};
  Pcg32 rng{};
};

template <class T> class VoiceEngine : public Source {
public:
  using Params = typename T::Params;
  using Voice = typename T::Voice;

  VoiceEngine() { prepare(48000, 512); }
  VoiceEngine(const VoiceEngine&) = delete;
  VoiceEngine& operator=(const VoiceEngine&) = delete;

  const char* name() const noexcept override { return T::name; }

  void prepare(double sampleRate, int maximumBlockSize) override {
    (void)maximumBlockSize;
    sampleRate_ = std::isfinite(sampleRate) ? std::clamp(sampleRate, 8000., 192000.) : 48000.;
    (void)tables();
    for (auto& f : filter_) f.prepare();
    for (auto& d : dc_) d.prepare(sampleRate_);
    smoothing_ = static_cast<float>(1 - std::exp(-1 / (.005 * sampleRate_)));
    reset();
    setParameters(target_);
  }
  void reset() noexcept override {
    voices_ = {};
    bends_ = {};
    pedals_ = {};
    serial_ = 0;
    lastVelocity_ = lastHz_ = 0;
    cur_ = target_;
    for (auto& f : filter_) f.clear();
    for (auto& d : dc_) d.clear();
  }

  void setParameters(const Params& incoming) noexcept {
    Params p = incoming;
    sanitizeCommon(p);
    T::sanitize(p);
    const bool silent = activeVoices() == 0;
    if (p.antiAlias != target_.antiAlias) {
      for (auto& f : filter_) f.clear();
    }
    const Params old = cur_;
    target_ = p;
    cur_ = p;
    if (!silent)
      for (auto member : T::smoothed) cur_.*member = old.*member;
    factor_ = p.antiAlias ? oversamplingFactor : 1;
    rate_ = sampleRate_ * factor_;
    attackStep_ = 1 / (static_cast<double>(p.attack) * sampleRate_);
    decayStep_ = (1 - static_cast<double>(p.sustain)) / (static_cast<double>(p.decay) * sampleRate_);
    for (int i = 0; i < voiceCount; ++i) {
      const float pan = p.spread * (static_cast<float>((i * 5) % voiceCount) / 15 * 2 - 1);
      panLeft_[static_cast<std::size_t>(i)] = static_cast<float>(std::cos((pan + 1) * pi / 4));
      panRight_[static_cast<std::size_t>(i)] = static_cast<float>(std::sin((pan + 1) * pi / 4));
    }
    for (auto& v : voices_)
      if (v.active) retune(v);
  }
  const Params& parameters() const noexcept { return target_; }

  void noteOn(int note, float velocity, int channel = 1) noexcept override {
    if (note < 0 || note > 127 || channel < 1 || channel > 16) return;
    velocity = bound(velocity, 0, 1);
    if (velocity <= 0) { noteOff(note, channel); return; }
    std::size_t slot = 0;
    bool found = false;
    for (std::size_t i = 0; i < voices_.size(); ++i)
      if (!voices_[i].active) { slot = i; found = true; break; }
    if (!found) {
      for (std::size_t i = 1; i < voices_.size(); ++i)
        if (voices_[i].age < voices_[slot].age) slot = i;
    }
    Voice& v = voices_[slot];
    v = Voice{};
    v.active = v.held = true;
    v.note = note;
    v.channel = channel - 1;
    v.velocity = velocity;
    v.age = ++serial_;
    v.rng.seed(splitmix64(static_cast<std::uint64_t>(target_.seed) ^ splitmix64(v.age)), slot);
    v.env.gate();
    retune(v);
    T::start(v, cur_);
    lastVelocity_ = velocity;
    lastHz_ = static_cast<float>(hertz(v));
  }
  void noteOff(int note, int channel = 1) noexcept override {
    if (channel < 1 || channel > 16) return;
    for (auto& v : voices_)
      if (v.active && v.note == note && v.channel == channel - 1 && v.held) {
        v.held = false;
        if (!pedals_[static_cast<std::size_t>(channel - 1)]) release(v);
      }
  }
  void sustain(bool down, int channel = 1) noexcept override {
    if (channel < 1 || channel > 16) return;
    pedals_[static_cast<std::size_t>(channel - 1)] = down;
    if (!down)
      for (auto& v : voices_)
        if (v.active && !v.held && v.channel == channel - 1) release(v);
  }
  void pitchBend(float semitones, int channel = 1) noexcept override {
    if (channel < 1 || channel > 16) return;
    bends_[static_cast<std::size_t>(channel - 1)] = bound(semitones, -48, 48);
    for (auto& v : voices_)
      if (v.active) retune(v);
  }
  void allNotesOff() noexcept override {
    pedals_ = {};
    for (auto& v : voices_)
      if (v.active) { v.held = false; release(v); }
  }
  int activeVoices() const noexcept override {
    int count = 0;
    for (const auto& v : voices_) count += v.active;
    return count;
  }
  float lastVelocity() const noexcept override { return lastVelocity_; }
  float lastPitchHz() const noexcept override { return lastHz_; }

  void process(float* left, float* right, int samples) noexcept override {
    if (!left || !right || samples <= 0) return;
    const Tables& tab = tables();
    for (int n = 0; n < samples; ++n) {
      for (auto member : T::smoothed) {
        float& c = cur_.*member;
        const float diff = target_.*member - c;
        if (diff != 0) c = std::abs(diff) < 1e-6f ? target_.*member : c + diff * smoothing_;
      }
      for (auto& v : voices_) {
        if (!v.active) continue;
        v.level = v.env.tick(attackStep_, decayStep_, target_.sustain);
        if (v.env.stage == Adsr::off) { v.active = false; v.level = 0; }
      }
      const float scale = cur_.gain * .5f;
      float l = 0, r = 0;
      for (int sub = 0; sub < factor_; ++sub) {
        float sl = 0, sr = 0;
        for (std::size_t i = 0; i < voices_.size(); ++i) {
          Voice& v = voices_[i];
          if (!v.active) continue;
          const float s = T::tick(v, cur_, tab) * v.level * v.velocity * v.audible;
          sl += s * panLeft_[i];
          sr += s * panRight_[i];
        }
        sl *= scale;
        sr *= scale;
        if (factor_ > 1) {
          sl = filter_[0].tick(sl);
          sr = filter_[1].tick(sr);
        }
        l = sl;
        r = sr;
      }
      l = dc_[0].tick(l);
      r = dc_[1].tick(r);
      l = std::isfinite(l) ? std::clamp(l, -16.f, 16.f) : 0.f;
      r = std::isfinite(r) ? std::clamp(r, -16.f, 16.f) : 0.f;
      left[n] = target_.enabled ? l : 0.f;
      right[n] = target_.enabled ? r : 0.f;
    }
  }

private:
  double hertz(const Voice& v) const noexcept {
    return 440 * std::exp2((v.note - 69 + target_.transpose + bends_[static_cast<std::size_t>(v.channel)]) / 12.0);
  }
  void retune(Voice& v) noexcept {
    const double hz = hertz(v);
    v.increment = std::min(hz / rate_, .49);
    v.audible = static_cast<float>(std::clamp((.48 * sampleRate_ - hz) / (.13 * sampleRate_), 0., 1.));
  }
  void release(Voice& v) noexcept { v.env.ungate(static_cast<double>(target_.release) * sampleRate_); }

  Params target_{}, cur_{};
  double sampleRate_ = 48000, rate_ = 192000, attackStep_ = 0, decayStep_ = 0;
  float smoothing_ = .004f, lastVelocity_ = 0, lastHz_ = 0;
  int factor_ = oversamplingFactor;
  std::array<Voice, voiceCount> voices_{};
  std::array<float, 16> bends_{};
  std::array<bool, 16> pedals_{};
  std::array<float, voiceCount> panLeft_{}, panRight_{};
  std::array<DecimationFilter, 2> filter_{};
  std::array<DcBlocker, 2> dc_{};
  std::uint64_t serial_ = 0;
};
} // namespace aether::source