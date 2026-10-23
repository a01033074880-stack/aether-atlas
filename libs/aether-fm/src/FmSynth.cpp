#include <aether/FmSynth.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace aether::fm {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int oversampling = 4;
float finite(float x, float fallback = 0) noexcept {
  return std::isfinite(x) ? x : fallback;
}
float bound(float x, float lo, float hi, float fallback = 0) noexcept {
  return std::clamp(finite(x, fallback), lo, hi);
}
constexpr int sineTableSize = 4096;
const std::array<float, sineTableSize + 1> &sineTable() noexcept {
  static const auto table = [] {
    std::array<float, sineTableSize + 1> t{};
    for (int i = 0; i <= sineTableSize; ++i)
      t[i] = static_cast<float>(std::sin(2 * pi * i / sineTableSize));
    return t;
  }();
  return table;
}
float oscillator(double phase, float modulation) noexcept {
  // Modulation is bounded to +/-24 radians, so adding eight cycles makes
  // truncation an exact wrap without a general-purpose transcendental call.
  double cycles = phase + modulation / (2 * pi) + 8;
  cycles -= static_cast<int>(cycles);
  const double position = cycles * sineTableSize;
  const auto i = static_cast<int>(position);
  const float fraction = static_cast<float>(position - i);
  const auto &table = sineTable();
  return table[i] + fraction * (table[i + 1] - table[i]);
}
struct Definition {
  const char *name;
  const char *edges;
  unsigned carriers;
  float depth;
};
// Pair "51" means operator 6 modulates operator 2 (zero based). These are
// independent graph shapes, including parallel, branching and recursive paths.
constexpr std::array<Definition, 32> definitions{
    {{"Monolith / Six Stack", "5443322110", 0x01, 1},
     {"Twin Towers / Two Stacks", "54432110", 0x09, .96f},
     {"Triptych / Three Pairs", "543210", 0x15, .92f},
     {"Solar Crown / Five Into One", "5040302010", 0x01, .88f},
     {"River Delta / One Into Five", "5051525354", 0x1f, .84f},
     {"Four Storeys / Double Crown", "5443323020", 0x03, .80f},
     {"Forked Stair / Split Chain", "5443323021", 0x03, .76f},
     {"Twin Fans / Two Into Two", "504052423121", 0x03, .72f},
     {"Nested Arches / Double Nest", "5443423120", 0x03, .68f},
     {"Orbit / Six Cycle", "544332211005", 0x01, .64f},
     {"Crossed Towers / Bridge", "5443211041", 0x09, .60f},
     {"Diamond / Convergent Crown", "545342312110", 0x01, .56f},
     {"Canopy / Three Branches", "5352313020", 0x03, .52f},
     {"Sundial / Four Satellites", "50403020", 0x03, .48f},
     {"Lattice / Layered Pairs", "5343524231203120", 0x03, .94f},
     {"Falling Rooms / Cascade Taps", "5443322110", 0x15, .90f},
     {"Open Windows / Full Cascade Taps", "5443322110", 0x3f, .86f},
     {"Parallel Suns / Six Carriers", "", 0x3f, .82f},
     {"Mirror Pool / Coupled Pairs", "544532231001", 0x15, .78f},
     {"Helix / Dual Cycles", "544335211002", 0x09, .74f},
     {"Wheel / Hub And Return", "504030201005", 0x01, .70f},
     {"Braided River / Two Bridges", "54433221105230", 0x01, .66f},
     {"Pendulum / Carrier Recursion", "544332211000", 0x01, .62f},
     {"Glass Steps / Two Recursive Tops", "555443332110", 0x09, .58f},
     {"Triangular Courtyard / Three Cycle", "5443252110", 0x09, .54f},
     {"Asymmetric Arch / Long Short", "54433220", 0x03, .50f},
     {"Suspended Bridge / Four Outputs", "54534232", 0x0f, .46f},
     {"Five Bells / Paired Accent", "10", 0x3d, .98f},
     {"Folded Map / Cross Modulation", "534241302015", 0x03, .91f},
     {"Spiral Rooms / Recursive Fan", "555453525150", 0x1f, .83f},
     {"Broken Column / Cascade And Solo", "54433221", 0x03, .73f},
     {"Impossible Stair / Nested Feedback", "54433221100325", 0x01, .63f}}};
const std::array<Algorithm, 32> &algorithms() noexcept {
  static const auto result = [] {
    std::array<Algorithm, 32> a{};
    for (int i = 0; i < 32; ++i) {
      a[i].name = definitions[i].name;
      const char *e = definitions[i].edges;
      for (; *e; e += 2)
        a[i].matrix[e[1] - '0'][e[0] - '0'] = definitions[i].depth;
      for (int op = 0; op < 6; ++op)
        a[i].carriers[op] = (definitions[i].carriers & (1u << op)) ? 1.f : 0.f;
    }
    return a;
  }();
  return result;
}
struct Envelope {
  enum Stage { off, attack, decay, sustain, release } stage = off;
  double value = 0, releaseStep = 0;
  void gate() noexcept {
    value = 0;
    stage = attack;
  }
  void ungate(float seconds, double rate) noexcept {
    if (stage != off && stage != release) {
      releaseStep = value / (seconds * rate);
      stage = release;
    }
  }
  float tick(double attackStep, double decayStep, float sustainValue) noexcept {
    switch (stage) {
    case attack:
      value += attackStep;
      if (value >= 1) {
        value = 1;
        stage = decay;
      }
      break;
    case decay:
      value -= decayStep;
      if (value <= sustainValue) {
        value = sustainValue;
        stage = sustain;
      }
      break;
    case sustain:
      value = sustainValue;
      break;
    case release:
      value -= releaseStep;
      if (value <= 1e-8f) {
        value = 0;
        stage = off;
      }
      break;
    case off:
      break;
    }
    return static_cast<float>(value);
  }
};
struct Voice {
  bool active = false, held = false;
  int note = 0, channel = 0;
  std::uint64_t age = 0;
  float velocity = 1;
  std::array<double, 6> phase{}, increment{};
  std::array<float, 6> previous{}, audible{};
  std::array<Envelope, 6> envelopes{};
};
// Linear host-sample ramps: repeated unchanged APVTS snapshots must not
// restart the transition. Double state also guarantees progress at high rates.
struct Ramp {
  double value = 0, target = 0, step = 0;
  int remaining = 0;
  void set(float next, int samples, bool snap) noexcept {
    if (snap) {
      value = target = next;
      step = 0;
      remaining = 0;
      return;
    }
    if (target == next)
      return;
    target = next;
    remaining = samples;
    step = (target - value) / samples;
  }
  float tick() noexcept {
    if (remaining > 0) {
      value += step;
      if (--remaining == 0)
        value = target;
    }
    return static_cast<float>(value);
  }
};
struct Biquad {
  double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void prepare(double q) noexcept {
    // Eighth-order Butterworth at .42 of host rate, running at 4x.
    const double w = 2 * pi * .42 / oversampling, c = std::cos(w),
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
    if (std::abs(z1) < 1e-25)
      z1 = 0;
    if (std::abs(z2) < 1e-25)
      z2 = 0;
    return static_cast<float>(y);
  }
};
} // namespace
const Algorithm &algorithm(int index) noexcept {
  return algorithms()[static_cast<std::size_t>(std::clamp(index, 0, 31))];
}
struct Synth::Impl {
  Parameters p{};
  double sampleRate = 48000, rate = 192000;
  std::array<Voice, 16> voices{};
  Voice audio{};
  float audioPitch = 220, audioEnvelope = 0;
  std::array<float, 16> bends{};
  std::array<bool, 16> pedals{};
  struct Edge {
    int source = 0;
    float depth = 0, weightedDepth = 0;
  };
  std::array<std::array<Edge, 6>, 6> edges{};
  std::array<int, 6> edgeCounts{};
  std::array<float, 6> carriers{}, panLeft{}, panRight{};
  std::array<double, 6> attackSteps{}, decaySteps{};
  Ramp gain, index, feedback;
  float currentIndex = 2, currentFeedback = .1f;
  std::array<std::array<Biquad, 4>, 2> filters{};
  std::uint64_t serial = 0;
  float carrierNorm = 1;
  void frequency(Voice &v, double base) noexcept {
    for (int op = 0; op < 6; ++op) {
      const double hz = base * p.operators[op].ratio *
                        std::exp2(p.operators[op].detune / 1200.0);
      v.increment[op] = std::min(hz / rate, .49);
      // Fade ultrasonic oscillators, including their contribution as
      // modulators.
      v.audible[op] = static_cast<float>(
          std::clamp((.48 * sampleRate - hz) / (.13 * sampleRate), 0., 1.));
    }
  }
  void frequencies() noexcept {
    for (auto &v : voices)
      if (v.active)
        frequency(
            v, 440 * std::exp2((v.note - 69 + p.transpose + bends[v.channel]) /
                               12.0));
    if (audio.active)
      frequency(audio, audioPitch * std::exp2(p.transpose / 12.0));
  }
  void coefficients() noexcept {
    const auto &a = algorithm(p.algorithm);
    const auto &next = algorithm((p.algorithm + 1) % 32);
    carrierNorm = 0;
    edgeCounts = {};
    for (int src = 0; src < 6; ++src) {
      for (int dst = 0; dst < 6; ++dst) {
        const float from = p.custom ? p.matrix[dst][src] : a.matrix[dst][src];
        const float to = p.custom ? a.matrix[dst][src] : next.matrix[dst][src];
        const float depth = from + (to - from) * p.morph;
        if (depth != 0)
          edges[dst][edgeCounts[dst]++] = {
              src, depth, depth * static_cast<float>(index.value)};
      }
      float weight = p.custom
                         ? 1.f
                         : a.carriers[src] +
                               (next.carriers[src] - a.carriers[src]) * p.morph;
      if (p.custom)
        weight = 1 + (a.carriers[src] - 1) * p.morph;
      carriers[src] = weight * p.operators[src].carrier;
      carrierNorm += carriers[src];
      attackSteps[src] =
          1 / (static_cast<double>(p.attack) * p.operators[src].attack * rate);
      decaySteps[src] = (1 - static_cast<double>(p.sustain)) / (p.decay * rate);
      const float pan = p.spread * (static_cast<float>(src) / 5 * 2 - 1);
      panLeft[src] = static_cast<float>(std::cos((pan + 1) * pi / 4));
      panRight[src] = static_cast<float>(std::sin((pan + 1) * pi / 4));
    }
    carrierNorm = 1 / std::max(1.f, carrierNorm);
    frequencies();
  }
  void release(Voice &v) noexcept {
    v.held = false;
    for (int op = 0; op < 6; ++op)
      v.envelopes[op].ungate(p.release * p.operators[op].release, rate);
  }
  void tick(Voice &v, float amplitude, float &left, float &right) noexcept {
    if (!v.active)
      return;
    std::array<float, 6> now{};
    bool alive = false;
    // Descending evaluation: higher -> lower operators use this sub-sample.
    // Other arcs and diagonal recursion use the previous 4x sub-sample.
    for (int dst = 5; dst >= 0; --dst) {
      float phaseInput = currentFeedback * v.previous[dst];
      for (int e = 0; e < edgeCounts[dst]; ++e) {
        const auto edge = edges[dst][e];
        phaseInput +=
            edge.weightedDepth *
            (edge.source > dst ? now[edge.source] : v.previous[edge.source]);
      }
      phaseInput = std::clamp(phaseInput, -24.f, 24.f);
      const float env =
          v.envelopes[dst].tick(attackSteps[dst], decaySteps[dst], p.sustain);
      alive |= v.envelopes[dst].stage != Envelope::off;
      now[dst] = oscillator(v.phase[dst], phaseInput) * env *
                 p.operators[dst].level * v.audible[dst];
      v.phase[dst] += v.increment[dst];
      if (v.phase[dst] >= 1)
        v.phase[dst] -= 1;
      const float out = now[dst] * carriers[dst] * amplitude * carrierNorm;
      left += out * panLeft[dst];
      right += out * panRight[dst];
    }
    v.previous = now;
    if (!alive)
      v.active = false;
  }
};
Synth::Synth() : impl(std::make_unique<Impl>()) {
  (void)algorithms();
  (void)sineTable();
  prepare(48000, 512);
}
Synth::~Synth() = default;
void Synth::prepare(double sampleRate, int maximumBlockSize) {
  (void)maximumBlockSize;
  impl->sampleRate = std::isfinite(sampleRate)
                         ? std::clamp(sampleRate, 8000., 192000.)
                         : 48000.;
  impl->rate = impl->sampleRate * oversampling;
  constexpr std::array<double, 4> q{.5097955791041592, .6013448869350453,
                                    .8999762231364156, 2.562915447741506};
  for (auto &channel : impl->filters)
    for (int i = 0; i < 4; ++i)
      channel[i].prepare(q[i]);
  reset();
  setParameters(impl->p);
}
void Synth::reset() noexcept {
  impl->voices = {};
  impl->audio = {};
  impl->audioEnvelope = 0;
  impl->bends = {};
  impl->pedals = {};
  impl->serial = 0;
  impl->gain.set(impl->p.gain, 1, true);
  impl->index.set(impl->p.index, 1, true);
  impl->feedback.set(impl->p.feedback, 1, true);
  for (auto &channel : impl->filters)
    for (auto &f : channel)
      f.z1 = f.z2 = 0;
}
void Synth::setParameters(const Parameters &incoming) noexcept {
  auto &p = impl->p;
  p = incoming;
  p.algorithm = std::clamp(p.algorithm, 0, 31);
  p.index = bound(p.index, 0, 12, 2);
  p.morph = bound(p.morph, 0, 1);
  p.feedback = bound(p.feedback, 0, .95f);
  p.attack = bound(p.attack, .001f, 5, .008f);
  p.decay = bound(p.decay, .01f, 5, .4f);
  p.sustain = bound(p.sustain, 0, 1, .7f);
  p.release = bound(p.release, .01f, 12, .5f);
  p.gain = bound(p.gain, 0, 1, .65f);
  p.spread = bound(p.spread, 0, 1, .25f);
  p.transpose = bound(p.transpose, -48, 48);
  for (auto &op : p.operators) {
    op.ratio = bound(op.ratio, .125f, 16, 1);
    op.level = bound(op.level, 0, 1, .7f);
    op.carrier = bound(op.carrier, 0, 1, 1);
    op.detune = bound(op.detune, -100, 100);
    op.attack = bound(op.attack, .1f, 4, 1);
    op.release = bound(op.release, .1f, 4, 1);
  }
  for (auto &row : p.matrix)
    for (auto &edge : row)
      edge = bound(edge, 0, 1);
  bool silent = !impl->audio.active;
  for (const auto &voice : impl->voices)
    silent &= !voice.active;
  const int rampSamples =
      std::max(1, static_cast<int>(impl->sampleRate * .005));
  impl->gain.set(p.gain, rampSamples, silent);
  impl->index.set(p.index, rampSamples, silent);
  impl->feedback.set(p.feedback, rampSamples, silent);
  impl->coefficients();
}
void Synth::noteOn(int note, float velocity, int channel) noexcept {
  if (note < 0 || note > 127 || channel < 1 || channel > 16)
    return;
  velocity = bound(velocity, 0, 1);
  if (velocity <= 0) {
    noteOff(note, channel);
    return;
  }
  Voice *target = nullptr;
  for (auto &v : impl->voices)
    if (!v.active) {
      target = &v;
      break;
    }
  if (!target)
    target = &*std::min_element(
        impl->voices.begin(), impl->voices.end(),
        [](const Voice &a, const Voice &b) { return a.age < b.age; });
  *target = {};
  target->active = true;
  target->held = true;
  target->note = note;
  target->channel = channel - 1;
  target->velocity = velocity;
  target->age = ++impl->serial;
  for (auto &e : target->envelopes)
    e.gate();
  impl->frequency(*target, 440 * std::exp2((note - 69 + impl->p.transpose +
                                            impl->bends[channel - 1]) /
                                           12.0));
}
void Synth::noteOff(int note, int channel) noexcept {
  if (channel < 1 || channel > 16)
    return;
  for (auto &v : impl->voices)
    if (v.active && v.note == note && v.channel == channel - 1 && v.held) {
      v.held = false;
      if (!impl->pedals[channel - 1])
        impl->release(v);
    }
}
void Synth::sustain(bool down, int channel) noexcept {
  if (channel < 1 || channel > 16)
    return;
  impl->pedals[channel - 1] = down;
  if (!down)
    for (auto &v : impl->voices)
      if (v.active && !v.held && v.channel == channel - 1)
        impl->release(v);
}
void Synth::pitchBend(float semitones, int channel) noexcept {
  if (channel < 1 || channel > 16)
    return;
  impl->bends[channel - 1] = bound(semitones, -48, 48);
  impl->frequencies();
}
void Synth::allNotesOff() noexcept {
  impl->voices = {};
  impl->pedals = {};
}
void Synth::setAudioExcitation(float pitchHz, float envelope) noexcept {
  auto &v = impl->audio;
  impl->audioEnvelope = bound(envelope, 0, 1);
  if (!std::isfinite(pitchHz) || pitchHz <= 0)
    impl->audioEnvelope = 0;
  impl->audioPitch = bound(pitchHz, 1, 24000, 220);
  if (impl->audioEnvelope > 0) {
    if (!v.active) {
      v = {};
      v.active = true;
      v.held = true;
      for (auto &e : v.envelopes)
        e.gate();
    }
    impl->frequency(v, impl->audioPitch * std::exp2(impl->p.transpose / 12.0));
  } else
    v = {};
}
void Synth::process(float *left, float *right, int samples) noexcept {
  if (!left || !right || samples <= 0)
    return;
  for (int n = 0; n < samples; ++n) {
    float l = 0, r = 0;
    const float gain = impl->gain.tick();
    const float newIndex = impl->index.tick();
    if (newIndex != impl->currentIndex) {
      impl->currentIndex = newIndex;
      for (int dst = 0; dst < 6; ++dst)
        for (int e = 0; e < impl->edgeCounts[dst]; ++e) {
          auto &edge = impl->edges[dst][e];
          edge.weightedDepth = edge.depth * newIndex;
        }
    }
    impl->currentFeedback = 2 * impl->feedback.tick();
    for (int sub = 0; sub < oversampling; ++sub) {
      l = 0;
      r = 0;
      for (auto &v : impl->voices)
        impl->tick(v, v.velocity, l, r);
      impl->tick(impl->audio, impl->audioEnvelope, l, r);
      l *= gain * .25f;
      r *= gain * .25f;
      for (auto &f : impl->filters[0])
        l = f.tick(l);
      for (auto &f : impl->filters[1])
        r = f.tick(r);
    }
    left[n] = impl->p.enabled ? l : 0;
    right[n] = impl->p.enabled ? r : 0;
  }
}
int Synth::activeVoices() const noexcept {
  int count = 0;
  for (const auto &v : impl->voices)
    count += v.active;
  return count;
}
} // namespace aether::fm
