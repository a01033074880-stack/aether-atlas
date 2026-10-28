// AETHER 2.3.0 RandSpec P1: tests for the pure randomization engine (JUCE-free).
#include "aether/Rand.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <thread>

using namespace aether::rand;

// Golden hash of 72 generate() calls (Full+Mutate, 3 editions x 3 safeties x 4 seeds). Pinned on both MSVC and clang.
static constexpr std::uint64_t kGolden = 0x3f54ff2101685d81ULL;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond)                                                                                      \
    do {                                                                                                 \
        ++g_checks;                                                                                      \
        if (!(cond)) { ++g_failures; std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)
#define CHECK_MSG(cond, ...)                                                                             \
    do {                                                                                                 \
        ++g_checks;                                                                                      \
        if (!(cond)) { ++g_failures; std::fprintf(stderr, "FAIL %s:%d  %s | ", __FILE__, __LINE__, #cond); std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } \
    } while (0)

namespace {

constexpr std::uint64_t kSeeds[] = {0, 1, 2, 3, 7, 42, 12345, 0xFFFFFFFFFFULL, 0x8000000000000000ULL, 0xFFFFFFFFFFFFFFFFULL};

StateView baseState(int mode /*0 FX,1 Resynth,2 Synth*/) {
    StateView s;
    for (int i = 0; i < kParamCount; ++i) s.value[static_cast<std::size_t>(i)] = static_cast<float>(paramTable()[static_cast<std::size_t>(i)].defaultV);
    s.edition.isSynth = mode == 2;
    s.edition.effectiveMode = mode == 2 ? 0 : mode;
    s.value[kGraphEnabled] = 1.f;
    if (mode >= 1) { s.value[kSourceEnabled] = 1.f; s.value[kSourceEngine] = 1.f; }
    if (mode == 2) { s.value[kFmEnabled] = 1.f; }
    return s;
}
Config cfgFull(std::uint64_t seed, Safety sf = Safety::Safe) { Config c; c.seed = seed; c.mode = Mode::Full; c.safety = sf; return c; }
Config cfgMut(std::uint64_t seed, float amount, Safety sf = Safety::Safe) { Config c; c.seed = seed; c.mode = Mode::Mutate; c.amount = amount; c.safety = sf; return c; }

bool samePatch(const Patch& a, const Patch& b) {
    if (a.diffs.size() != b.diffs.size() || a.report.ok != b.report.ok || std::strcmp(a.report.reason, b.report.reason) != 0) return false;
    for (std::size_t i = 0; i < a.diffs.size(); ++i)
        if (a.diffs[i].specIndex != b.diffs[i].specIndex || std::memcmp(&a.diffs[i].value, &b.diffs[i].value, 4) != 0) return false;
    return true;
}
std::uint64_t hashPatch(const Patch& p, std::uint64_t h = 0xcbf29ce484222325ULL) {
    auto mix = [&](std::uint32_t v) { for (int k = 0; k < 4; ++k) { h ^= (v >> (8 * k)) & 0xffu; h *= 0x100000001b3ULL; } };
    for (const auto& d : p.diffs) { std::uint32_t b; std::memcpy(&b, &d.value, 4); mix(static_cast<std::uint32_t>(d.specIndex)); mix(b); }
    mix(p.report.ok ? 1u : 0u);
    return h;
}
bool inRange(int i, float v) {
    const auto& pi = paramTable()[static_cast<std::size_t>(i)];
    return std::isfinite(v) && v >= static_cast<float>(pi.minV) && v <= static_cast<float>(pi.maxV);
}
bool diffsInRange(const Patch& p) {
    for (const auto& d : p.diffs) if (d.specIndex < 0 || d.specIndex >= kParamCount || !inRange(d.specIndex, d.value)) return false;
    return true;
}
bool isFmSrc(int i) { return i >= kFmOffset; }
bool changed(const StateView& s, const Patch& p, int i) {
    for (const auto& d : p.diffs) if (d.specIndex == i) return std::memcmp(&d.value, &s.value[static_cast<std::size_t>(i)], 4) != 0;
    return false;
}

// ---- known answers / math -------------------------------------------------------------------------------------------
void testPrimitives() {
    CHECK(fnv1a64("") == 0xcbf29ce484222325ULL);
    CHECK(fnv1a64("a") == 0xaf63dc4c8601ec8cULL);
    CHECK(fnv1a64("foobar") == 0x85944171f73967e8ULL);
    Pcg32 g(42, 54);  // pcg32-demo reference values
    const std::uint32_t ref[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (std::uint32_t r : ref) CHECK(g.next32() == r);
    Pcg32 h(1, 2);
    for (int i = 0; i < 100000; ++i) { const double u = h.nextUnit(); if (!(u >= 0.0 && u < 1.0)) { CHECK(false); break; } }
    double worst = 0;
    for (int k = 1; k <= 4000; ++k) {
        const double x = 1e-4 * std::pow(1.0021, k);  // 1e-4 .. ~4e0 .. 1e4
        worst = std::max(worst, std::abs(log2Det(x) - std::log2(x)));
    }
    CHECK_MSG(worst < 1e-12, "log2 worst %g", worst);
    worst = 0;
    for (int k = -3000; k <= 3000; ++k) {
        const double x = k * 0.01;
        worst = std::max(worst, std::abs(exp2Det(x) / std::exp2(x) - 1.0));
    }
    CHECK_MSG(worst < 1e-12, "exp2 rel worst %g", worst);
    CHECK(std::isfinite(exp2Det(5000.0)) && exp2Det(-5000.0) >= 0.0);
    CHECK(std::abs(powDet(0.5, 0.8) - std::pow(0.5, 0.8)) < 1e-12);
    // trim helper (frozen 9.1.1 #4)
    CHECK(trimDbForLoudness(-18.f) == 0.f);
    CHECK(trimDbForLoudness(-30.f) == 0.f);  // never boosts
    CHECK(trimDbForLoudness(-6.f) == -12.f);
    CHECK(trimDbForLoudness(40.f) == -24.f);
    CHECK(trimDbForLoudness(std::numeric_limits<float>::quiet_NaN()) == 0.f);
}

// ---- table integrity ------------------------------------------------------------------------------------------------
void testTable() {
    const auto t = paramTable();
    std::set<std::string> ids;
    int cls[3] = {0, 0, 0};
    for (int i = 0; i < kParamCount; ++i) {
        const auto& p = t[static_cast<std::size_t>(i)];
        CHECK(p.id && *p.id);
        CHECK(ids.insert(p.id).second);  // unique
        CHECK(p.hash == fnv1a64(p.id));
        CHECK(paramIndex(p.id) == i);
        ++cls[static_cast<int>(p.cls)];
        CHECK(p.minV <= p.maxV && p.defaultV >= p.minV && p.defaultV <= p.maxV);
        if (p.cls == Classification::Never) CHECK(p.role == Role::Never);
        const bool ratio = i >= kOpOffset && i < kEdgeOffset && (i - kOpOffset) % 6 == 0;  // value sets, not windows
        for (int s = 0; s < 3 && !ratio; ++s) {
            if (p.cls == Classification::Random && p.dist != Dist::None && p.dist != Dist::PerType && p.dist != Dist::Derived) {
                CHECK_MSG(p.win[s].lo <= p.win[s].hi, "%s window %d", p.id, s);
                CHECK_MSG(p.win[s].lo >= p.minV - 1e-6 && p.win[s].hi <= p.maxV + 1e-6, "%s window %d outside range", p.id, s);
            }
        }
    }
    CHECK(ids.size() == static_cast<std::size_t>(kParamCount));
    CHECK_MSG(cls[0] == 175 && cls[1] == 273 && cls[2] == 47, "classes %d/%d/%d", cls[0], cls[1], cls[2]);
    CHECK(paramIndex("graph.enabled") == kGraphEnabled && paramIndex("fm.enabled") == kFmEnabled && paramIndex("source.engine") == kSourceEngine);
    CHECK(paramIndex("no.such.param") == -1);
    // macro / fm / route tables
    for (int ty = 1; ty < kModuleTypeCount; ++ty)
        for (int k = 0; k < 4; ++k) {
            const auto& m = macroInfo(ty, k);
            for (int s = 0; s < 3; ++s) CHECK_MSG(m.win[s].lo <= m.win[s].hi && m.win[s].lo >= 0.0 && m.win[s].hi <= 1.0, "macro %d/%d/%d", ty, k, s);
        }
    for (int a = 0; a < kFmAlgorithmCount; ++a) CHECK(fmAlgoInfo(a).carrierMask != 0);
    int safeRoutes = 0;
    for (int a = 0; a < kRouteAlgorithmCount; ++a) safeRoutes += routeAlgoInfo(a).feedbackArcs == 0;
    CHECK_MSG(safeRoutes == 20, "feedback-free routing algorithms %d", safeRoutes);
}

// ---- determinism ----------------------------------------------------------------------------------------------------
void testDeterminism() {
    for (int mode = 0; mode < 3; ++mode) {
        const StateView s = baseState(mode);
        LockSet lk;
        for (std::uint64_t seed : kSeeds)
            for (Safety sf : {Safety::Safe, Safety::Normal, Safety::Wild}) {
                CHECK(samePatch(generate(s, cfgFull(seed, sf), lk), generate(s, cfgFull(seed, sf), lk)));
                CHECK(samePatch(generate(s, cfgMut(seed, 0.5f, sf), lk), generate(s, cfgMut(seed, 0.5f, sf), lk)));
            }
        std::set<std::uint64_t> hashes;
        for (std::uint64_t seed = 1; seed <= 50; ++seed) hashes.insert(hashPatch(generate(s, cfgFull(seed, Safety::Normal), lk)));
        CHECK_MSG(hashes.size() >= 48, "mode %d: only %zu distinct patches of 50 seeds", mode, hashes.size());
        hashes.clear();
        for (std::uint64_t seed = 1; seed <= 50; ++seed) hashes.insert(hashPatch(generate(s, cfgMut(seed, 0.7f, Safety::Normal), lk)));
        CHECK_MSG(hashes.size() >= 48, "mode %d: mutate only %zu distinct", mode, hashes.size());
    }
    // Full ignores amount; re-roll attempts differ
    const StateView s = baseState(2);
    LockSet lk;
    Config a = cfgFull(9), b = cfgFull(9);
    a.amount = 0.1f; b.amount = 0.9f;
    CHECK(samePatch(generate(s, a, lk), generate(s, b, lk)));
    std::set<std::uint64_t> att;
    for (int k = 0; k < 6; ++k) { Config c = cfgFull(9); c.attempt = k; att.insert(hashPatch(generate(s, c, lk))); }
    CHECK(att.size() == 6);
    // Full does not depend on the input values of what it generates (only on structure-relevant state)
    StateView s2 = s;
    for (int i = kSourceOffset + 4; i < kSourceOffset + 9; ++i) s2.value[static_cast<std::size_t>(i)] = static_cast<float>(paramTable()[static_cast<std::size_t>(i)].maxV);
    int compared = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        const Patch p1 = generate(s, cfgFull(seed), lk), p2 = generate(s2, cfgFull(seed), lk);
        for (const auto& d : p1.diffs) {
            if (d.specIndex < kSourceOffset + 4 || d.specIndex >= kSourceOffset + 9) continue;
            for (const auto& e : p2.diffs) if (e.specIndex == d.specIndex) { CHECK(e.value == d.value); ++compared; }
        }
    }
    CHECK(compared > 20);
}

// ---- sub-stream stability -------------------------------------------------------------------------------------------
void testSubstreams() {
    const StateView s = baseState(2);
    for (std::uint64_t seed : kSeeds) {
        for (Safety sf : {Safety::Safe, Safety::Normal, Safety::Wild}) {
            LockSet none, fmLocked;
            fmLocked.sections = kLockFm;
            fmLocked.params.set(static_cast<std::size_t>(kSourceOffset + 5));  // also pin one source param
            const auto a = applyPatch(s, generate(s, cfgFull(seed, sf), none));
            const auto b = applyPatch(s, generate(s, cfgFull(seed, sf), fmLocked));
            for (int i = kSourceOffset; i < kParamCount; ++i) {
                if (i == kSourceOffset + 5) continue;
                CHECK_MSG(a[static_cast<std::size_t>(i)] == b[static_cast<std::size_t>(i)], "source param %d changed because FM/another param was locked (seed %llu)", i, static_cast<unsigned long long>(seed));
            }
            // and the other way round: locking all of Source does not move the FM values
            LockSet srcLocked;
            srcLocked.sections = kLockSource;
            const auto c = applyPatch(s, generate(s, cfgFull(seed, sf), srcLocked));
            for (int i = kFmOffset; i < kSourceOffset; ++i)
                CHECK_MSG(a[static_cast<std::size_t>(i)] == c[static_cast<std::size_t>(i)], "fm param %d changed because Source was locked (seed %llu)", i, static_cast<unsigned long long>(seed));
            // module params of slot 0 do not move when slot 1 is locked (same module type on slot 0)
            LockSet slot1;
            slot1.sections = kLockSlot0 << 1;
            const auto d = applyPatch(s, generate(s, cfgFull(seed, sf), slot1));
            if (a[static_cast<std::size_t>(nodeParam(0, 0))] == d[static_cast<std::size_t>(nodeParam(0, 0))] && a[static_cast<std::size_t>(nodeParam(0, 1))] == d[static_cast<std::size_t>(nodeParam(0, 1))])
                for (int f = 5; f < 9; ++f)
                    CHECK_MSG(a[static_cast<std::size_t>(nodeParam(0, f))] == d[static_cast<std::size_t>(nodeParam(0, f))], "slot 0 param %d depends on slot 1 lock", f);
        }
    }
}

// ---- locks / never ----------------------------------------------------------------------------------------------------
void testLocksAndNever() {
    int lockedChecks = 0;
    for (int mode = 0; mode < 3; ++mode) {
        StateView s = baseState(mode);
        // make a stateful, non-default input: values scattered inside range
        Pcg32 rg(77, static_cast<std::uint64_t>(mode));
        for (int i = 0; i < kParamCount; ++i) {
            const auto& pi = paramTable()[static_cast<std::size_t>(i)];
            if (i == kGraphEnabled || i == kSourceEnabled || i == kSourceEngine || i == kFmEnabled) continue;
            s.value[static_cast<std::size_t>(i)] = static_cast<float>(pi.minV + rg.nextUnit() * (pi.maxV - pi.minV));
            if (pi.type != ParamType::Float) s.value[static_cast<std::size_t>(i)] = static_cast<float>(std::floor(s.value[static_cast<std::size_t>(i)] + 0.5));
        }
        for (std::uint64_t seed = 1; seed <= 150; ++seed) {
            LockSet lk;
            Pcg32 lg(seed, 99);
            for (int i = 0; i < kParamCount; ++i) if (lg.nextUnit() < 0.3) lk.params.set(static_cast<std::size_t>(i));
            if (seed % 5 == 0) lk.sections |= kLockFm | (kLockSlot0 << 3);
            if (seed % 7 == 0) lk.modRows = 0x00F3;
            for (Safety sf : {Safety::Safe, Safety::Wild}) {
                for (int m = 0; m < 2; ++m) {
                    const Config c = m == 0 ? cfgFull(seed, sf) : cfgMut(seed, 0.8f, sf);
                    const Patch p = generate(s, c, lk);
                    CHECK(diffsInRange(p));
                    for (const auto& d : p.diffs) {
                        CHECK_MSG(!lk.isLocked(d.specIndex), "locked param %d (%s) written", d.specIndex, paramTable()[static_cast<std::size_t>(d.specIndex)].id);
                        CHECK_MSG(paramTable()[static_cast<std::size_t>(d.specIndex)].cls != Classification::Never, "never param %d written", d.specIndex);
                    }
                    const auto after = applyPatch(s, p);
                    for (int i = 0; i < kParamCount; ++i)
                        if (lk.isLocked(i) || paramTable()[static_cast<std::size_t>(i)].cls == Classification::Never) { CHECK(after[static_cast<std::size_t>(i)] == s.value[static_cast<std::size_t>(i)]); ++lockedChecks; }
                }
            }
        }
    }
    CHECK(lockedChecks > 100000);
    // locking every parameter -> nothing to do
    const StateView s = baseState(2);
    LockSet all;
    all.params.set();
    CHECK(generate(s, cfgFull(1), all).diffs.empty());
    CHECK(generate(s, cfgMut(1, 1.f), all).diffs.empty());
    // everything locked except Never params is still "never written"
    // locked slots that already exceed the cap: the engine adds nothing and the result is still valid
    StateView t = baseState(0);
    for (int sl = 0; sl < kSlotCount; ++sl) { t.value[static_cast<std::size_t>(nodeParam(sl, 0))] = 11.f; t.value[static_cast<std::size_t>(nodeParam(sl, 1))] = 1.f; }
    LockSet slots;
    slots.sections = 0xFFFu;
    for (std::uint64_t seed = 1; seed <= 30; ++seed) {
        const Patch p = generate(t, cfgFull(seed, Safety::Safe), slots);
        CHECK(p.report.ok);
        for (const auto& d : p.diffs) CHECK(!(d.specIndex >= kNodeOffset && d.specIndex < kRouteOffset));
        CHECK(p.report.activeSlots == 12);
    }
    // graph disabled: Full yields ok=false/graph-disabled when only graph scopes are enabled; Mutate is empty
    StateView off = baseState(0);
    off.value[kGraphEnabled] = 0.f;
    Config c = cfgFull(3);
    c.scopeMask = ScopeChain | ScopeModuleParams | ScopeMotion;
    const Patch gp = generate(off, c, LockSet{});
    CHECK(!gp.report.ok && std::strcmp(gp.report.reason, "graph-disabled") == 0 && gp.diffs.empty());
    CHECK(generate(off, cfgMut(3, 1.f), LockSet{}).diffs.empty());
}

// ---- editions -----------------------------------------------------------------------------------------------------------
void testEditions() {
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
        for (Safety sf : {Safety::Safe, Safety::Normal, Safety::Wild}) {
            LockSet lk;
            // FX: no FM/Source parameter and no modulation target >= 85, in Full and Mutate
            {
                const StateView s = baseState(0);
                for (int m = 0; m < 2; ++m) {
                    const Patch p = generate(s, m == 0 ? cfgFull(seed, sf) : cfgMut(seed, 1.f, sf), lk);
                    for (const auto& d : p.diffs) CHECK_MSG(!isFmSrc(d.specIndex), "FX wrote FM/Source param %d", d.specIndex);
                    const auto after = applyPatch(s, p);
                    for (int r = 0; r < kModRowCount; ++r) CHECK(after[static_cast<std::size_t>(modParam(r, 1))] < 85.f);
                    CHECK(after[static_cast<std::size_t>(paramIndex("global.mode"))] == s.value[static_cast<std::size_t>(paramIndex("global.mode"))]);
                }
            }
            // Resynth with the source off: FM/Source still closed
            {
                StateView s = baseState(1);
                s.value[kSourceEnabled] = 0.f;
                const Patch p = generate(s, cfgFull(seed, sf), lk);
                for (const auto& d : p.diffs) CHECK(!isFmSrc(d.specIndex));
            }
            // Resynth with the source on: source stays on with engine > 0; FM may be touched only in the source/FM groups
            {
                const StateView s = baseState(1);
                const Patch p = generate(s, cfgFull(seed, sf), lk);
                const auto after = applyPatch(s, p);
                CHECK(after[kSourceEnabled] > 0.5f && after[kSourceEngine] >= 1.f);
                CHECK(!changed(s, p, kSourceEnabled) && !changed(s, p, kFmEnabled));
            }
            // Synth: at least one sound source, always
            {
                const StateView s = baseState(2);
                const Patch p = generate(s, cfgFull(seed, sf), lk);
                const auto after = applyPatch(s, p);
                CHECK_MSG(p.report.ok, "synth full not ok: %s", p.report.reason);
                CHECK_MSG(after[kFmEnabled] > 0.5f || (after[kSourceEnabled] > 0.5f && after[kSourceEngine] >= 1.f), "synth without sound source, seed %llu", static_cast<unsigned long long>(seed));
            }
        }
    // Synth with both sources locked off: ok=false / no-sound-source, empty patch
    StateView s = baseState(2);
    s.value[kFmEnabled] = 0.f; s.value[kSourceEnabled] = 0.f; s.value[kSourceEngine] = 0.f;
    LockSet lk;
    lk.params.set(kFmEnabled); lk.params.set(kSourceEnabled);
    const Patch p = generate(s, cfgFull(5), lk);
    CHECK(!p.report.ok && p.diffs.empty() && std::strcmp(p.report.reason, "no-sound-source") == 0);
    // Synth with only FM locked on: the source is generated freely, FM stays on
    s.value[kFmEnabled] = 1.f;
    const Patch q = generate(s, cfgFull(5), lk);
    CHECK(q.report.ok);
}

// ---- bounds / extremes -------------------------------------------------------------------------------------------------
void testBounds() {
    const float amounts[] = {0.f, 1e-9f, 0.001f, 0.5f, 0.999999f, 1.f, 2.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()};
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    for (int mode = 0; mode < 3; ++mode) {
        std::vector<StateView> inputs;
        StateView mn = baseState(mode), mx = mn, bad = mn, badInf = mn;
        for (int i = 0; i < kParamCount; ++i) {
            const auto& pi = paramTable()[static_cast<std::size_t>(i)];
            mn.value[static_cast<std::size_t>(i)] = static_cast<float>(pi.minV);
            mx.value[static_cast<std::size_t>(i)] = static_cast<float>(pi.maxV);
            bad.value[static_cast<std::size_t>(i)] = nan;
            badInf.value[static_cast<std::size_t>(i)] = (i & 1) ? inf : -inf;
        }
        inputs = {baseState(mode), mn, mx, bad, badInf};
        for (const auto& s : inputs) {
            for (std::uint64_t seed : kSeeds)
                for (Safety sf : {Safety::Safe, Safety::Normal, Safety::Wild})
                    for (float a : amounts) {
                        LockSet lk;
                        const Patch p = generate(s, cfgMut(seed, a, sf), lk);
                        CHECK(diffsInRange(p));
                        if (!(a > 0.f)) CHECK(p.diffs.empty());
                        const Patch f = generate(s, cfgFull(seed, sf), lk);
                        CHECK(diffsInRange(f));
                        if (&s == &inputs[0] || &s == &inputs[1] || &s == &inputs[2]) {
                            CHECK_MSG(validate(s, p, cfgMut(seed, a, sf), lk).ok, "mutate validate failed");
                            if (f.report.ok) CHECK_MSG(validate(s, f, cfgFull(seed, sf), lk).ok, "full validate failed: %s", validate(s, f, cfgFull(seed, sf), lk).reason);
                            else CHECK_MSG(std::strcmp(f.report.reason, "graph-disabled") == 0 && f.diffs.empty(), "unexpected failure %s", f.report.reason);
                        }
                        if (&s != &inputs[3] && &s != &inputs[4]) {
                            const auto after = applyPatch(s, p);
                            for (int i = 0; i < kParamCount; ++i) CHECK(inRange(i, after[static_cast<std::size_t>(i)]));
                        }
                    }
        }
    }
    // amount boundaries for p / r: amount=1 must still be valid and change something; amount tiny changes (almost) nothing
    const StateView s = baseState(2);
    LockSet lk;
    std::size_t big = 0, small = 0;
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        big += generate(s, cfgMut(seed, 1.f, Safety::Wild), lk).diffs.size();
        small += generate(s, cfgMut(seed, 0.05f, Safety::Wild), lk).diffs.size();
    }
    CHECK_MSG(big > small * 2, "mutate amount does not scale: amount 1 -> %zu diffs, amount 0.05 -> %zu", big, small);
    CHECK(big > 0);
    CHECK(generate(s, cfgMut(1, 0.f), lk).diffs.empty());
    CHECK(generate(s, cfgMut(1, 0.f), lk).report.ok);
}

// ---- Mutate semantics ----------------------------------------------------------------------------------------------------
void testMutate() {
    StateView s = baseState(2);
    LockSet lk;
    // Mutate never changes structure: node type/on, algorithm, fm.enabled, source.engine, modulation source/target
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
        for (Safety sf : {Safety::Safe, Safety::Wild}) {
            Config c = cfgMut(seed, 1.f, sf);
            const Patch p = generate(s, c, lk);
            for (const auto& d : p.diffs) {
                const auto& pi = paramTable()[static_cast<std::size_t>(d.specIndex)];
                CHECK_MSG(pi.cls == Classification::Random, "mutate wrote structural/never param %s", pi.id);
            }
        }
    // after a Full, small Mutates keep the values inside the same windows
    for (std::uint64_t seed = 1; seed <= 100; ++seed) {
        StateView t = baseState(2);
        t.value = applyPatch(t, generate(t, cfgFull(seed, Safety::Normal), lk));
        const Patch p = generate(t, cfgMut(seed + 1000, 0.3f, Safety::Normal), lk);
        CHECK(p.report.ok);
        CHECK(validate(t, p, cfgMut(seed + 1000, 0.3f, Safety::Normal), lk).ok);
    }
}

// ---- Wild / safe algorithm ------------------------------------------------------------------------------------------------
void testAlgorithms() {
    bool seen[33] = {};
    int fmSeen[32] = {};
    for (int mode : {0, 2}) {
        const StateView s = baseState(mode);
        LockSet lk;
        for (std::uint64_t seed = 1; seed <= 4000; ++seed) {
            const Patch w = generate(s, cfgFull(seed, Safety::Wild), lk);
            const auto aw = applyPatch(s, w);
            const int alg = static_cast<int>(std::lround(aw[kRoutingAlgorithm]));
            CHECK_MSG(alg != 32 && alg >= 0 && alg <= 31, "Wild produced routing algorithm %d", alg);
            seen[alg] = true;
            if (mode == 2) fmSeen[static_cast<int>(std::lround(aw[kFmAlgorithm])) & 31]++;
            CHECK(aw[kFmCustom] == 0.f);  // custom matrices are not generated
            const Patch sf = generate(s, cfgFull(seed, Safety::Safe), lk);
            const int alg2 = static_cast<int>(std::lround(applyPatch(s, sf)[kRoutingAlgorithm]));
            CHECK_MSG(routeAlgoInfo(alg2).feedbackArcs == 0, "Safe produced feedback algorithm %d", alg2);
        }
    }
    int distinct = 0;
    for (int a = 0; a < 32; ++a) distinct += seen[a];
    CHECK_MSG(distinct == 32 && !seen[32], "Wild covers %d of 32 algorithms", distinct);
    int fmDistinct = 0;
    for (int a = 0; a < 32; ++a) fmDistinct += fmSeen[a] > 0;
    CHECK_MSG(fmDistinct >= 30, "FM algorithm coverage %d", fmDistinct);
}

// ---- structure rules over many seeds -----------------------------------------------------------------------------------
void testStructure() {
    int checked = 0;
    for (int mode = 0; mode < 3; ++mode)
        for (Safety sf : {Safety::Safe, Safety::Normal, Safety::Wild}) {
            const StateView s = baseState(mode);
            LockSet lk;
            for (std::uint64_t seed = 1; seed <= 600; ++seed) {
                const Config c = cfgFull(seed * 7919, sf);
                const Patch p = generate(s, c, lk);
                CHECK_MSG(p.report.ok, "full not ok (%s)", p.report.reason);
                const auto rep = validate(s, p, c, lk);
                CHECK_MSG(rep.ok, "validate: %s (mode %d safety %d seed %llu)", rep.reason, mode, static_cast<int>(sf), static_cast<unsigned long long>(c.seed));
                CHECK(p.report.activeSlots >= 2 && p.report.activeSlots <= (sf == Safety::Safe ? 5 : 8));
                CHECK(p.report.modulationRows >= 1);
                const auto after = applyPatch(s, p);
                // frozen-knob safety: no infinite tail
                for (int sl = 0; sl < kSlotCount; ++sl) {
                    if (after[static_cast<std::size_t>(nodeParam(sl, 1))] < 0.5f) continue;
                    const int ty = static_cast<int>(std::lround(after[static_cast<std::size_t>(nodeParam(sl, 0))]));
                    if (ty != 6 && ty != 18) continue;
                    CHECK_MSG(sf != Safety::Safe, "Safe produced a freeze module");
                    if (sf == Safety::Wild) continue;  // Wild may use the freeze knob (infinite tail is the documented Wild risk)
                    const int knob = ty == 6 ? 3 : 2;
                    double sum = after[static_cast<std::size_t>(nodeParam(sl, 5 + knob))];
                    for (int r = 0; r < kModRowCount; ++r)
                        if (after[static_cast<std::size_t>(modParam(r, 0))] >= 0.5f && std::lround(after[static_cast<std::size_t>(modParam(r, 1))]) == 1 + sl * 7 + knob) sum += std::abs(after[static_cast<std::size_t>(modParam(r, 2))]);
                    CHECK_MSG(sum < 0.5, "freeze knob %g + depth >= 0.5", sum);
                }
                for (int r = 0; r < kModRowCount; ++r) {
                    const bool a = after[static_cast<std::size_t>(modParam(r, 0))] >= 0.5f, b = after[static_cast<std::size_t>(modParam(r, 1))] >= 0.5f;
                    CHECK_MSG(a == b, "half-empty row %d", r);
                    if (a) CHECK(std::abs(after[static_cast<std::size_t>(modParam(r, 2))]) >= 0.0199f);
                }
                ++checked;
            }
        }
    CHECK(checked == 3 * 3 * 600);
}

// ---- the validator itself must reject illegal graphs -------------------------------------------------------------------
void testValidatorRejects() {
    const StateView s = baseState(2);
    LockSet lk;
    const Config c = cfgFull(1, Safety::Safe);
    const Patch ok = generate(s, c, lk);
    CHECK(validate(s, ok, c, lk).ok);
    auto bad = [&](Patch p, const Config& cf, const LockSet& l) { return validate(s, p, cf, l).ok; };
    { Patch p = ok; p.diffs.push_back({paramIndex("global.mode"), 1.f}); CHECK(!bad(p, c, lk)); }  // Never (also non-ascending)
    { Patch p; p.diffs.push_back({paramIndex("global.mode"), 1.f}); CHECK(!bad(p, c, lk)); }
    { Patch p; p.diffs.push_back({kGraphTrim, -3.f}); CHECK(!bad(p, c, lk)); }
    { Patch p; p.diffs.push_back({kRoutingAlgorithm, 32.f}); CHECK(!bad(p, c, lk)); }
    { Patch p; p.diffs.push_back({kRoutingAlgorithm, 99.f}); CHECK(!bad(p, c, lk)); }
    { Patch p; p.diffs.push_back({kSourceOffset + 8, std::numeric_limits<float>::quiet_NaN()}); CHECK(!bad(p, c, lk)); }
    { Patch p; p.diffs.push_back({nodeParam(2, 1), 1.f}); LockSet l; l.sections = kLockSlot0 << 2; CHECK(!bad(p, c, l)); }
    { Patch p; p.diffs.push_back({nodeParam(2, 1), 1.f}); Config cf = c; cf.scopeMask = ScopeFM; CHECK(!bad(p, cf, lk)); }
    { Patch p; for (int sl = 0; sl < 8; ++sl) { p.diffs.push_back({nodeParam(sl, 0), 4.f}); p.diffs.push_back({nodeParam(sl, 1), 1.f}); } CHECK(!bad(p, c, lk)); }
    { Patch p; p.diffs.push_back({modParam(0, 0), 1.f}); CHECK(!bad(p, c, lk)); }   // half-empty row
    { Patch p; p.diffs.push_back({modParam(0, 0), 1.f}); p.diffs.push_back({modParam(0, 1), 5.f}); p.diffs.push_back({modParam(0, 2), 0.01f}); CHECK(!bad(p, c, lk)); }
    { Patch p; p.diffs.push_back({modParam(0, 0), 1.f}); p.diffs.push_back({modParam(0, 1), 5.f}); p.diffs.push_back({modParam(0, 2), 0.9f}); CHECK(!bad(p, c, lk)); }  // inactive target / over cap
    { StateView fx = baseState(0); Patch p; p.diffs.push_back({kFmOffset + 3, 1.f}); CHECK(!validate(fx, p, c, lk).ok); }
    { Patch p; p.diffs.push_back({kFmOffset + 1, 3.f}); p.diffs.push_back({kFmOffset + 3, 30.f}); CHECK(!bad(p, c, lk)); }
    { Patch p; CHECK(validate(s, p, cfgMut(1, 0.f), lk).ok); p.diffs.push_back({kFmOffset + 3, 1.f}); CHECK(!validate(s, p, cfgMut(1, 0.f), lk).ok); }
    // synth that ends without any sound source
    { Patch p; p.diffs.push_back({kFmEnabled, 0.f}); StateView t = s; t.value[kSourceEnabled] = 0.f; CHECK(!validate(t, p, c, lk).ok); }
}

// ---- no global state ------------------------------------------------------------------------------------------------------
void testNoGlobalState() {
    const StateView s2 = baseState(2), s0 = baseState(0);
    LockSet lk;
    const Patch a1 = generate(s2, cfgFull(11, Safety::Normal), lk);
    const Patch b1 = generate(s0, cfgMut(12, 0.6f, Safety::Wild), lk);
    // interleave in another order, with unrelated calls in between
    for (int k = 0; k < 20; ++k) (void)generate(baseState(1), cfgFull(1000 + static_cast<std::uint64_t>(k), Safety::Wild), lk);
    const Patch b2 = generate(s0, cfgMut(12, 0.6f, Safety::Wild), lk);
    const Patch a2 = generate(s2, cfgFull(11, Safety::Normal), lk);
    CHECK(samePatch(a1, a2) && samePatch(b1, b2));
    // concurrent calls give identical results
    std::vector<Patch> out(8);
    std::vector<std::thread> th;
    for (int t = 0; t < 8; ++t)
        th.emplace_back([&, t] { for (int k = 0; k < 20; ++k) out[static_cast<std::size_t>(t)] = generate(s2, cfgFull(11, Safety::Normal), lk); });
    for (auto& t : th) t.join();
    for (const auto& o : out) CHECK(samePatch(o, a1));
    // the input is not modified
    const StateView copy = s2;
    (void)generate(s2, cfgFull(3), lk);
    CHECK(std::memcmp(&copy.value, &s2.value, sizeof(copy.value)) == 0);
}

// ---- golden: bit-exactness across compilers (spec 6.1, kRandAlgoVersion = 1) ---------------------------------------------
std::uint64_t goldenHash() {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (int mode = 0; mode < 3; ++mode)
        for (Safety sf : {Safety::Safe, Safety::Normal, Safety::Wild})
            for (std::uint64_t seed : {1ULL, 2ULL, 99ULL, 0xFFFFFFFFFFULL}) {
                const StateView s = baseState(mode);
                LockSet lk;
                h = hashPatch(generate(s, cfgFull(seed, sf), lk), h);
                StateView t = s;
                t.value = applyPatch(s, generate(s, cfgFull(seed + 5, sf), lk));
                h = hashPatch(generate(t, cfgMut(seed, 0.4f, sf), lk), h);
                h = hashPatch(generate(t, cfgMut(seed, 1.0f, sf), lk), h);
            }
    return h;
}
void testGolden() {
    static_assert(kRandAlgoVersion == 1, "update the golden hash when the algorithm version changes");
    const std::uint64_t h = goldenHash();
    std::printf("golden hash (kRandAlgoVersion=%d): 0x%016llx\n", kRandAlgoVersion, static_cast<unsigned long long>(h));
    CHECK_MSG(h == kGolden, "golden hash changed: 0x%016llx", static_cast<unsigned long long>(h));
}

}  // namespace

int main() {
    testPrimitives();
    testTable();
    testDeterminism();
    testSubstreams();
    testLocksAndNever();
    testEditions();
    testBounds();
    testMutate();
    testAlgorithms();
    testStructure();
    testValidatorRejects();
    testNoGlobalState();
    testGolden();
    std::printf("aether_rand: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
