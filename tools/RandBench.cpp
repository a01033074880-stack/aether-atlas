// AETHER 2.3.0 RandSpec P1: timing of aether::rand::generate() (non-realtime; allocates a std::vector).
// Usage: AetherRandBench [iterations=20000]   (build in Release)
#include "aether/Rand.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace aether::rand;

static StateView makeState(int mode) {
    StateView s;
    for (int i = 0; i < kParamCount; ++i) s.value[static_cast<std::size_t>(i)] = static_cast<float>(paramTable()[static_cast<std::size_t>(i)].defaultV);
    s.edition.isSynth = mode == 2;
    s.edition.effectiveMode = mode == 2 ? 0 : mode;
    s.value[kGraphEnabled] = 1.f;
    if (mode >= 1) { s.value[kSourceEnabled] = 1.f; s.value[kSourceEngine] = 1.f; }
    if (mode == 2) s.value[kFmEnabled] = 1.f;
    return s;
}

int main(int argc, char** argv) {
    const int n = argc > 1 ? std::max(1, std::atoi(argv[1])) : 20000;
    const char* modeNames[] = {"FX", "Resynth", "Synth"};
    const char* safetyNames[] = {"Safe", "Normal", "Wild"};
    std::size_t sink = 0;
    for (int mode = 0; mode < 3; ++mode) {
        const StateView st = makeState(mode);
        for (int m = 0; m < 2; ++m)
            for (int sf = 0; sf < 3; ++sf) {
                Config c;
                c.mode = m == 0 ? Mode::Full : Mode::Mutate;
                c.amount = 0.6f;
                c.safety = static_cast<Safety>(sf);
                std::vector<double> us;
                us.reserve(static_cast<std::size_t>(n));
                for (int i = 0; i < 200; ++i) { c.seed = static_cast<std::uint64_t>(i); sink += generate(st, c, LockSet{}).diffs.size(); }  // warm-up
                double total = 0, worst = 0;
                for (int i = 0; i < n; ++i) {
                    c.seed = static_cast<std::uint64_t>(i) * 2654435761ULL;
                    const auto t0 = std::chrono::steady_clock::now();
                    const Patch p = generate(st, c, LockSet{});
                    const auto t1 = std::chrono::steady_clock::now();
                    sink += p.diffs.size();
                    const double d = std::chrono::duration<double, std::micro>(t1 - t0).count();
                    us.push_back(d);
                    total += d;
                    worst = std::max(worst, d);
                }
                std::sort(us.begin(), us.end());
                std::printf("%-8s %-6s %-6s n=%d  avg %8.2f us  p50 %8.2f us  p99 %8.2f us  worst %8.2f us\n", modeNames[mode], m == 0 ? "Full" : "Mutate", safetyNames[sf], n, total / n,
                            us[us.size() / 2], us[static_cast<std::size_t>(static_cast<double>(us.size()) * 0.99)], worst);
            }
    }
    std::printf("checksum %zu\n", sink);
    return 0;
}
