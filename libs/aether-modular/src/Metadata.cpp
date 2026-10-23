#include <aether/Modular.h>
#include <algorithm>

namespace aether::modular {
namespace {
using M = MacroInfo;
constexpr M macro(const char *n, const char *u, float lo, float hi,
                  float d = .5f) {
  return {n, u, lo, hi, d};
}
const std::array<ModuleInfo, 21> modules{
    {{"Thru",
      "Utility",
      "Transparent stereo connection. Slot mix, level and pan remain "
      "available.",
      "Aether original",
      {macro("Unused A", "", 0, 1), macro("Unused B", "", 0, 1),
       macro("Unused C", "", 0, 1), macro("Unused D", "", 0, 1)}},
     {"Spectral",
      "Spectral",
      "Twenty-four dynamically controlled resonance bands with transient "
      "protection and focused de-harshing.",
      "Aether original dynamic biquad filter bank / MIT",
      {macro("Threshold", "dB", -60, 0, .5f), macro("Amount", "", 0, 1, .45f),
       macro("Focus", "Hz", 100, 16000, .2f),
       macro("Transient", "", 0, 1, .6f)}},
     {"Modal",
      "Physical",
      "Twelve-mode resonant body with continuously variable material.",
      "DaisySP Resonator / MIT; Emilie Gillet, Electrosmith",
      {macro("Fundamental", "Hz", 40, 1200, .15f),
       macro("Material", "", 0, 1, .35f), macro("Brightness", "", 0, 1, .55f),
       macro("Decay", "", 0, 1, .6f)}},
     {"Colour",
      "Colour",
      "Asymmetric saturator with output tone filtering and DC removal.",
      "Aether original",
      {macro("Drive", "x", 1, 12, .3f), macro("Bias", "", -.3f, .3f, .55f),
       macro("Tone", "Hz", 200, 18000, .55f),
       macro("Character", "", 0, 1, .4f)}},
     {"Space",
      "Spatial",
      "Sixteen Householder-coupled delay lines with calibrated RT60 and damped "
      "stereo diffusion.",
      "Aether original",
      {macro("Size", "ms", 20, 250, .4f), macro("Decay", "s", .2f, 20, .18f),
       macro("Damping", "", 0, 1, .45f), macro("Diffusion", "", 0, 1, .65f)}},
     {"Prism Pitch",
      "Spectral",
      "Independent pitch and formant transformation with controllable harmonic "
      "focus and stereo width.",
      "Signalsmith Stretch 1.3.2 + Signalsmith Linear / MIT",
      {macro("Pitch", "st", -24, 24, .625f),
       macro("Formant", "st", -12, 12, .5f), macro("Tonality", "", 0, 1, .5f),
       macro("Width", "", 0, 2, .5f)}},
     {"Grain Cloud",
      "Granular",
      "Two overlapping grains read a live 500 ms circular capture; Freeze "
      "stops recording.",
      "DaisySP GranularPlayer / MIT; Vinicius Fernandes",
      {macro("Speed", "x", .25f, 2, .43f), macro("Pitch", "st", -12, 12, .5f),
       macro("Grain", "ms", 10, 150, .4f), macro("Freeze", "", 0, 1, 0.f)},
      3,
      .5f},
     {"Wavefolder",
      "Colour",
      "Daisy wavefolding with asymmetric bias, tone and output trim.",
      "DaisySP Wavefolder / MIT; Nick Donaldson",
      {macro("Fold", "x", 1, 16, .4f), macro("Bias", "", -.5f, .5f, .5f),
       macro("Tone", "Hz", 200, 18000, .6f), macro("Trim", "dB", -18, 0, .7f)}},
     {"WDF Diode",
      "Circuit",
      "Antiparallel Shockley diode root with resistive voltage drive and "
      "capacitive shunt. Actual wave-digital circuit.",
      "chowdsp_wdf / BSD-3-Clause; Chowdhury DSP",
      {macro("Drive", "x", 1, 20, .25f),
       macro("Resistance", "ohm", 100, 10100, .25f),
       macro("Capacitance", "nF", 1, 101, .2f),
       macro("Diodes", "", 1, 4, .33f)}},
     {"Resonator",
      "Physical",
      "Twenty-four sympathetic modes; audio excites the resonant body.",
      "DaisySP Resonator / MIT; Emilie Gillet, Electrosmith",
      {macro("Fundamental", "Hz", 40, 1200, .12f),
       macro("Material", "", 0, 1, .65f), macro("Brightness", "", 0, 1, .5f),
       macro("Decay", "", 0, 1, .7f)}},
     {"String",
      "Physical",
      "Audio-excited Karplus-Strong string. Excitation drive changes harmonic "
      "density; deterministic core.",
      "DaisySP String / MIT; Emilie Gillet, Electrosmith",
      {macro("Pitch", "Hz", 50, 1200, .15f), macro("Brightness", "", 0, 1, .6f),
       macro("Decay", "", .1f, .94f, .65f),
       macro("Excitation", "x", 1, 8, .25f)}},
     {"Ladder",
      "Filter",
      "Four-times internally oversampled Huovilainen ladder with continuous "
      "passband compensation.",
      "DaisySP LadderFilter / MIT; Richard van Hoesel, Infrasonic Audio",
      {macro("Cutoff", "Hz", 40, 18000, .18f),
       macro("Resonance", "", 0, 1.4f, .3f), macro("Drive", "x", .2f, 4, .4f),
       macro("Compensation", "", 0, .5f, .5f)}},
     {"Frequency Shift",
      "Modulation",
      "127-tap Hilbert analytic signal: additive Hz shift, not pitch "
      "transposition. Sideband blends down/up.",
      "Aether original Hilbert FIR / 63 sample group delay",
      {macro("Shift", "Hz", -500, 500, .6f), macro("Stereo", "Hz", 0, 50, .1f),
       macro("Feedback", "", 0, .8f, .15f), macro("Sideband", "", 0, 1, 1.f)}},
     {"Ring Mod",
      "Modulation",
      "Audio-rate bipolar carrier with sine-to-pulse shape and stereo phase "
      "spread.",
      "Aether original",
      {macro("Carrier", "Hz", 1, 2000, .07f), macro("Shape", "", 0, 1, .1f),
       macro("Stereo", "cycles", 0, .5f, .25f), macro("Depth", "", 0, 1, 1.f)}},
     {"Comb",
      "Delay",
      "Tuned fractional-delay comb with bounded feedback and damping.",
      "Aether original",
      {macro("Time", "ms", 2, 80, .12f), macro("Feedback", "", -.9f, .9f, .75f),
       macro("Damping", "", 0, 1, .4f), macro("Polarity", "", -1, 1, .8f)}},
     {"Diffuser",
      "Spatial",
      "Four serial Schroeder allpasses with prime delay ratios and "
      "controllable input diffusion.",
      "Aether original",
      {macro("Time", "ms", 1, 30, .35f),
       macro("Diffusion", "", .1f, .85f, .65f), macro("Damping", "", 0, 1, .4f),
       macro("Spread", "", 0, 1, .6f)}},
     {"LoFi",
      "Colour",
      "Sample-and-hold rate reduction and genuine integer bit crushing, "
      "followed by reconstruction tone.",
      "DaisySP Decimator / MIT; Electrosmith",
      {macro("Rate reduction", "", 0, .8f, .18f),
       macro("Bit crush", "", 0, .9f, .5f),
       macro("Tone", "Hz", 200, 18000, .65f), macro("Drive", "x", 1, 4, .2f)}},
     {"Tape",
      "Colour",
      "Saturated fractional delay with wow, flutter and damped repeats.",
      "Aether original",
      {macro("Drive", "x", 1, 8, .25f), macro("Wow", "ms", 0, 3, .3f),
       macro("Tone", "Hz", 300, 16000, .5f), macro("Echo", "", 0, .8f, .2f)}},
     {"Spectral Freeze",
      "Spectral",
      "Retains a captured analysis spectrum while synthesis continues. Freeze "
      "engages after one analysis window has filled.",
      "Signalsmith Stretch 1.3.2 + Signalsmith Linear / MIT",
      {macro("Pitch", "st", -12, 12, .5f), macro("Formant", "st", -12, 12, .5f),
       macro("Freeze", "", 0, 1, 0.f), macro("Tonality", "", 0, 1, .4f)},
      2,
      .5f},
     {"Chaos",
      "Modulation",
      "Bounded Lorenz attractor moves a resonant tone filter and nonlinear "
      "drive.",
      "Aether original bounded Lorenz dynamics",
      {macro("Speed", "", .1f, 4, .3f), macro("Depth", "", 0, 1, .6f),
       macro("Tone", "Hz", 100, 10000, .4f), macro("Drive", "x", 1, 8, .25f)}},
     {"Chorus",
      "Spatial",
      "Two independent modulated delay engines, with stereo rate offset and "
      "bounded feedback.",
      "DaisySP ChorusEngine / MIT; Electrosmith",
      {macro("Rate", "Hz", .05f, 5, .12f), macro("Depth", "", 0, 1, .5f),
       macro("Delay", "ms", 1, 5, .4f), macro("Feedback", "", 0, .8f, .25f)}}}};
constexpr std::array<const char *, 32> names{{"Twelve Steps",
                                              "Twelve Horizons",
                                              "Six Twins",
                                              "Four Triptychs",
                                              "Three Quartets",
                                              "Two Rivers",
                                              "Root and Leaves",
                                              "Gathering",
                                              "Binary Canopy",
                                              "Inverted Canopy",
                                              "Diamond Stair",
                                              "Three Diamonds",
                                              "Parallel Ladder",
                                              "Braided Steps",
                                              "Alternating Banks",
                                              "Twin Fans",
                                              "Long Echo",
                                              "Nested Echoes",
                                              "Three Echo Rings",
                                              "Four Echo Rings",
                                              "Twelve Mirrors",
                                              "Six Crossfeeds",
                                              "Signed Stair",
                                              "Signed Horizons",
                                              "Orbit Six",
                                              "Orbit Twelve",
                                              "Crossed Rivers",
                                              "Spiral Taps",
                                              "Butterfly",
                                              "Cascaded Fans",
                                              "Weave",
                                              "Constellation"}};
} // namespace
const ModuleInfo &moduleInfo(ModuleType t) noexcept {
  return modules[std::clamp(int(t), 0, 20)];
}
const char *algorithmName(int i) noexcept {
  return names[std::clamp(i, 0, 31)];
}
const char *sourceName(int i) noexcept {
  static constexpr const char *names[]{
      "None",     "LFO 1",    "LFO 2", "LFO 3",       "LFO 4",
      "Envelope", "Velocity", "Pitch", "Random Hold", "Chaos"};
  return names[std::clamp(i, 0, 9)];
}
void assignModule(Parameters &p, int slot, ModuleType t) noexcept {
  if (slot < 0 || slot >= 12)
    return;
  t = ModuleType(std::clamp(int(t), 0, 20));
  p.slots[slot].type = t;
  for (int i = 0; i < 4; ++i)
    p.slots[slot].macro[i] = moduleInfo(t).macros[i].defaultValue;
}
void applyAlgorithm(int a, Parameters &p) noexcept {
  p.input.fill(0);
  p.output.fill(0);
  for (auto &r : p.connection)
    r.fill(0);
  auto edge = [&](int s, int d, float g = 1.f) { p.connection[d][s] = g; };
  auto chains = [&](int length) {
    for (int b = 0; b < 12; b += length) {
      p.input[b] = 1;
      p.output[b + length - 1] = float(length) / 12;
      for (int i = b + 1; i < b + length; ++i)
        edge(i - 1, i);
    }
  };
  a = std::clamp(a, 0, 31);
  switch (a) {
  case 0:
    chains(12);
    break;
  case 1:
    chains(1);
    break;
  case 2:
    chains(2);
    break;
  case 3:
    chains(3);
    break;
  case 4:
    chains(4);
    break;
  case 5:
    chains(6);
    break;
  case 6:
    p.input[0] = 1;
    for (int n = 1; n < 12; ++n) {
      edge(0, n);
      p.output[n] = 1.f / 11;
    }
    break;
  case 7:
    for (int n = 0; n < 11; ++n) {
      p.input[n] = 1;
      edge(n, 11);
    }
    p.output[11] = 1;
    break;
  case 8:
    p.input[0] = 1;
    for (int n = 1; n < 12; ++n)
      edge((n - 1) / 2, n);
    for (int n = 6; n < 12; ++n)
      p.output[n] = 1.f / 6;
    break;
  case 9:
    for (int n = 0; n < 6; ++n)
      p.input[n] = 1;
    for (int n = 0; n < 11; ++n)
      edge(n, 6 + n / 2);
    p.output[11] = 1;
    break;
  case 10:
    p.input[0] = 1;
    for (int b = 0; b < 9; b += 3) {
      edge(b, b + 1);
      edge(b, b + 2);
      edge(b + 1, b + 3);
      edge(b + 2, b + 3);
    }
    edge(9, 10);
    edge(9, 11);
    p.output[10] = p.output[11] = .5f;
    break;
  case 11:
    for (int b = 0; b < 12; b += 4) {
      p.input[b] = 1;
      edge(b, b + 1);
      edge(b, b + 2);
      edge(b + 1, b + 3);
      edge(b + 2, b + 3);
      p.output[b + 3] = 1.f / 3;
    }
    break;
  case 12:
    chains(6);
    for (int n = 0; n < 6; ++n)
      edge(n, n + 6, .5f);
    break;
  case 13:
    chains(12);
    for (int n = 0; n < 10; ++n)
      edge(n, n + 2, .5f);
    break;
  case 14:
    for (int n = 0; n < 6; ++n) {
      p.input[n] = 1;
      edge(n, n + 6);
      edge(n, (n + 1) % 6 + 6, .5f);
      p.output[n + 6] = 1.f / 6;
    }
    break;
  case 15:
    for (int b : {0, 6}) {
      p.input[b] = 1;
      for (int n = b + 1; n < b + 6; ++n) {
        edge(b, n);
        p.output[n] = .1f;
      }
    }
    break;
  case 16:
    chains(12);
    edge(11, 0, .8f);
    break;
  case 17:
    chains(12);
    edge(11, 0, .7f);
    edge(8, 3, .5f);
    edge(6, 5, .3f);
    break;
  case 18:
    chains(4);
    for (int b = 0; b < 12; b += 4)
      edge(b + 3, b, .8f);
    break;
  case 19:
    chains(3);
    for (int b = 0; b < 12; b += 3)
      edge(b + 2, b, .7f);
    break;
  case 20:
    chains(1);
    for (int n = 0; n < 12; ++n)
      edge(n, n, .8f);
    break;
  case 21:
    chains(2);
    for (int n = 0; n < 12; n += 2)
      edge(n + 1, n, -.7f);
    break;
  case 22:
    chains(12);
    for (int n = 1; n < 12; n += 2)
      edge(n - 1, n, -1);
    break;
  case 23:
    chains(1);
    for (int n = 1; n < 12; n += 2)
      p.output[n] = -.06f;
    break;
  case 24:
    chains(6);
    edge(5, 6, .5f);
    edge(11, 0, .75f);
    break;
  case 25:
    chains(12);
    edge(11, 0, -.85f);
    for (int n = 0; n < 12; ++n)
      p.output[n] = 1.f / 12;
    break;
  case 26:
    chains(6);
    edge(2, 9, .7f);
    edge(8, 3, -.6f);
    break;
  case 27:
    chains(12);
    for (int n = 0; n < 12; ++n)
      p.output[n] = 1.f / 12;
    for (int n = 3; n < 12; n += 3)
      edge(n, n - 3, -.4f);
    break;
  case 28:
    p.input[0] = p.input[1] = 1;
    for (int n = 2; n < 12; ++n) {
      edge(n - 2, n);
      edge(n - 1, n, .6f);
    }
    p.output[10] = p.output[11] = .5f;
    break;
  case 29:
    p.input[0] = 1;
    for (int n = 1; n < 6; ++n) {
      edge(0, n);
      edge(n, 6);
    }
    for (int n = 7; n < 12; ++n) {
      edge(6, n);
      p.output[n] = .2f;
    }
    break;
  case 30:
    chains(3);
    for (int n = 0; n < 9; ++n)
      edge(n, n + 3, .5f);
    for (int n = 3; n < 12; n += 3)
      edge(n, n - 1, -.3f);
    break;
  case 31:
    for (int n = 0; n < 12; ++n) {
      p.input[n] = n % 3 == 0 ? 1.f : 0.f;
      p.output[n] = 1.f / 12;
      edge(n, (n + 5) % 12, .7f);
      edge(n, (n + 1) % 12, -.25f);
    }
    break;
  }
}
} // namespace aether::modular
