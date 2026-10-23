#include "ModuleState.h"

namespace aether::modular {
using namespace detail;
struct GraphEngine::Impl {
  Parameters parameters;
  std::array<SlotState, slotCount> state;
  std::array<Stereo, slotCount> previous{}, current{};
  std::array<float, slotCount> meters{}, inputScale{};
  std::array<float, sourceCount> sources{};
  std::array<double, 4> phase{};
  std::array<float, 4> randomLfo{};
  std::array<Slot, slotCount> effective{};
  float sr = 48000, velocity = 0, pitch = 440, envelope = 0, outputScale = 1,
        trim = 1, held = 0;
  float envelopeAttack = 0, envelopeRelease = 0, pitchSource = 0;
  float chaosX = .1f, chaosY = .2f, chaosZ = .3f;
  uint32_t random = 0x6f37a219;
  int holdSamples = 0, control = 0;
  bool prepared = false;
  float noise() noexcept {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    return float(double(random) / 2147483648. - 1.);
  }
  void reset() noexcept {
    for (auto &s : state)
      s.reset();
    previous.fill({});
    current.fill({});
    meters.fill(0);
    sources.fill(0);
    phase.fill(0);
    randomLfo.fill(0);
    envelope = 0;
    held = 0;
    holdSamples = control = 0;
    random = 0x6f37a219;
    chaosX = .1f;
    chaosY = .2f;
    chaosZ = .3f;
  }
  void updateSources(float input) noexcept {
    float a = std::abs(input);
    envelope +=
        (a - envelope) * (a > envelope ? envelopeAttack : envelopeRelease);
    sources[5] = std::min(1.f, envelope * 2);
    sources[6] = velocity;
    sources[7] = pitchSource;
    for (int i = 0; i < 4; ++i) {
      const auto &l = parameters.lfo[i];
      double rate = l.rate * (l.sync ? parameters.tempo / 60 : 1);
      phase[i] += rate / sr;
      if (phase[i] >= 1) {
        phase[i] -= std::floor(phase[i]);
        randomLfo[i] = noise();
      }
      float p = float(phase[i] + l.phase);
      p -= std::floor(p);
      switch (l.shape) {
      case 1:
        sources[i + 1] = 1 - 4 * std::abs(p - .5f);
        break;
      case 2:
        sources[i + 1] = 2 * p - 1;
        break;
      case 3:
        sources[i + 1] = p < .5f ? 1.f : -1.f;
        break;
      case 4:
        sources[i + 1] = randomLfo[i];
        break;
      default:
        sources[i + 1] = std::sin(tau * p);
      }
    }
    if (--holdSamples <= 0) {
      held = noise();
      holdSamples = std::max(1, int(sr * .125f));
    }
    sources[8] = held;
    float dt = 8.f / sr, dx = 10 * (chaosY - chaosX),
          dy = chaosX * (28 - chaosZ) - chaosY,
          dz = chaosX * chaosY - 2.666667f * chaosZ;
    chaosX = clamp(chaosX + dt * dx, -30, 30);
    chaosY = clamp(chaosY + dt * dy, -40, 40);
    chaosZ = clamp(chaosZ + dt * dz, 0, 60);
    sources[9] = chaosX / 30;
  }
  void updateControls() noexcept {
    effective = parameters.slots;
    for (auto row : parameters.modulation) {
      if (row.source == 0 || row.target == 0 || row.depth == 0)
        continue;
      int slot = (row.target - 1) / 7, attr = (row.target - 1) % 7;
      float amount = sources[row.source] * row.depth;
      auto &s = effective[slot];
      if (attr < 4)
        s.macro[attr] = clamp(s.macro[attr] + amount, 0, 1);
      else if (attr == 4)
        s.mix = clamp(s.mix + amount, 0, 1);
      else if (attr == 5)
        s.levelDb = clamp(s.levelDb + 24 * amount, -24, 12);
      else
        s.pan = clamp(s.pan + amount, -1, 1);
    }
    for (int i = 0; i < 12; ++i)
      state[i].configure(effective[i]);
  }
};
GraphEngine::GraphEngine() : impl(std::make_unique<Impl>()) {}
GraphEngine::~GraphEngine() = default;
void GraphEngine::prepare(double sampleRate, int maximumBlockSize) {
  (void)maximumBlockSize;
  auto &e = *impl;
  e.sr = float(std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.,
                          8000., 192000.));
  e.envelopeAttack = 1 - std::exp(-1.f / (e.sr * .005f));
  e.envelopeRelease = 1 - std::exp(-1.f / (e.sr * .12f));
  e.pitchSource = clamp(std::log2(e.pitch / 20.f) / 10, 0, 1);
  for (auto &s : e.state)
    s.prepare(e.sr);
  e.prepared = true;
  e.reset();
  setParameters(e.parameters);
}
void GraphEngine::reset() noexcept {
  if (impl->prepared)
    impl->reset();
}
void GraphEngine::setParameters(const Parameters &p) noexcept {
  auto &e = *impl;
  e.parameters = p;
  auto &q = e.parameters;
  q.feedback = clamp(q.feedback, 0, .95f);
  q.trimDb = clamp(q.trimDb, -24, 12);
  q.tempo = std::clamp(std::isfinite(q.tempo) ? q.tempo : 120., 20., 400.);
  e.trim = gain(q.trimDb);
  float totalOutput = 0;
  for (int i = 0; i < 12; ++i) {
    auto &s = q.slots[i];
    s.type = ModuleType(std::clamp(int(s.type), 0, 20));
    s.mix = clamp(s.mix, 0, 1);
    s.levelDb = clamp(s.levelDb, -24, 12);
    s.pan = clamp(s.pan, -1, 1);
    for (auto &m : s.macro)
      m = clamp(m, 0, 1);
    q.input[i] = clamp(q.input[i], -1, 1);
    q.output[i] = clamp(q.output[i], -1, 1);
    totalOutput += std::abs(q.output[i]);
    float sum = std::abs(q.input[i]);
    for (auto &edge : q.connection[i]) {
      edge = clamp(edge, -1, 1);
      sum += std::abs(edge);
    }
    e.inputScale[i] = 1 / std::max(1.f, sum);
    if (e.state[i].type != s.type) {
      e.state[i].type = s.type;
      if (e.prepared) {
        e.state[i].reset();
        e.state[i].configure(s);
        e.effective[i] = s;
      }
    }
  }
  e.outputScale = 1 / std::max(1.f, totalOutput);
  for (auto &l : q.lfo) {
    l.rate = clamp(l.rate, .02f, 30);
    l.phase = clamp(l.phase, 0, 1);
    l.shape = std::clamp(l.shape, 0, 4);
  }
  for (auto &m : q.modulation) {
    m.source = std::clamp(m.source, 0, 9);
    m.target = std::clamp(m.target, 0, 84);
    m.depth = clamp(m.depth, -1, 1);
  }
  // Parameter updates do not reset the sample-clocked modulation cadence.
}
void GraphEngine::setPerformance(float velocity, float pitchHz) noexcept {
  impl->velocity = clamp(velocity, 0, 1);
  impl->pitch = clamp(pitchHz, 20, 20000);
  impl->pitchSource = clamp(std::log2(impl->pitch / 20.f) / 10, 0, 1);
}
void GraphEngine::process(float *left, float *right, const float *sidechain,
                          int samples) noexcept {
  auto &e = *impl;
  if (!e.prepared || !left || !right || samples <= 0)
    return;
  for (int n = 0; n < samples; ++n) {
    Stereo in{bounded(left[n]), bounded(right[n])};
    e.updateSources(sidechain ? bounded(sidechain[n]) : (in.l + in.r) * .5f);
    if (e.control-- <= 0) {
      e.updateControls();
      e.control = 15;
    }
    Stereo out{};
    for (int d = 0; d < 12; ++d) {
      Stereo x{in.l * e.parameters.input[d], in.r * e.parameters.input[d]};
      for (int s = 0; s < 12; ++s) {
        float weight = e.parameters.connection[d][s];
        if (weight == 0)
          continue;
        Stereo source = s < d ? e.current[s] : e.previous[s];
        if (s >= d) {
          weight *= e.parameters.feedback;
          source.l = soft(source.l);
          source.r = soft(source.r);
        }
        x.l += weight * source.l;
        x.r += weight * source.r;
      }
      x.l *= e.inputScale[d];
      x.r *= e.inputScale[d];
      e.current[d] = e.effective[d].enabled ? e.state[d].tick(x) : x;
      float meter =
          std::max(std::abs(e.current[d].l), std::abs(e.current[d].r));
      e.meters[d] = std::max(meter, e.meters[d] * .9995f);
      out.l += e.current[d].l * e.parameters.output[d];
      out.r += e.current[d].r * e.parameters.output[d];
    }
    e.previous = e.current;
    left[n] = bounded(out.l * e.outputScale * e.trim);
    right[n] = bounded(out.r * e.outputScale * e.trim);
  }
}
std::array<float, slotCount> GraphEngine::nodeLevels() const noexcept {
  return impl->meters;
}
std::array<float, sourceCount> GraphEngine::sourceLevels() const noexcept {
  return impl->sources;
}
int GraphEngine::intrinsicLatencySamples(int slot) const noexcept {
  if (slot < 0 || slot >= 12)
    return 0;
  auto &s = impl->state[slot];
  switch (s.type) {
  case ModuleType::PrismPitch:
  case ModuleType::SpectralFreeze:
    return s.latency;
  case ModuleType::FrequencyShift:
    return 63;
  default:
    return 0;
  }
}
} // namespace aether::modular
