// Release-build CPU measurement for the new sources: 16 sustained voices.
#include <aether/Chaos.h>
#include <aether/PhaseGeometry.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

using namespace aether::source;
namespace {
template <class E, class P> void bench(const char* label, P p, int lowNote, int step, double rate, int block) {
  auto e = std::make_unique<E>();
  e->prepare(rate, block);
  e->setParameters(p);
  for (int i = 0; i < voiceCount; ++i) e->noteOn(lowNote + i * step, .8f);
  std::vector<float> l(static_cast<std::size_t>(block)), r(l.size());
  const int warm = static_cast<int>(rate / block);
  for (int i = 0; i < warm; ++i) e->process(l.data(), r.data(), block); // reach sustain
  const double seconds = 20;
  const int iterations = static_cast<int>(seconds * rate / block);
  double best = 1e30;
  for (int rep = 0; rep < 3; ++rep) {
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) e->process(l.data(), r.data(), block);
    best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
  }
  const double audio = static_cast<double>(iterations) * block / rate;
  std::printf("BENCH %-34s voices=%d active=%d rate=%.0f block=%d AA=%s audio_s=%.2f process_s=%.4f realtime_x=%.1f core_percent=%.2f\n",
              label, voiceCount, e->activeVoices(), rate, block, p.antiAlias ? "4x" : "off", audio, best, audio / best, best / audio * 100);
}
} // namespace
int main() {
#if defined(_MSC_VER)
  std::printf("compiler: MSVC %d, ", _MSC_FULL_VER);
#endif
#ifdef NDEBUG
  std::printf("NDEBUG defined (Release)\n");
#else
  std::printf("NDEBUG not defined (NOT a Release build)\n");
#endif
  for (int block : {64, 480}) {
    for (bool aa : {true, false}) {
      PhaseGeometryParameters pg;
      pg.sustain = 1; pg.czDepth = .7f; pg.walshMix = .3f; pg.walshSequency = 5.5f; pg.vpsD = .3f; pg.vpsV = .6f; pg.antiAlias = aa;
      bench<PhaseGeometrySource>("PhaseGeometry notes 36..111", pg, 36, 5, 48000, block);
      ChaosParameters ch;
      ch.sustain = 1; ch.antiAlias = aa;
      bench<ChaosSource>("Chaos seg12 notes 36..111", ch, 36, 5, 48000, block);
    }
  }
  ChaosParameters hard;
  hard.sustain = 1; hard.segments = 32; hard.ampStep = .5f; hard.durStep = .5f;
  bench<ChaosSource>("Chaos seg32 notes 84..99 (stress)", hard, 84, 1, 48000, 480);
  PhaseGeometryParameters pgHard;
  pgHard.sustain = 1; pgHard.czShape = 4; pgHard.czDepth = 1; pgHard.walshMix = 1; pgHard.walshSequency = 40.5f; pgHard.vpsRatio = 8; pgHard.vpsD = .2f; pgHard.vpsV = .9f;
  bench<PhaseGeometrySource>("PhaseGeometry heavy notes 84..99", pgHard, 84, 1, 48000, 480);
  return 0;
}