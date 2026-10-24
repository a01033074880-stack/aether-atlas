// Numeric scan of every Delay::read call site in libs/aether-modular/src/ModuleState.h (branch test/fixtures-after-delay-fix).
// Question per call site: can p = write - clamp(delay,1,size-2); p<0 -> p+=size round to exactly size (so int(p)==size, read of data[size])?
// The OLD and NEW read() bodies are extracted verbatim from git (integration/2.0.1-rc and the fixed tree) by scan-delay.ps1 into
// delay_old.h / delay_new.h and compiled in here, so the index math under test is the real code.
// Only write == floor(d) can give a tiny negative p (any smaller write gives p < -1, any larger write gives p >= 0), so for a
// constant delay d the check is one evaluation; --bruteforce also checks every write for a sample of d to prove that shortcut.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
namespace sim {
inline float bounded(float x) noexcept { return std::clamp(std::isfinite(x) ? x : 0.f, -8.f, 8.f); }
}
namespace oldv { using sim::bounded;
#include "delay_old.h"
}
namespace newv { using sim::bounded;
#include "delay_new.h"
}
// Index only (same arithmetic as read(), literal copy of the two variants; checked against the extracted real code below).
static inline long idxOld(int write, float delay, int size) {
  float p = float(write) - std::clamp(delay, 1.f, float(size - 2));
  if (p < 0) p += float(size);
  return int(p);
}
static inline long idxNew(int write, float delay, int size) {
  float p = float(write) - std::clamp(delay, 1.f, float(size - 2));
  if (p < 0) p += float(size);
  if (p >= float(size)) p -= float(size);
  return int(p);
}
static inline bool hitOld(float d, int size) { // write = floor(d) (clamped like read does)
  float dc = std::clamp(d, 1.f, float(size - 2));
  int w = int(std::floor(dc));
  if (w >= size) return false;
  return idxOld(w, d, size) >= size;
}
static inline bool hitNew(float d, int size, bool& bad) {
  float dc = std::clamp(d, 1.f, float(size - 2));
  int w = int(std::floor(dc));
  long i = idxNew(w, d, size);
  if (i < 0 || i >= size) bad = true;
  return idxOld(w, d, size) >= size;
}
static float fromBits(uint32_t b) { float f; std::memcpy(&f, &b, 4); return f; }
struct Result { uint64_t total = 0, oldHits = 0, newBad = 0; float firstM = -1, firstD = -1; uint32_t firstBits = 0; };
template <class F> Result sweep(F delayOf, int size, uint32_t lo = 0, uint32_t hi = 0x3F800000u) {
  unsigned nt = std::max(1u, std::thread::hardware_concurrency() > 4 ? std::thread::hardware_concurrency() - 4 : 1u);
  std::vector<Result> rs(nt); std::vector<std::thread> th;
  uint64_t n = uint64_t(hi) - lo + 1;
  for (unsigned t = 0; t < nt; ++t) th.emplace_back([&, t] {
    Result r; uint64_t a = lo + n * t / nt, b = lo + n * (t + 1) / nt;
    for (uint64_t k = a; k < b; ++k) {
      float m = fromBits(uint32_t(k)); float d = delayOf(m); bool bad = false;
      bool h = hitNew(d, size, bad); ++r.total;
      if (h) { if (!r.oldHits) { r.firstM = m; r.firstD = d; r.firstBits = uint32_t(k); } ++r.oldHits; }
      if (bad) ++r.newBad;
    }
    rs[t] = r; });
  for (auto& x : th) x.join();
  Result s; for (auto& r : rs) { s.total += r.total; s.oldHits += r.oldHits; s.newBad += r.newBad; if (r.oldHits && s.firstM < 0) { s.firstM = r.firstM; s.firstD = r.firstD; s.firstBits = r.firstBits; } }
  return s;
}
static void report(const char* site, double sr, int size, const Result& r) {
  std::printf("%-34s sr=%-8.0f size=%-6d floats_in_[0,1]=%llu old_hits=%llu (%.4f%%) new_bad_index=%llu", site, sr, size,
              (unsigned long long)r.total, (unsigned long long)r.oldHits, 100.0 * double(r.oldHits) / double(r.total), (unsigned long long)r.newBad);
  if (r.oldHits) std::printf(" first m=%.9g (bits 0x%08X) d=%.9g", r.firstM, r.firstBits, r.firstD);
  std::printf("\n"); std::fflush(stdout);
}
int main(int argc, char** argv) {
  bool brute = false, tapeOnly = false, quick = false;
  for (int i = 1; i < argc; ++i) { if (!std::strcmp(argv[i], "--bruteforce")) brute = true; if (!std::strcmp(argv[i], "--tape-only")) tapeOnly = true; if (!std::strcmp(argv[i], "--quick")) quick = true; }
  const float srs[] = {44100.f, 48000.f, 88200.f, 96000.f, 176400.f, 192000.f};
  const int nSr = quick ? 2 : 6;
  std::printf("# DelayScan: exhaustive over all float32 macro values m in [0,1] (1065353217 values) unless noted\n");
  std::printf("# 'old_hits' = macro values for which the OLD index math reads data[size]; 'new_bad_index' = NEW index outside [0,size)\n");
  // sanity: real extracted code, same index decision
  {
    oldv::Delay o; newv::Delay n; o.prepare(12484); n.prepare(12484);
    for (int i = 0; i < 12484; ++i) { o.data[size_t(i)] = n.data[size_t(i)] = float(i + 1) * .001f; }
    int mism = 0, cnt = 0;
    for (int w = 0; w < 12484; w += 7) { o.write = n.write = w; for (float d : {1.f, 2.5f, 100.f, 3000.0005f, 5376.0005f, 12000.f}) { if (idxOld(w, d, 12484) >= 12484) continue; ++cnt; if (o.read(d) != n.read(d)) ++mism; } }
    std::printf("# extracted-real-code check: old.read == new.read on %d (write,delay) pairs where old index is in range: mismatches=%d\n", cnt, mism);
  }
  if (!tapeOnly) {
  // generic: how likely is a hit for a delay d uniformly random just above an integer, per buffer size
  for (int si = 0; si < nSr; ++si) {
    float sr = srs[si];
    // ---- Space reverb: reverb[i].read(reverbTime[i]); reverbTime = v0*sr*.001f*ratio ; v0 = 20 + m*(250-20) ; size = int(sr*.26f)+4
    static constexpr float ratios[]{.493f, .527f, .563f, .601f, .643f, .677f, .719f, .751f, .787f, .823f, .859f, .887f, .919f, .947f, .977f, 1.f};
    int szR = int(sr * .26f) + 4;
    uint64_t tot = 0, hits = 0, bad = 0;
    for (int i = 0; i < 16; ++i) {
      auto r = sweep([&](float m) { float v0 = 20.f + m * (250.f - 20.f); return v0 * sr * .001f * ratios[i]; }, szR);
      char name[64]; std::snprintf(name, sizeof name, "Space reverb[%d]", i); report(name, sr, szR, r); tot += r.total; hits += r.oldHits; bad += r.newBad;
    }
    std::printf("== Space reverb all 16 lines sr=%.0f: old_hits=%llu new_bad=%llu\n", sr, (unsigned long long)hits, (unsigned long long)bad);
    // ---- Space allpassTick (L: time=3+m*15 spread .2 ; R: time=3+m*17 spread .6), 4 stages, size = int(sr*.3f)+4
    static constexpr float arat[]{1, .733f, .521f, .367f};
    int szA = int(sr * .3f) + 4;
    for (int ch = 0; ch < 2; ++ch) for (int k = 0; k < 4; ++k) {
      float spread = ch ? .6f : .2f; float mul = ch ? 17.f : 15.f;
      auto r = sweep([&](float m) { float time = 3 + m * mul; return time * sr * .001f * arat[k] * (1 + spread * .13f * k); }, szA);
      char name[64]; std::snprintf(name, sizeof name, "Space allpass ch%d stage%d", ch, k); report(name, sr, szA, r);
    }
    // ---- Diffuser: time v0 = 1 + m0*(30-1); spread = macro3*(ch?1:.5); several macro3 values; exhaustive in m0
    for (float m3 : {0.f, .1f, .25f, .5f, .65f, .75f, 1.f}) for (int ch = 0; ch < 2; ++ch) for (int k = 0; k < 4; ++k) {
      float spread = m3 * (ch ? 1.f : .5f);
      auto r = sweep([&](float m) { float time = 1.f + m * (30.f - 1.f); return time * sr * .001f * arat[k] * (1 + spread * .13f * k); }, szA);
      char name[64]; std::snprintf(name, sizeof name, "Diffuser m3=%.2f ch%d stage%d", m3, ch, k); report(name, sr, szA, r);
    }
    // ---- Comb: delay = v0*sr*.001f ; v0 = 2 + m*(80-2) ; size = int(sr*1.2f)+4
    int szC = int(sr * 1.2f) + 4;
    { auto r = sweep([&](float m) { float v0 = 2.f + m * (80.f - 2.f); return v0 * sr * .001f; }, szC); report("Comb", sr, szC, r); }
  }
  // shortcut proof: brute-force every write for a sample of delays
  if (brute) {
    uint64_t checked = 0, shortcutMiss = 0;
    for (int size : {12484, 14404, 57604}) {
      for (int n = 1; n < size - 2; n += 3) for (int ulp = 0; ulp < 5; ++ulp) {
        float d = float(n); for (int u = 0; u < ulp; ++u) d = std::nextafter(d, 1e9f);
        bool any = false; for (int w = 0; w < size; ++w) if (idxOld(w, d, size) >= size) any = true;
        bool sc = hitOld(d, size); ++checked; if (any != sc) ++shortcutMiss;
      }
    }
    std::printf("# bruteforce shortcut check: %llu (size,d) pairs, all writes vs write==floor(d) only: disagreements=%llu\n", (unsigned long long)checked, (unsigned long long)shortcutMiss);
  }
  }
  // ---- Tape: d = sr*.045f + wow ; wow = (sin(tau*phase)+.22f*sin(tau*phase*7.31f))*v1*sr*.001f ; phase += .63f/sr per sample (wraps at 1)
  // Per-sample modulated, so simulate the real per-sample sequence with the real write counter and count old index overflows.
  {
    const float tau = 2 * 3.14159265358979323846f;
    for (int si = 0; si < nSr; ++si) {
      float sr = srs[si]; int size = int(sr * 1.2f) + 4;
      for (float m1 : {0.f, .1f, .3f, .5f, 1.f}) {
        float v1 = 0 + m1 * 3.f; float phase = 0; int write = 0; uint64_t hits = 0, bad = 0, n = 0;
        uint64_t target = quick ? uint64_t(sr) * 600 : uint64_t(sr) * 3600; // 1 hour of audio per config (10 min with --quick)
        for (uint64_t s = 0; s < target; ++s) {
          phase += .63f / sr; if (phase >= 1) phase -= 1;
          float wow = (std::sin(tau * phase) + .22f * std::sin(tau * phase * 7.31f)) * v1 * sr * .001f;
          float d = sr * .045f + wow;
          if (idxOld(write, d, size) >= size) ++hits;
          long ni = idxNew(write, d, size); if (ni < 0 || ni >= size) ++bad;
          if (++write == size) write = 0; ++n;
        }
        std::printf("Tape (one channel)  sr=%-7.0f size=%-6d Wow macro=%.2f (v1=%.2f ms): samples=%llu (%.0f s audio) old_hits=%llu new_bad_index=%llu; constant part sr*.045f=%.9g\n",
                    sr, size, m1, v1, (unsigned long long)n, double(n) / sr, (unsigned long long)hits, (unsigned long long)bad, double(sr * .045f));
        std::fflush(stdout);
      }
    }
  }
  return 0;
}
