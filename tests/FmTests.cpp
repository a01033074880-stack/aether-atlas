#include <aether/FmSynth.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <vector>
static std::atomic<std::size_t> allocations{0};
#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif
NOINLINE void *operator new(std::size_t n) {
  ++allocations;
  if (auto *p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
NOINLINE void *operator new[](std::size_t n) { return ::operator new(n); }
NOINLINE void operator delete(void *p) noexcept { std::free(p); }
NOINLINE void operator delete[](void *p) noexcept { std::free(p); }
NOINLINE void operator delete(void *p, std::size_t) noexcept { std::free(p); }
NOINLINE void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
using namespace aether::fm;
int failures = 0;
void check(bool x, const char *what) {
  std::cout << (x ? "PASS " : "FAIL ") << what << '\n';
  failures += !x;
}
double energy(const std::vector<float> &x) {
  double e = 0;
  for (float a : x)
    e += a * a;
  return e / x.size();
}
double distance(const std::vector<float> &a, const std::vector<float> &b) {
  double e = 0;
  for (std::size_t i = 0; i < a.size(); ++i)
    e += (a[i] - b[i]) * (a[i] - b[i]);
  return e / a.size();
}
struct Audio {
  std::vector<float> l, r;
  Audio(int n) : l(n), r(n) {}
};
Parameters sine() {
  Parameters p;
  p.custom = true;
  p.index = 0;
  p.feedback = 0;
  p.attack = .001f;
  p.decay = .01f;
  p.sustain = 1;
  p.release = .02f;
  p.spread = 0;
  for (int i = 1; i < 6; ++i)
    p.operators[i].carrier = 0;
  return p;
}
Audio render(Parameters p, int n = 8192, double sr = 48000, int block = 127,
             int note = 69) {
  Synth s;
  s.prepare(sr, block);
  s.setParameters(p);
  s.noteOn(note, 1);
  Audio a(n);
  for (int i = 0; i < n; i += block)
    s.process(a.l.data() + i, a.r.data() + i, std::min(block, n - i));
  return a;
}
double frequency(const std::vector<float> &x, double sr, int begin = 2048) {
  int crossings = 0, first = -1, last = -1;
  for (int i = begin + 1; i < (int)x.size(); ++i)
    if (x[i - 1] <= 0 && x[i] > 0) {
      if (first < 0)
        first = i;
      last = i;
      ++crossings;
    }
  return crossings > 1 ? (crossings - 1) * sr / (last - first) : 0;
}
int main() {
  {
    Synth silent;
    silent.prepare(48000, 8);
    std::array<float, 37> left, right;
    left.fill(123);
    right.fill(-456);
    silent.process(left.data(), right.data(), 37);
    check(
        std::all_of(left.begin(), left.end(), [](float v) { return v == 0; }) &&
            std::all_of(right.begin(), right.end(),
                        [](float v) { return v == 0; }),
        "distinct stereo buffers are replaced, including sizes above prepare "
        "maximum");
  }
  auto p = sine();
  auto a = render(p);
  check(energy(a.l) > 1e-4, "a note produces audio");
  check(std::abs(frequency(a.l, 48000) - 440) < 1,
        "single carrier A4 is 440 Hz");
  auto ratio = p;
  ratio.operators[0].ratio = 2;
  check(std::abs(frequency(render(ratio).l, 48000) - 880) < 2,
        "operator ratio changes frequency");
  ratio = p;
  ratio.operators[0].detune = 100;
  check(std::abs(frequency(render(ratio).l, 48000) - 466.164) < 2,
        "operator detune measured in cents");
  ratio = p;
  ratio.transpose = 12;
  check(std::abs(frequency(render(ratio).l, 48000) - 880) < 2,
        "transpose measured in semitones");
  auto quiet = p;
  quiet.operators[0].level = .35f;
  check(energy(render(quiet).l) < energy(a.l) * .3,
        "operator level changes amplitude");
  quiet = p;
  quiet.operators[0].carrier = 0;
  check(energy(render(quiet).l) == 0, "carrier outputs control audibility");
  auto slow = p;
  slow.attack = .1f;
  slow.operators[0].attack = 4;
  auto fast = slow;
  fast.operators[0].attack = .1f;
  check(energy(render(fast, 4096).l) > energy(render(slow, 4096).l) * 4,
        "operator attack scale changes onset");
  auto decay = p;
  decay.decay = .02f;
  decay.sustain = .1f;
  check(energy(render(decay).l) < energy(a.l) * .1,
        "decay and sustain shape amplitude");
  auto stereo = p;
  stereo.spread = 1;
  check(distance(render(stereo).l, render(stereo).r) > 1e-5,
        "stereo spread changes distinct channels");
  check(distance(a.l, render(p, 8192, 48000, 1).l) < 1e-15,
        "sample/block split invariance");
  for (double sr : {8000., 44100., 96000., 192000.}) {
    auto r = render(p, (int)sr / 4, sr);
    check(std::abs(frequency(r.l, sr, (int)sr / 16) - 440) < 3,
          "sample rate pitch invariance");
  }
  Synth s;
  s.prepare(48000, 256);
  s.setParameters(p);
  Audio b(16384);
  s.process(b.l.data(), b.r.data(), 137);
  s.noteOn(69, 1, 2);
  s.process(b.l.data() + 137, b.r.data() + 137, 1024);
  check(std::all_of(b.l.begin(), b.l.begin() + 137,
                    [](float x) { return x == 0; }) &&
            energy(b.l) > 0,
        "wrapper sample offset split starts on time");
  s.reset();
  s.setParameters(p);
  for (int n = 48; n < 65; ++n)
    s.noteOn(n, 1);
  check(s.activeVoices() == 16, "16 voice polyphony and oldest steal");
  s.noteOff(48);
  s.process(b.l.data(), b.r.data(), 4096);
  check(s.activeVoices() == 16,
        "stolen oldest note cannot release replacement");
  s.noteOff(49);
  s.process(b.l.data(), b.r.data(), 4096);
  check(s.activeVoices() == 15, "non-stolen note releases");
  s.reset();
  s.noteOn(69, 1, 2);
  s.sustain(true, 2);
  s.noteOff(69, 2);
  s.process(b.l.data(), b.r.data(), 4096);
  check(s.activeVoices() == 1, "channel sustain holds released key");
  s.sustain(false, 1);
  s.process(b.l.data(), b.r.data(), 4096);
  check(s.activeVoices() == 1, "other channel pedal is isolated");
  s.sustain(false, 2);
  s.process(b.l.data(), b.r.data(), 4096);
  check(s.activeVoices() == 0, "pedal up releases held voice");
  s.reset();
  s.noteOn(69, 1, 2);
  s.pitchBend(12, 1);
  s.process(b.l.data(), b.r.data(), 16384);
  check(std::abs(frequency(b.l, 48000) - 440) < 2, "channel bend is isolated");
  s.pitchBend(12, 2);
  s.process(b.l.data(), b.r.data(), 16384);
  check(std::abs(frequency(b.l, 48000) - 880) < 2,
        "channel bend changes target channel");
  s.allNotesOff();
  check(s.activeVoices() == 0, "all notes off clears MIDI voices");
  s.reset();
  s.noteOn(69, 1);
  s.process(b.l.data(), b.r.data(), 8192);
  std::vector<float> before(b.l.begin(), b.l.begin() + 8192);
  s.reset();
  s.noteOn(69, 1);
  s.process(b.l.data(), b.r.data(), 8192);
  check(std::equal(before.begin(), before.end(), b.l.begin()),
        "reset deterministic phase and filter histories");
  for (int i = 0; i < 6; ++i) {
    auto m = p;
    m.index = 3;
    m.operators[0].carrier = 0;
    m.operators[(i + 1) % 6].carrier = 1;
    m.matrix[(i + 1) % 6][i] = 1;
    auto dry = m;
    dry.matrix = {};
    check(distance(render(m).l, render(dry).l) > 1e-5,
          "every source operator really modulates destination");
  }
  auto m = p;
  m.index = 3;
  m.matrix[0][1] = 1;
  auto baseline = render(m).l;
  for (int control = 0; control < 4; ++control) {
    auto mod = m;
    if (control == 0)
      mod.index = .2f;
    if (control == 1)
      mod.operators[1].ratio = 2.71f;
    if (control == 2)
      mod.operators[1].level = .1f;
    if (control == 3)
      mod.feedback = .8f;
    check(distance(baseline, render(mod).l) > 1e-5,
          "index modulator ratio/level and feedback affect sound");
  }
  bool matrixCoverage = true;
  for (int source = 0; source < 6; ++source)
    for (int destination = 0; destination < 6; ++destination) {
      auto edge = sine();
      edge.index = 4;
      for (auto &op : edge.operators)
        op.carrier = 0;
      edge.operators[destination].carrier = 1;
      edge.operators[source].ratio = 2.37f;
      edge.matrix[destination][source] = .75f;
      auto disconnected = edge;
      disconnected.matrix = {};
      matrixCoverage &=
          distance(render(edge, 2048).l, render(disconnected, 2048).l) > 1e-6;
    }
  check(matrixCoverage,
        "all 36 matrix edges including self and backward are audible");
  auto velocitySound = [&](float velocity) {
    s.reset();
    s.setParameters(p);
    s.noteOn(69, velocity);
    s.process(b.l.data(), b.r.data(), 16384);
    return energy(b.l);
  };
  const auto loud = velocitySound(1);
  check(velocitySound(.5f) < loud * .26 && velocitySound(.5f) > loud * .24,
        "MIDI velocity controls amplitude");
  s.noteOn(69, 0);
  s.process(b.l.data(), b.r.data(), 4096);
  check(s.activeVoices() == 0, "velocity zero is note off");
  s.reset();
  s.setParameters(p);
  s.noteOn(69, 1, 1);
  s.noteOn(69, 1, 2);
  s.noteOff(69, 1);
  s.process(b.l.data(), b.r.data(), 4096);
  check(s.activeVoices() == 1, "note off is isolated by channel");
  s.noteOn(-1, 1);
  s.noteOn(128, 1);
  s.noteOn(60, 1, 0);
  s.noteOn(60, 1, 17);
  check(s.activeVoices() == 1, "invalid MIDI notes and channels are ignored");
  auto disabled = p;
  disabled.enabled = false;
  check(energy(render(disabled).l) == 0,
        "disabled synth replaces buffer with silence");
  for (int i = 0; i < 32; ++i) {
    Parameters builtin;
    builtin.algorithm = i;
    auto copied = builtin;
    copied.custom = true;
    copied.matrix = algorithm(i).matrix;
    for (int op = 0; op < 6; ++op)
      copied.operators[op].carrier = algorithm(i).carriers[op];
    check(distance(render(builtin, 1024).l, render(copied, 1024).l) < 1e-15,
          "builtin matrix and carriers can be copied exactly into custom");
  }
  std::set<std::string> names;
  std::vector<std::vector<float>> algorithms;
  for (int i = 0; i < 32; ++i) {
    Parameters ap;
    ap.algorithm = i;
    auto sound = render(ap).l;
    check(energy(sound) > 1e-6, "builtin algorithm audible");
    bool distinct = true;
    for (auto &prev : algorithms)
      distinct &= distance(prev, sound) > 1e-7;
    check(distinct, "builtin algorithm has distinct default rendered sound");
    algorithms.push_back(std::move(sound));
    names.insert(algorithm(i).name);
  }
  check(names.size() == 32, "32 uniquely named algorithms");
  Parameters morph;
  morph.algorithm = 4;
  auto start = render(morph).l;
  morph.morph = 1;
  auto end = render(morph).l;
  morph.algorithm = 5;
  morph.morph = 0;
  check(distance(end, render(morph).l) < 1e-15,
        "builtin morph one equals next algorithm");
  morph.algorithm = 4;
  morph.morph = .5f;
  auto middle = render(morph).l;
  check(distance(start, middle) > 1e-6 && distance(end, middle) > 1e-6,
        "algorithm morph has audible midpoint");
  morph.algorithm = 31;
  morph.morph = 1;
  Parameters wrapped;
  wrapped.algorithm = 0;
  check(distance(render(morph).l, render(wrapped).l) < 1e-15,
        "algorithm morph wraps 31 to zero");
  auto custom = p;
  custom.index = 2;
  custom.morph = 1;
  custom.algorithm = 3;
  auto target = custom;
  target.custom = false;
  target.morph = 0;
  check(distance(render(custom).l, render(target).l) < 1e-15,
        "custom morph one equals selected template");
  auto built = Parameters{};
  for (auto &op : built.operators)
    op.carrier = 0;
  check(energy(render(built).l) == 0,
        "builtin carriers multiplied by operator carrier controls");
  s.reset();
  s.setParameters(p);
  s.setAudioExcitation(220, 1);
  s.process(b.l.data(), b.r.data(), 16384);
  check(std::abs(frequency(b.l, 48000) - 220) < 1,
        "continuous audio excitation follows pitch");
  s.setAudioExcitation(330, 1);
  s.process(b.l.data(), b.r.data(), 16384);
  check(std::abs(frequency(b.l, 48000) - 330) < 1,
        "continuous excitation pitch updates without retrigger");
  const double fullExcitation = energy(b.l);
  s.setAudioExcitation(330, .25f);
  s.process(b.l.data(), b.r.data(), 16384);
  check(energy(b.l) < fullExcitation * .07,
        "continuous excitation external envelope controls gain");
  auto audioOnly = b.l;
  s.noteOn(69, 1);
  s.process(b.l.data(), b.r.data(), 16384);
  check(s.activeVoices() == 1 && distance(audioOnly, b.l) > 1e-4,
        "audio excitation and MIDI coexist");
  s.reset();
  s.process(b.l.data(), b.r.data(), 16384);
  check(energy(b.l) == 0, "reset also silences audio excitation");
  auto releaseDuration = [&](float scale) {
    s.reset();
    auto rp = p;
    rp.operators[0].release = scale;
    s.setParameters(rp);
    s.noteOn(69, 1);
    s.process(b.l.data(), b.r.data(), 1024);
    s.noteOff(69);
    s.process(b.l.data(), b.r.data(), 2048);
    return energy(std::vector<float>(b.l.begin() + 1024, b.l.begin() + 2048));
  };
  check(releaseDuration(4) > releaseDuration(.1f) + 1e-5,
        "operator release scale changes tail");
  auto high = p;
  high.operators[0].ratio = 16;
  check(energy(render(high, 8192, 8000, 128, 127).l) < 1e-12,
        "ultrasonic carrier is suppressed instead of folded");
  for (double sr : {8000., 48000., 192000.}) {
    s.prepare(sr, 7);
    auto extreme = Parameters{};
    extreme.custom = true;
    extreme.index = 12;
    extreme.feedback = .95f;
    for (auto &row : extreme.matrix)
      row.fill(1);
    for (auto &op : extreme.operators) {
      op.ratio = 1.3713f;
      op.level = 1;
      op.detune = 100;
    }
    s.setParameters(extreme);
    for (int i = 0; i < 16; ++i)
      s.noteOn(48 + i, 1);
    s.process(b.l.data(), b.r.data(), 16384);
    check(std::all_of(
              b.l.begin(), b.l.end(),
              [](float x) { return std::isfinite(x) && std::abs(x) < 8; }),
          "dense recursive extreme patch stays finite and bounded");
    check(energy(b.l) > 1e-8,
          "dense recursion test exercises audible oscillators");
  }
  Parameters invalid;
  invalid.index = std::numeric_limits<float>::infinity();
  invalid.attack = std::numeric_limits<float>::quiet_NaN();
  invalid.operators[0].ratio = std::numeric_limits<float>::infinity();
  invalid.matrix[0][0] = std::numeric_limits<float>::quiet_NaN();
  s.setParameters(invalid);
  s.pitchBend(std::numeric_limits<float>::infinity());
  s.setAudioExcitation(std::numeric_limits<float>::quiet_NaN(), 1);
  s.process(b.l.data(), b.r.data(), 16384);
  check(std::all_of(b.l.begin(), b.l.end(),
                    [](float x) { return std::isfinite(x); }),
        "invalid parameters and MIDI values sanitized");
  s.prepare(48000, 64);
  s.setParameters(p);
  auto count = allocations.load();
  for (int i = 0; i < 50; ++i) {
    s.setParameters(p);
    s.noteOn(i % 128, .8f, i % 16 + 1);
    s.pitchBend(2, i % 16 + 1);
    s.sustain(i % 2, i % 16 + 1);
    s.setAudioExcitation(220, .2f);
    s.process(b.l.data(), b.r.data(), 37);
    s.noteOff(i % 128, i % 16 + 1);
  }
  check(allocations == count,
        "parameter MIDI excitation and process paths allocate nothing");
  s.reset();
  s.setParameters(Parameters{});
  for (int i = 0; i < 16; ++i)
    s.noteOn(48 + i, 1);
  auto t = std::chrono::steady_clock::now();
  for (int i = 0; i < 8; ++i)
    s.process(b.l.data(), b.r.data(), 16384);
  std::cout << "16 voice render seconds="
            << std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                             t)
                   .count()
            << " for " << 8 * 16384. / 48000 << " audio seconds\n";
  std::cout << "Failures: " << failures << '\n';
  return failures ? 1 : 0;
}
