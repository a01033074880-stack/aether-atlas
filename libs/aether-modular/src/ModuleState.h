#pragma once
#include <Effects/chorus.h>
#include <Effects/decimator.h>
#include <Effects/wavefolder.h>
#include <Filters/ladder.h>
#include <PhysicalModeling/KarplusString.h>
#include <PhysicalModeling/resonator.h>
#include <Sampling/granularplayer.h>
#include <aether/Modular.h>
#include <algorithm>
#include <chowdsp_wdf/chowdsp_wdf.h>
#include <cmath>
#include <cstdint>
#include <signalsmith-stretch.h>
#include <vector>

namespace aether::modular::detail {
constexpr float pi = 3.14159265358979323846f, tau = 2 * pi;
inline float clean(float x, float fallback = 0) noexcept {
  return std::isfinite(x) ? x : fallback;
}
inline float clamp(float x, float lo, float hi) noexcept {
  return std::clamp(clean(x), lo, hi);
}
inline float bounded(float x) noexcept { return clamp(x, -8, 8); }
inline float gain(float db) noexcept { return std::pow(10.f, db * .05f); }
inline float pole(float hz, float sr) noexcept {
  return 1 - std::exp(-tau * std::min(hz, sr * .45f) / sr);
}
inline float soft(float x) noexcept { return std::tanh(x); }
struct Stereo {
  float l = 0, r = 0;
};
struct Band {
  float b0 = 0, a1 = 0, a2 = 0, frequency = 100, level = 0, reduction = 0,
        focus = 1;
  std::array<float, 2> z1{}, z2{};
  void prepare(float sr, float hz) noexcept {
    frequency = hz;
    float w = tau * std::min(hz, sr * .44f) / sr, alpha = std::sin(w) / 5.f;
    b0 = alpha / (1 + alpha);
    a1 = -2 * std::cos(w) / (1 + alpha);
    a2 = (1 - alpha) / (1 + alpha);
    reset();
  }
  void reset() noexcept {
    z1.fill(0);
    z2.fill(0);
    level = reduction = 0;
  }
  float tick(float x, int c) noexcept {
    float y = b0 * x + z1[c];
    z1[c] = -a1 * y + z2[c];
    z2[c] = -b0 * x - a2 * y;
    if (std::abs(z1[c]) < 1e-20f)
      z1[c] = 0;
    if (std::abs(z2[c]) < 1e-20f)
      z2[c] = 0;
    return y;
  }
};
struct Delay {
  std::vector<float> data;
  int write = 0;
  void prepare(int n) {
    data.resize(n);
    reset();
  }
  void reset() noexcept {
    std::fill(data.begin(), data.end(), 0);
    write = 0;
  }
  float read(float delay) const noexcept {
    float p = float(write) - std::clamp(delay, 1.f, float(data.size() - 2));
    if (p < 0)
      p += float(data.size());
    int i = int(p), j = (i + 1) % int(data.size());
    return data[i] + (data[j] - data[i]) * (p - i);
  }
  void push(float x) noexcept {
    data[write] = bounded(x);
    if (++write == int(data.size()))
      write = 0;
  }
};
// Root diode topology based on chowdsp_wdf's Shockley/RC examples. Source and
// shunt capacitor are real WDF ports. Upstream BSD notices remain vendored.
struct DiodeCircuit {
  using Source = chowdsp::wdft::ResistiveVoltageSourceT<float>;
  using Cap = chowdsp::wdft::CapacitorT<float>;
  Source source{2700};
  Cap cap{22e-9f};
  chowdsp::wdft::WDFParallelT<float, Source, Cap> parallel{source, cap};
  chowdsp::wdft::PolarityInverterT<float, decltype(parallel)> inverter{
      parallel};
  chowdsp::wdft::DiodePairT<float, decltype(inverter)> diode{inverter,
                                                             2.52e-9f};
  void prepare(float sr) { cap.prepare(sr); }
  void reset() noexcept {
    cap.reset();
    source.setVoltage(0);
  }
  void configure(float r, float c, float n) noexcept {
    source.setResistanceValue(r);
    cap.setCapacitanceValue(c);
    diode.setDiodeParameters(2.52e-9f, 25.85e-3f, n);
  }
  float tick(float x) noexcept {
    source.setVoltage(x);
    diode.incident(inverter.reflected());
    inverter.incident(diode.reflected());
    return chowdsp::wdft::voltage<float>(cap);
  }
};
struct Channel {
  daisysp::Wavefolder folder;
  daisysp::Decimator decimator;
  daisysp::ChorusEngine chorus;
  daisysp::LadderFilter ladder;
  daisysp::Resonator resonator;
  daisysp::String string;
  daisysp::GranularPlayer granular;
  DiodeCircuit diode;
  Delay delay;
  std::array<Delay, 4> allpass;
  std::vector<float> grain;
  std::array<float, 127> hilbert{};
  int grainWrite = 0, grainSize = 0, hilbertWrite = 0;
  float low = 0, dcIn = 0, dcOut = 0, previous = 0, phase = 0;
  std::array<float, 4> damp{};
  void prepare(float sr) {
    delay.prepare(int(sr * 1.2f) + 4);
    for (auto &d : allpass)
      d.prepare(int(sr * .3f) + 4);
    grainSize = int(sr * .5f);
    grain.resize(grainSize + 2);
    diode.prepare(sr);
    reset(sr);
  }
  void reset(float sr) noexcept {
    folder.Init();
    decimator.Init();
    decimator.SetSmoothCrushing(true);
    chorus.Init(sr);
    ladder = daisysp::LadderFilter{};
    ladder.Init(sr);
    resonator.Init(.24f, 24, sr);
    string.Init(sr);
    string.SetNonLinearity(0);
    diode.reset();
    delay.reset();
    for (auto &d : allpass)
      d.reset();
    std::fill(grain.begin(), grain.end(), 0);
    granular = daisysp::GranularPlayer{};
    granular.Init(grain.data(), grainSize, sr);
    hilbert.fill(0);
    grainWrite = hilbertWrite = 0;
    low = dcIn = dcOut = previous = phase = 0;
    damp.fill(0);
  }
  float dc(float x, float sr) noexcept {
    float y = x - dcIn + (1 - 20.f / sr) * dcOut;
    dcIn = x;
    dcOut = bounded(y);
    return dcOut;
  }
};
struct SlotState {
  std::array<Channel, 2> channel;
  signalsmith::stretch::SignalsmithStretch<float> spectral{1979};
  ModuleType type = ModuleType::Thru;
  float sr = 48000;
  int latency = 0, age = 0;
  std::array<float, 4> macro{}, v{};
  float tone = .5f, level = 1, leftGain = 1, rightGain = 1, mix = 1;
  float cachedLevel = 0, cachedPan = 0;
  bool configured = false;
  float bandAttack = 0, bandRelease = 0, bandReduction = 0, envelopeFast = 0,
        envelopeSlow = 0;
  float chaosX = .1f, chaosY = .2f, chaosZ = .3f;
  std::array<float, 127> hilbertKernel{};
  std::array<Band, 24> bands;
  float fastEnvelope = 0, slowEnvelope = 0, threshold = .03f, spectralTone = 1;
  std::array<Delay, 16> reverb;
  std::array<float, 16> reverbTime{}, reverbGain{}, reverbDamp{};
  void prepare(float rate) {
    sr = rate;
    bandAttack = pole(80, sr);
    bandRelease = pole(4, sr);
    bandReduction = pole(16, sr);
    envelopeFast = pole(100, sr);
    envelopeSlow = pole(5, sr);
    int window = 256;
    while (window < sr * .04f)
      window *= 2;
    spectral.configure(2, window, window / 4, true);
    latency = spectral.inputLatency() + spectral.outputLatency();
    for (auto &c : channel)
      c.prepare(sr);
    for (auto &d : reverb)
      d.prepare(int(sr * .26f) + 4);
    for (int i = 0; i < 24; ++i)
      bands[i].prepare(sr, 80.f * std::pow(200.f, float(i) / 23.f));
    for (int i = 0; i < 127; ++i) {
      int k = i - 63;
      hilbertKernel[i] =
          (k != 0 && k % 2 != 0)
              ? 2.f / (pi * k) * (.54f - .46f * std::cos(tau * i / 126.f))
              : 0.f;
    }
    reset();
  }
  void reset() noexcept {
    configured = false;
    spectral.reseed(1979);
    spectral.reset();
    for (auto &c : channel) {
      c.reset(sr);
      if (type == ModuleType::Modal)
        c.resonator.Init(.24f, 12, sr);
    }
    for (auto &b : bands)
      b.reset();
    for (auto &d : reverb)
      d.reset();
    reverbDamp.fill(0);
    fastEnvelope = slowEnvelope = 0;
    age = 0;
    chaosX = .1f;
    chaosY = .2f;
    chaosZ = .3f;
  }
  void configure(const Slot &s) noexcept {
    if (configured && macro == s.macro && mix == s.mix &&
        cachedLevel == s.levelDb && cachedPan == s.pan)
      return;
    configured = true;
    cachedLevel = s.levelDb;
    cachedPan = s.pan;
    macro = s.macro;
    mix = s.mix;
    level = gain(s.levelDb);
    leftGain = std::min(1.f, 1 - s.pan);
    rightGain = std::min(1.f, 1 + s.pan);
    const auto &info = moduleInfo(type);
    for (int i = 0; i < 4; ++i)
      v[i] = info.macros[i].minimum +
             macro[i] * (info.macros[i].maximum - info.macros[i].minimum);
    tone = pole(v[2], sr);
    if (type == ModuleType::Spectral) {
      threshold = gain(v[0]);
      for (auto &b : bands) {
        float oct = std::log2(b.frequency / v[2]) / 1.7f;
        b.focus = .2f + .8f * std::exp(-.5f * oct * oct);
      }
    }
    if (type == ModuleType::PrismPitch || type == ModuleType::SpectralFreeze) {
      float focus = type == ModuleType::SpectralFreeze ? macro[3] : macro[2];
      spectral.setTransposeSemitones(v[0], .005f + .12f * focus);
      spectral.setFormantSemitones(v[1], true);
      spectralTone = pole(300 + 15700 * focus, sr);
    }
    if (type == ModuleType::Space) {
      static constexpr float ratios[]{.493f, .527f, .563f, .601f, .643f, .677f,
                                      .719f, .751f, .787f, .823f, .859f, .887f,
                                      .919f, .947f, .977f, 1.f};
      for (int i = 0; i < 16; ++i) {
        reverbTime[i] = v[0] * sr * .001f * ratios[i];
        reverbGain[i] = std::exp(-6.907755f * reverbTime[i] / (sr * v[1]));
      }
    }
    for (int c = 0; c < 2; ++c) {
      auto &x = channel[c];
      switch (type) {
      case ModuleType::Modal:
      case ModuleType::Resonator:
        x.resonator.SetFreq(v[0]);
        x.resonator.SetStructure(v[1]);
        x.resonator.SetBrightness(v[2]);
        x.resonator.SetDamping(std::min(.94f, v[3]));
        break;
      case ModuleType::Wavefolder:
        x.folder.SetGain(v[0]);
        x.folder.SetOffset(v[1]);
        break;
      case ModuleType::WdfDiode:
        x.diode.configure(v[1], v[2] * 1e-9f, v[3]);
        break;
      case ModuleType::String:
        x.string.SetFreq(v[0]);
        x.string.SetBrightness(v[1]);
        x.string.SetDamping(v[2]);
        break;
      case ModuleType::Ladder:
        x.ladder.SetFreq(std::min(v[0], sr * .43f));
        x.ladder.SetRes(v[1]);
        x.ladder.SetInputDrive(v[2]);
        x.ladder.SetPassbandGain(v[3]);
        break;
      case ModuleType::LoFi:
        x.decimator.SetDownsampleFactor(v[0]);
        x.decimator.SetBitcrushFactor(v[1]);
        break;
      case ModuleType::Chorus:
        x.chorus.SetLfoFreq(v[0] * (c ? 1.071f : 1.f));
        x.chorus.SetDelayMs(std::min(v[2], 1100000.f / sr));
        x.chorus.SetLfoDepth(v[1]);
        x.chorus.SetFeedback(v[3]);
        break;
      default:
        break;
      }
    }
  }
  float allpassTick(Channel &c, float input, float time, float diffusion,
                    float damping, float spread) noexcept {
    float y = input;
    static constexpr float ratios[]{1, .733f, .521f, .367f};
    for (int k = 0; k < 4; ++k) {
      float d = c.allpass[k].read(time * sr * .001f * ratios[k] *
                                  (1 + spread * .13f * k));
      c.damp[k] += (d - c.damp[k]) * (1 - .94f * damping);
      float out = c.damp[k] - diffusion * y;
      c.allpass[k].push(y + diffusion * out);
      y = out;
    }
    return y;
  }
  Stereo tick(Stereo in) noexcept {
    Stereo out = in;
    float *ins[]{&in.l, &in.r};
    float *outs[]{&out.l, &out.r};
    if (type == ModuleType::Spectral) {
      float amplitude = std::max(std::abs(in.l), std::abs(in.r));
      fastEnvelope += (amplitude - fastEnvelope) * envelopeFast;
      slowEnvelope += (amplitude - slowEnvelope) * envelopeSlow;
      float transient =
          clamp((fastEnvelope - slowEnvelope) / (slowEnvelope + .005f), 0, 1);
      float protect = 1 - macro[3] * transient;
      Stereo correction{};
      for (auto &b : bands) {
        float l = b.tick(in.l, 0), r = b.tick(in.r, 1),
              amp = std::max(std::abs(l), std::abs(r));
        b.level += (amp - b.level) * (amp > b.level ? bandAttack : bandRelease);
        float reduction =
            b.level > threshold ? 1 - threshold / (b.level + 1e-12f) : 0;
        b.reduction += (reduction - b.reduction) * bandReduction;
        float g = b.reduction * macro[1] * b.focus * protect;
        correction.l += l * g;
        correction.r += r * g;
      }
      out = {in.l - .72f * correction.l, in.r - .72f * correction.r};
    } else if (type == ModuleType::Space) {
      float l = allpassTick(channel[0], in.l, 3 + macro[3] * 15,
                            .1f + .7f * macro[3], macro[2] * .3f, .2f),
            r = allpassTick(channel[1], in.r, 3 + macro[3] * 17,
                            .1f + .7f * macro[3], macro[2] * .3f, .6f);
      float sum = 0, outL = 0, outR = 0;
      for (int i = 0; i < 16; ++i) {
        float delayed = reverb[i].read(reverbTime[i]);
        reverbDamp[i] += (delayed - reverbDamp[i]) * (1 - .96f * macro[2]);
        sum += reverbDamp[i];
        outL += reverbDamp[i] * (i % 2 ? -1.f : 1.f);
        outR += reverbDamp[i] * (i % 4 < 2 ? 1.f : -1.f);
      }
      for (int i = 0; i < 16; ++i) {
        float excitation =
            .25f * (l * (i % 2 ? -1.f : 1.f) + r * (i % 4 < 2 ? 1.f : -1.f));
        reverb[i].push(excitation +
                       reverbGain[i] * (sum * .125f - reverbDamp[i]));
      }
      out = {.2f * in.l + .25f * outL, .2f * in.r + .25f * outR};
    } else if (type == ModuleType::PrismPitch ||
               type == ModuleType::SpectralFreeze) {
      bool frozen = type == ModuleType::SpectralFreeze && macro[2] > .5f &&
                    age > latency * 2;
      spectral.process(ins, frozen ? 0 : 1, outs, 1);
      if (age < latency * 3)
        ++age;
      channel[0].low += spectralTone * (out.l - channel[0].low);
      channel[1].low += spectralTone * (out.r - channel[1].low);
      out = {channel[0].low, channel[1].low};
      if (type != ModuleType::SpectralFreeze) {
        float mid = (out.l + out.r) * .5f, side = (out.l - out.r) * .5f * v[3];
        out = {mid + side, mid - side};
      }
    } else {
      if (type == ModuleType::Chaos) {
        float h = v[0] * 10 / sr, dx = 10 * (chaosY - chaosX),
              dy = chaosX * (28 - chaosZ) - chaosY,
              dz = chaosX * chaosY - 2.666667f * chaosZ;
        chaosX = clamp(chaosX + h * dx, -30, 30);
        chaosY = clamp(chaosY + h * dy, -40, 40);
        chaosZ = clamp(chaosZ + h * dz, 0, 60);
      }
      for (int ch = 0; ch < 2; ++ch) {
        auto &c = channel[ch];
        float x = *ins[ch], y = x;
        switch (type) {
        case ModuleType::Thru:
          break;
        case ModuleType::Modal:
        case ModuleType::Resonator: {
          float resonance = c.resonator.Process(x * .2f);
          y = (type == ModuleType::Modal ? .65f : .25f) * x +
              resonance * (type == ModuleType::Modal ? 2.f : 4.f);
          break;
        }
        case ModuleType::Colour: {
          float shaped = (1 - macro[3]) * soft((x + v[1]) * v[0]) +
                         macro[3] * std::sin((x + v[1]) * v[0]);
          c.low += tone * (shaped - c.low);
          y = c.dc(c.low, sr) * .65f;
          break;
        }
        case ModuleType::GrainCloud: {
          if (macro[3] <= .5f) {
            c.grain[c.grainWrite] = x;
            if (c.grainWrite == 0)
              c.grain[c.grainSize] = x;
            if (++c.grainWrite >= c.grainSize)
              c.grainWrite = 0;
          }
          y = c.granular.Process(v[0], v[1] * 100, v[2]);
          break;
        }
        case ModuleType::Wavefolder:
          c.low += tone * (c.folder.Process(x) - c.low);
          y = c.dc(c.low, sr) * gain(v[3]);
          break;
        case ModuleType::WdfDiode:
          y = c.diode.tick(x * v[0]) * .8f;
          break;
        case ModuleType::String:
          y = .2f * x + .6f * c.string.Process(soft(x * v[3]) * .15f);
          break;
        case ModuleType::Ladder:
          y = c.ladder.Process(x);
          break;
        case ModuleType::FrequencyShift: {
          c.hilbert[c.hilbertWrite] = bounded(x + v[2] * c.previous);
          float quadrature = 0;
          for (int k = 0; k < 127; ++k)
            if (hilbertKernel[k] != 0)
              quadrature += hilbertKernel[k] *
                            c.hilbert[(c.hilbertWrite - k + 127) % 127];
          float real = c.hilbert[(c.hilbertWrite - 63 + 127) % 127];
          if (++c.hilbertWrite == 127)
            c.hilbertWrite = 0;
          c.phase += (v[0] + (ch ? 1.f : -1.f) * v[1]) / sr;
          c.phase -= std::floor(c.phase);
          float cs = std::cos(tau * c.phase), sn = std::sin(tau * c.phase);
          y = real * cs + quadrature * sn * (1 - 2 * macro[3]);
          c.previous = y;
          break;
        }
        case ModuleType::RingMod: {
          c.phase += v[0] / sr;
          c.phase -= std::floor(c.phase);
          float carrier = std::sin(tau * (c.phase + (ch ? v[2] : -v[2]) * .5f));
          carrier =
              carrier * (1 - macro[1]) + std::tanh(carrier * 8) * macro[1];
          y = x * (1 - macro[3] + macro[3] * carrier);
          break;
        }
        case ModuleType::Comb: {
          float delayed = c.delay.read(v[0] * sr * .001f);
          c.low += (1 - .97f * macro[2]) * (delayed - c.low);
          c.delay.push(x + v[1] * c.low);
          y = .6f * x + .6f * v[3] * delayed;
          break;
        }
        case ModuleType::Diffuser:
          y = allpassTick(c, x, v[0], v[1], macro[2],
                          macro[3] * (ch ? 1.f : .5f));
          break;
        case ModuleType::LoFi:
          c.low += tone * (c.decimator.Process(soft(x * v[3])) - c.low);
          y = c.low;
          break;
        case ModuleType::Tape: {
          c.phase += .63f / sr;
          if (c.phase >= 1)
            c.phase -= 1;
          float wow = (std::sin(tau * c.phase) +
                       .22f * std::sin(tau * c.phase * 7.31f)) *
                      v[1] * sr * .001f;
          float delayed = c.delay.read(sr * .045f + wow);
          float input = soft(x * v[0] + delayed * v[3]);
          c.low += tone * (input - c.low);
          c.delay.push(c.low);
          y = delayed * .7f + c.low * .3f;
          break;
        }
        case ModuleType::Chaos: {
          float chaos = chaosX / 30;
          float cutoff =
              std::clamp(v[2] * (1 + macro[1] * chaos * 1.7f), 40.f, sr * .44f);
          c.low += pole(cutoff, sr) *
                   (soft(x * v[3] * (1 + macro[1] * chaos * .4f)) - c.low);
          y = c.low;
          break;
        }
        case ModuleType::Chorus:
          y = c.chorus.Process(x);
          break;
        default:
          break;
        }
        *outs[ch] = bounded(y);
      }
    }
    return {bounded((in.l + (out.l - in.l) * mix) * level * leftGain),
            bounded((in.r + (out.r - in.r) * mix) * level * rightGain)};
  }
};
} // namespace aether::modular::detail
