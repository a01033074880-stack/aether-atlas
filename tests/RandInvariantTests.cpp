// AETHER 2.3.0 segment B - engine-level invariant tests for aether_rand (JUCE-free). Tag: rand-230.
//
// Complements tests/RandTests.cpp (aether_rand) and tests/RandCoverageTests.cpp (aether_rand_coverage); it does not repeat them.
// Implements the engine-layer items of docs/randomization-test-plan.md (branch docs/rand-test-plan-230, 01ea237):
//   R02 per-parameter PCG32 sub-streams (known answers from the PUBLIC Pcg32/fnv1a64, lock-one-parameter independence)
//   R05 Never 47: id list == docs/randomization-spec-params.tsv, bit-identical under every seed / config / scope / safety / edition
//   R06 seven groups (Chain/ModuleParams/FM/Source/Motion/MixGlobal/System = 194/84/84/28/64/9/32): whole-group lock => bit-identical;
//       every lock-section bit and modRows bit == its parameter set
//   R08 EditionContext combinations incl. the real Synth pair (effectiveMode 2, isSynth true)
//   R11 FM / Source idle rules (ActiveWhen re-implemented here from spec 4.2/5.7, evaluated on the FINAL state)
//   R12 extreme configurations: bounded, finite, no Never write
//   R13 scopeMask / intoB / attempt
//   R16 allocation count of generate() has an upper bound (global operator new counter); applyPatch & friends allocate nothing
//   R18 "not triggered => state bit-identical" (amount <= 0, scopeMask 0, all locked, NaN / -0.0 inputs)
//
// Usage: AetherRandInvariants [section|all] [path/to/docs/randomization-spec-params.tsv]
//   sections: R02 R05 R06 R08 R11 R12 R13 R16 R18
// Every product-code property asserted here comes from the spec / header; where the spec is silent the test only records an INFO line.
#include "aether/Rand.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <set>
#include <string>
#include <vector>

using namespace aether::rand;

// ---- allocation counter (R16) -----------------------------------------------------------------------------------------
namespace {
std::atomic<std::uint64_t> g_allocCount{0};
std::atomic<bool> g_countAllocs{false};
}  // namespace
void* operator new(std::size_t n) {
    if (g_countAllocs.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    if (g_countAllocs.load(std::memory_order_relaxed)) g_allocCount.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return ::operator new(n, std::nothrow); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

// ---- tiny check framework ---------------------------------------------------------------------------------------------
namespace {
struct Tally { int checks = 0, failures = 0, shown = 0; } g;
void failAt(const char* file, int line, const char* expr, const char* msg) {
    ++g.failures;
    if (g.shown < 25) { ++g.shown; std::fprintf(stderr, "FAIL %s:%d  %s%s%s\n", file, line, expr, *msg ? " | " : "", msg); }
}
#define CHECK(cond) do { ++g.checks; if (!(cond)) failAt(__FILE__, __LINE__, #cond, ""); } while (0)
#define CHECKF(cond, ...) do { ++g.checks; if (!(cond)) { char b_[600]; std::snprintf(b_, sizeof b_, __VA_ARGS__); failAt(__FILE__, __LINE__, #cond, b_); } } while (0)
void info(const char* fmt, ...) {
    std::va_list ap; va_start(ap, fmt);
    std::printf("INFO ");
    std::vprintf(fmt, ap);
    std::putchar('\n');
    va_end(ap);
}

using Arr = std::array<float, kParamCount>;
constexpr const char* kGroupName[7] = {"Chain", "ModuleParams", "FM", "Source", "Motion", "MixGlobal", "System"};
constexpr int kGroupExpected[7] = {194, 84, 84, 28, 64, 9, 32};
constexpr std::uint64_t kPhi = 0x9E3779B97F4A7C15ULL;
constexpr Safety kSafeties[3] = {Safety::Safe, Safety::Normal, Safety::Wild};
constexpr std::uint64_t kSeedSet[] = {0, 1, 2, 3, 7, 42, 12345, 0xFFFFFFFFFFULL, 0x8000000000000000ULL, 0xFFFFFFFFFFFFFFFFULL};

const ParamInfo& P(int i) { return paramTable()[static_cast<std::size_t>(i)]; }
std::uint32_t bitsOf(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
bool bitEq(float a, float b) { return bitsOf(a) == bitsOf(b); }
bool sameArr(const Arr& a, const Arr& b) { return std::memcmp(a.data(), b.data(), sizeof(Arr)) == 0; }
std::uint32_t groupBit(Group gr) {  // the scope bit of a group (Rand.h ScopeBits); MixGlobal and System have none
    switch (gr) {
        case Group::Chain: return ScopeChain;
        case Group::ModuleParams: return ScopeModuleParams;
        case Group::FM: return ScopeFM;
        case Group::Source: return ScopeSource;
        case Group::Motion: return ScopeMotion;
        default: return 0;
    }
}
bool isGate(int i) { return i == kGraphEnabled || i == kSourceEnabled || i == kSourceEngine || i == kFmEnabled; }

struct Ed { const char* name; EditionContext e; };
const Ed kEds[] = {{"FX(0,false)", {0, false}}, {"Resynth(1,false)", {1, false}}, {"Synth(2,true)", {2, true}},
                   {"SynthLegacy(0,true)", {0, true}}, {"FxStored2(2,false)", {2, false}}};
int modeOf(const EditionContext& e) { return e.isSynth ? 2 : (e.effectiveMode == 1 ? 1 : 0); }  // spec 4.1: the effective mode decides

StateView defaults(const EditionContext& e) {
    StateView s;
    for (int i = 0; i < kParamCount; ++i) s.value[static_cast<std::size_t>(i)] = static_cast<float>(P(i).defaultV);
    s.edition = e;
    s.value[kGraphEnabled] = 1.f;
    if (modeOf(e) >= 1) { s.value[kSourceEnabled] = 1.f; s.value[kSourceEngine] = 1.f; }
    if (modeOf(e) == 2) s.value[kFmEnabled] = 1.f;
    return s;
}
StateView scattered(const EditionContext& e, std::uint64_t seed) {
    StateView s = defaults(e);
    Pcg32 rg(seed, 0x5CA77E);
    for (int i = 0; i < kParamCount; ++i) {
        if (isGate(i)) continue;
        const auto& pi = P(i);
        double v = pi.minV + rg.nextUnit() * (pi.maxV - pi.minV);
        if (pi.type != ParamType::Float) v = std::floor(v + 0.5);
        s.value[static_cast<std::size_t>(i)] = static_cast<float>(std::clamp(v, pi.minV, pi.maxV));
    }
    return s;
}
Config cFull(std::uint64_t seed, Safety sf = Safety::Safe, std::uint32_t scope = ScopeAll) {
    Config c; c.seed = seed; c.mode = Mode::Full; c.safety = sf; c.scopeMask = scope; return c;
}
Config cMut(std::uint64_t seed, float a, Safety sf = Safety::Safe, std::uint32_t scope = ScopeAll) {
    Config c; c.seed = seed; c.mode = Mode::Mutate; c.amount = a; c.safety = sf; c.scopeMask = scope; return c;
}
bool samePatch(const Patch& a, const Patch& b) {
    if (a.diffs.size() != b.diffs.size() || a.report.ok != b.report.ok || std::strcmp(a.report.reason, b.report.reason) != 0) return false;
    for (std::size_t i = 0; i < a.diffs.size(); ++i)
        if (a.diffs[i].specIndex != b.diffs[i].specIndex || !bitEq(a.diffs[i].value, b.diffs[i].value)) return false;
    return true;
}
LockSet lockGroup(Group gr) {
    LockSet l;
    for (int i = 0; i < kParamCount; ++i) if (P(i).group == gr) l.params.set(static_cast<std::size_t>(i));
    return l;
}
bool inRangeF(int i, float v) { return std::isfinite(v) && v >= static_cast<float>(P(i).minV) && v <= static_cast<float>(P(i).maxV); }

// Common per-patch sanity used by several sections.
void checkPatchBasics(const Patch& p, const char* ctx) {
    int prev = -1;
    for (const auto& d : p.diffs) {
        CHECKF(d.specIndex > prev && d.specIndex < kParamCount, "%s: diffs not strictly ascending / out of range (%d after %d)", ctx, d.specIndex, prev);
        prev = d.specIndex;
        if (d.specIndex < 0 || d.specIndex >= kParamCount) continue;
        CHECKF(inRangeF(d.specIndex, d.value), "%s: %s value %g outside [%g,%g] or non-finite", ctx, P(d.specIndex).id, static_cast<double>(d.value), P(d.specIndex).minV, P(d.specIndex).maxV);
        CHECKF(P(d.specIndex).cls != Classification::Never, "%s: Never parameter %s written", ctx, P(d.specIndex).id);
        CHECKF(d.specIndex != kGraphTrim, "%s: graph.trim written by the engine (frozen #4)", ctx);
        if (P(d.specIndex).type != ParamType::Float) CHECKF(d.value == std::floor(d.value), "%s: %s discrete value %g not integral", ctx, P(d.specIndex).id, static_cast<double>(d.value));
    }
    CHECKF(p.report.reason != nullptr, "%s: report.reason null", ctx);
    if (!p.report.ok) CHECKF(p.diffs.empty(), "%s: not-ok patch (%s) carries %zu diffs", ctx, p.report.reason ? p.report.reason : "?", p.diffs.size());
}
}  // namespace

// =====================================================================================================================
// R02: per-parameter PCG32 sub-streams
// =====================================================================================================================
namespace {
float snapLikeSpec(int i, double v) {  // values live on the parameter grid (unit values)
    const auto& pi = P(i);
    v = std::clamp(v, pi.minV, pi.maxV);
    if (pi.type == ParamType::Bool) v = v >= 0.5 ? 1.0 : 0.0;
    else if (pi.type == ParamType::Choice) v = std::clamp(std::floor(v + 0.5), 0.0, static_cast<double>(pi.numChoices - 1));
    else if (pi.step > 0.0) v = pi.minV + std::floor((v - pi.minV) / pi.step + 0.5) * pi.step;
    return std::clamp(static_cast<float>(v), static_cast<float>(pi.minV), static_cast<float>(pi.maxV));
}

void secR02() {
    // (a) the 495 stream ids are pairwise distinct and equal fnv1a64(id)
    std::set<std::uint64_t> hs;
    for (int i = 0; i < kParamCount; ++i) { CHECK(P(i).hash == fnv1a64(P(i).id)); hs.insert(P(i).hash); }
    CHECK(hs.size() == static_cast<std::size_t>(kParamCount));
    CHECK(hs.count(fnv1a64("@structure")) == 0 && hs.count(fnv1a64("@modulation")) == 0);

    // (b) known answers from the PUBLIC primitives (spec 6.1): value = window sample of u = Pcg32(attemptSeed, fnv1a64(id)).nextUnit()
    //     Source group, engine locked so pg.* (engine 1) / chaos.* (engine 2) are active.  Full mode, scope Source only.
    int exact = 0, checked = 0;
    for (int engine = 1; engine <= 2; ++engine)
        for (int si = 0; si < 3; ++si)
            for (std::uint64_t seed : {1ULL, 2ULL, 99ULL, 0xFFFFFFFFFFULL}) {
                for (int attempt : {0, 3}) {
                    StateView s = defaults({1, false});
                    s.value[kSourceEngine] = static_cast<float>(engine);
                    LockSet lk;
                    lk.params.set(kSourceEngine); lk.params.set(kSourceEnabled);
                    Config c = cFull(seed, kSafeties[si], ScopeSource);
                    c.attempt = attempt;
                    const Patch p = generate(s, c, lk);
                    CHECK(p.report.ok);
                    const Arr after = applyPatch(s, p);
                    const std::uint64_t aseed = seed ^ (static_cast<std::uint64_t>(attempt) * kPhi);
                    for (int i = kSourceOffset; i < kParamCount; ++i) {
                        const auto& pi = P(i);
                        if (pi.cls != Classification::Random) continue;
                        const bool act = (pi.active == ActiveWhen::SrcOn) || (pi.active == ActiveWhen::SrcEngine1 && engine == 1) || (pi.active == ActiveWhen::SrcEngine2 && engine == 2);
                        if (!act) { CHECKF(bitEq(after[static_cast<std::size_t>(i)], s.value[static_cast<std::size_t>(i)]), "idle source param %s changed (engine %d)", pi.id, engine); continue; }
                        Pcg32 gen(aseed, fnv1a64(pi.id));
                        double u[4];
                        for (double& x : u) x = gen.nextUnit();
                        const double lo = pi.win[si].lo, hi = pi.win[si].hi, span = hi - lo;
                        double v;
                        switch (pi.dist) {
                            case Dist::Uniform: v = lo + span * u[0]; break;
                            case Dist::Log: { const double l = log2Det(lo); v = (lo > 0.0 && hi > lo) ? exp2Det(l + u[0] * (log2Det(hi) - l)) : lo + span * u[0]; break; }
                            case Dist::Kumaraswamy: {
                                const double inner = 1.0 - powDet(1.0 - u[0], 1.0 / pi.kb);
                                v = lo + span * (pi.ka == 1.0 ? inner : powDet(inner, 1.0 / pi.ka));
                                break;
                            }
                            case Dist::Centered: {
                                const double center = std::clamp(pi.defaultV, lo, hi), t = u[1] - u[2];
                                v = t >= 0.0 ? center + t * (hi - center) : center + t * (center - lo);
                                break;
                            }
                            case Dist::Choice: { const double l2 = std::ceil(lo), h2 = std::floor(hi), n = h2 - l2 + 1.0; v = n < 1.0 ? l2 : l2 + std::min(std::floor(u[0] * n), n - 1.0); break; }
                            default: continue;
                        }
                        ++checked;
                        const float want = snapLikeSpec(i, v);
                        const bool ok = bitEq(want, after[static_cast<std::size_t>(i)]);
                        exact += ok;
                        CHECKF(ok, "%s: engine value %.9g != known-answer %.9g (engine %d safety %d seed %llu attempt %d)", pi.id, static_cast<double>(after[static_cast<std::size_t>(i)]), static_cast<double>(want), engine, si, static_cast<unsigned long long>(seed), attempt);
                    }
                }
            }
    info("R02 known-answer values compared: %d (exact %d)", checked, exact);
    CHECK(checked > 500);

    // (c) lock-one-parameter independence (spec 6.1: "locking other parameters does not change the rest")
    //     pool: Random value parameters of ModuleParams / FM / Source (+ graph.feedback).  Documented exception: locking fm.morph
    //     legitimately moves fm.index and the fm.op.*.ratio sets (they read the morph neighbour, spec 5.4).
    std::vector<int> pool;
    for (int i = 0; i < kParamCount; ++i)
        if (P(i).cls == Classification::Random && (P(i).group == Group::ModuleParams || P(i).group == Group::FM || P(i).group == Group::Source || i == kGraphFeedback)) pool.push_back(i);
    CHECKF(pool.size() >= 140, "pool size %zu", pool.size());
    auto dependsOnMorph = [](int q) { return q == kFmIndex || (q >= kOpOffset && q < kEdgeOffset && (q - kOpOffset) % 6 == 0); };
    long compared = 0;
    for (const Ed& ed : {kEds[2], kEds[1]})
        for (std::uint64_t seed = 1; seed <= 6; ++seed)
            for (Safety sf : kSafeties)
                for (int m = 0; m < 2; ++m) {
                    const StateView s = scattered(ed.e, seed * 31);
                    const Config c = m == 0 ? cFull(seed, sf) : cMut(seed, 0.6f, sf);
                    const Arr base = applyPatch(s, generate(s, c, LockSet{}));
                    for (int pIdx : pool) {
                        LockSet lk;
                        lk.params.set(static_cast<std::size_t>(pIdx));
                        const Arr locked = applyPatch(s, generate(s, c, lk));
                        CHECKF(bitEq(locked[static_cast<std::size_t>(pIdx)], s.value[static_cast<std::size_t>(pIdx)]), "locked %s moved", P(pIdx).id);
                        for (int q : pool) {
                            if (q == pIdx || (pIdx == kFmMorph && dependsOnMorph(q))) continue;
                            ++compared;
                            CHECKF(bitEq(base[static_cast<std::size_t>(q)], locked[static_cast<std::size_t>(q)]),
                                   "%s changed when only %s was locked (%s seed %llu safety %d %s)", P(q).id, P(pIdx).id, ed.name, static_cast<unsigned long long>(seed), static_cast<int>(sf), m == 0 ? "Full" : "Mutate");
                        }
                    }
                }
    info("R02 lock-one independence comparisons: %ld", compared);

    // (d) the structure stream does not depend on FM / Source / modulation locks: the chain (types / on / algorithm) is unchanged
    for (std::uint64_t seed = 1; seed <= 60; ++seed)
        for (Safety sf : kSafeties) {
            const StateView s = defaults({2, true});
            LockSet lk;
            lk.sections = kLockFm | kLockSource | kLockMod;
            const Arr a = applyPatch(s, generate(s, cFull(seed, sf), LockSet{}));
            const Arr b = applyPatch(s, generate(s, cFull(seed, sf), lk));
            for (int sl = 0; sl < kSlotCount; ++sl)
                for (int f = 0; f < 2; ++f) CHECKF(bitEq(a[static_cast<std::size_t>(nodeParam(sl, f))], b[static_cast<std::size_t>(nodeParam(sl, f))]), "slot %d field %d depends on FM/Source/Mod locks (seed %llu)", sl, f, static_cast<unsigned long long>(seed));
            CHECK(bitEq(a[kRoutingAlgorithm], b[kRoutingAlgorithm]));
        }
}
}  // namespace

// =====================================================================================================================
// R05: Never 47
// =====================================================================================================================
namespace {
std::vector<std::string> splitTab(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == '\t') { out.push_back(cur); cur.clear(); } else if (c != '\r') cur.push_back(c); }
    out.push_back(cur);
    return out;
}

void secR05(const char* tsv) {
    // (a) the Never list in the table equals the Never rows of the spec TSV (not a second copy in code)
    std::set<std::string> tsvNever, tableNever;
    for (int i = 0; i < kParamCount; ++i) if (P(i).cls == Classification::Never) tableNever.insert(P(i).id);
    if (!tsv || !*tsv) { CHECKF(false, "R05 needs the path of docs/randomization-spec-params.tsv as argv[2]"); }
    else {
        std::ifstream in(tsv, std::ios::binary);
        CHECKF(static_cast<bool>(in), "cannot open %s", tsv);
        std::string line;
        std::getline(in, line);
        const auto hdr = splitTab(line);
        CHECK(hdr.size() > 3 && hdr[0] == "id" && hdr[3] == "classification");
        int rows = 0;
        while (std::getline(in, line)) {
            if (line.empty() || line == "\r") continue;
            const auto c = splitTab(line);
            if (c.size() < 4) continue;
            ++rows;
            if (c[3] == "Never") tsvNever.insert(c[0]);
        }
        CHECK(rows == kParamCount);
        CHECKF(tsvNever.size() == 47, "TSV Never rows: %zu", tsvNever.size());
        CHECK(tsvNever == tableNever);
    }
    CHECK(tableNever.size() == 47);
    for (const char* id : {"global.mode", "graph.enabled", "lfo.1.sync", "lfo.4.sync", "source.antialias", "source.seed", "source.transpose", "global.bypass", "master.ceiling"})
        CHECKF(tableNever.count(id) == 1, "%s not in the Never list", id);

    // (b) matrix: edition x input variant x seed x safety x {Full, Mutate .3/.8/1} x scope x intoB
    std::vector<int> never;
    for (int i = 0; i < kParamCount; ++i) if (P(i).cls == Classification::Never) never.push_back(i);
    const std::uint32_t scopes[] = {ScopeAll, 0u, ScopeChain, ScopeModuleParams, ScopeFM, ScopeSource, ScopeMotion, 0xFFFFFFFFu, 0b10101u};
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    long runs = 0, diffTotal = 0;
    for (const Ed& ed : kEds)
        for (int variant = 0; variant < 4; ++variant)
            for (std::uint64_t seed = 1; seed <= 12; ++seed) {
                StateView s = variant == 0 ? defaults(ed.e) : scattered(ed.e, seed);
                if (variant == 2) for (std::size_t k = 0; k < never.size(); ++k) s.value[static_cast<std::size_t>(never[k])] = (k % 3 == 0) ? nan : ((k % 3 == 1) ? -0.0f : 0.0f);
                if (variant == 3) for (std::size_t k = 0; k < never.size(); ++k) s.value[static_cast<std::size_t>(never[k])] = (k & 1) ? inf : -inf;
                for (Safety sf : kSafeties)
                    for (int m = 0; m < 4; ++m)
                        for (std::uint32_t scope : scopes)
                            for (int intoB = 0; intoB < 2; ++intoB) {
                                Config c = m == 0 ? cFull(seed * 7919, sf, scope) : cMut(seed * 7919, m == 1 ? 0.3f : (m == 2 ? 0.8f : 1.f), sf, scope);
                                c.intoB = intoB != 0;
                                const Patch p = generate(s, c, LockSet{});
                                ++runs; diffTotal += static_cast<long>(p.diffs.size());
                                for (const auto& d : p.diffs) CHECKF(P(d.specIndex).cls != Classification::Never, "Never %s in diffs", P(d.specIndex).id);
                                const Arr after = applyPatch(s, p);
                                for (int i : never) CHECKF(bitEq(after[static_cast<std::size_t>(i)], s.value[static_cast<std::size_t>(i)]), "Never %s changed (%s variant %d seed %llu)", P(i).id, ed.name, variant, static_cast<unsigned long long>(seed));
                            }
            }
    info("R05 matrix runs: %ld, total diffs seen: %ld", runs, diffTotal);
    CHECK(diffTotal > 100000);
}
}  // namespace

// =====================================================================================================================
// R06: seven groups + lock sections
// =====================================================================================================================
namespace {
void secR06() {
    int cnt[7] = {};
    for (int i = 0; i < kParamCount; ++i) ++cnt[static_cast<int>(P(i).group)];
    int sum = 0;
    for (int gi = 0; gi < 7; ++gi) { CHECKF(cnt[gi] == kGroupExpected[gi], "group %s has %d params, expected %d", kGroupName[gi], cnt[gi], kGroupExpected[gi]); sum += cnt[gi]; }
    CHECK(sum == kParamCount);

    // (a) whole-group lock => bit-identical + not in diffs; the unlocked control proves the group is normally touched
    long touched[7] = {}, okRuns[7] = {}, allRuns[7] = {};
    for (int gi = 0; gi < 7; ++gi) {
        const LockSet lk = lockGroup(static_cast<Group>(gi));
        CHECK(lk.params.count() == static_cast<std::size_t>(kGroupExpected[gi]));
        for (const Ed& ed : kEds)
            for (Safety sf : kSafeties)
                for (int m = 0; m < 3; ++m)
                    for (std::uint64_t seed = 1; seed <= 25; ++seed) {
                        const StateView s = (seed & 1) ? defaults(ed.e) : scattered(ed.e, seed);
                        const Config c = m == 0 ? cFull(seed, sf) : cMut(seed, m == 1 ? 0.6f : 1.f, sf);
                        const Patch p = generate(s, c, lk);
                        checkPatchBasics(p, ed.name);
                        ++allRuns[gi]; okRuns[gi] += p.report.ok;  // generate() self-validates and clears diffs on a violation: count accepted patches too
                        for (const auto& d : p.diffs) CHECKF(P(d.specIndex).group != static_cast<Group>(gi), "locked group %s: %s written", kGroupName[gi], P(d.specIndex).id);
                        const Arr after = applyPatch(s, p);
                        for (int i = 0; i < kParamCount; ++i)
                            if (P(i).group == static_cast<Group>(gi)) CHECKF(bitEq(after[static_cast<std::size_t>(i)], s.value[static_cast<std::size_t>(i)]), "locked group %s: %s changed (%s seed %llu)", kGroupName[gi], P(i).id, ed.name, static_cast<unsigned long long>(seed));
                        const Patch q = generate(s, c, LockSet{});
                        for (const auto& d : q.diffs) if (P(d.specIndex).group == static_cast<Group>(gi)) { ++touched[gi]; break; }
                    }
    }
    for (int gi = 0; gi < 7; ++gi) info("R06 control: group %-12s written in %ld unlocked runs; with the group locked %ld of %ld patches accepted", kGroupName[gi], touched[gi], okRuns[gi], allRuns[gi]);
    // a whole-group lock must not turn generation into a rejection (a rejected patch has no diffs and would make the checks above vacuous)
    for (int gi = 0; gi < 7; ++gi) CHECKF(okRuns[gi] * 100 >= allRuns[gi] * 90, "locking group %s: only %ld of %ld patches accepted", kGroupName[gi], okRuns[gi], allRuns[gi]);
    for (int gi = 0; gi < 5; ++gi) CHECKF(touched[gi] > 100, "group %s is (almost) never written even unlocked: lock test would be vacuous (%ld)", kGroupName[gi], touched[gi]);
    // MixGlobal = 8 Never + graph.trim (never written by P1, frozen #4); System = 32 Never: never written, locked or not
    CHECK(touched[5] == 0 && touched[6] == 0);

    // (b) every lock-section bit and modRows bit locks exactly its parameter set
    struct Case { std::string name; LockSet lk; std::bitset<kParamCount> expect; };
    std::vector<Case> cases;
    auto add = [&](std::string name, std::uint32_t sections, std::uint16_t modRows, std::bitset<kParamCount> expect) {
        Case c; c.name = std::move(name); c.lk.sections = sections; c.lk.modRows = modRows; c.expect = expect; cases.push_back(std::move(c));
    };
    auto range = [](int lo, int hi) { std::bitset<kParamCount> b; for (int i = lo; i < hi; ++i) b.set(static_cast<std::size_t>(i)); return b; };
    for (int sl = 0; sl < kSlotCount; ++sl) add("slot" + std::to_string(sl), kLockSlot0 << sl, 0, range(nodeParam(sl, 0), nodeParam(sl, 0) + 11));
    add("fm", kLockFm, 0, range(kFmOffset, kSourceOffset));
    add("source", kLockSource, 0, range(kSourceOffset, kParamCount));
    for (int l = 0; l < 4; ++l) add("lfo" + std::to_string(l), kLockLfo0 << l, 0, range(lfoParam(l, 0), lfoParam(l, 0) + 4));
    add("mod", kLockMod, 0, range(kModOffset, kFmOffset));
    for (int r = 0; r < kModRowCount; ++r) add("modRow" + std::to_string(r), 0, static_cast<std::uint16_t>(1u << r), range(modParam(r, 0), modParam(r, 0) + 3));
    add("modRows=0xFFFF", 0, 0xFFFF, range(kModOffset, kFmOffset));
    add("junk-high-section-bits", 0xFFF80000u, 0, {});
    add("none", 0, 0, {});
    for (const auto& cs : cases) {
        for (int i = 0; i < kParamCount; ++i) CHECKF(cs.lk.isLocked(i) == cs.expect.test(static_cast<std::size_t>(i)), "%s: isLocked(%d %s)=%d expected %d", cs.name.c_str(), i, P(i).id, cs.lk.isLocked(i), static_cast<int>(cs.expect.test(static_cast<std::size_t>(i))));
        CHECK(!cs.lk.isLocked(-1) && !cs.lk.isLocked(kParamCount) && !cs.lk.isLocked(INT_MAX) && !cs.lk.isLocked(INT_MIN));
        LockSet byParams; byParams.params = cs.expect;
        if (cs.expect.none()) continue;
        for (const Ed& ed : {kEds[0], kEds[1], kEds[2]})
            for (Safety sf : kSafeties)
                for (int m = 0; m < 2; ++m)
                    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
                        const StateView s = (seed & 1) ? defaults(ed.e) : scattered(ed.e, seed);
                        const Config c = m == 0 ? cFull(seed, sf) : cMut(seed, 0.8f, sf);
                        const Patch p = generate(s, c, cs.lk);
                        const Arr after = applyPatch(s, p);
                        for (int i = 0; i < kParamCount; ++i) if (cs.expect.test(static_cast<std::size_t>(i))) CHECKF(bitEq(after[static_cast<std::size_t>(i)], s.value[static_cast<std::size_t>(i)]), "%s: %s changed", cs.name.c_str(), P(i).id);
                        for (const auto& d : p.diffs) CHECKF(!cs.expect.test(static_cast<std::size_t>(d.specIndex)), "%s: %s in diffs", cs.name.c_str(), P(d.specIndex).id);
                        // a section lock and the equivalent per-parameter lock produce the same patch
                        CHECKF(samePatch(p, generate(s, c, byParams)), "%s: section lock != per-parameter lock (%s seed %llu)", cs.name.c_str(), ed.name, static_cast<unsigned long long>(seed));
                    }
    }
    // sections + rows + single parameters combine as a union
    {
        LockSet u; u.sections = kLockFm | (kLockSlot0 << 2); u.modRows = 0x0005; u.params.set(kSourceOffset + 6);
        for (std::uint64_t seed = 1; seed <= 30; ++seed) {
            const StateView s = scattered({2, true}, seed);
            const Arr after = applyPatch(s, generate(s, cFull(seed, Safety::Wild), u));
            for (int i = 0; i < kParamCount; ++i) if (u.isLocked(i)) CHECK(bitEq(after[static_cast<std::size_t>(i)], s.value[static_cast<std::size_t>(i)]));
        }
    }
}
}  // namespace

// =====================================================================================================================
// R08: edition combinations
// =====================================================================================================================
namespace {
bool soundSource(const Arr& f) { return f[kFmEnabled] > 0.5f || (f[kSourceEnabled] > 0.5f && f[kSourceEngine] >= 0.5f); }

void secR08() {
    // (a) equivalence classes of EditionContext (spec 4.1: the effective mode decides; isSynth => 2)
    for (std::uint64_t seed = 1; seed <= 60; ++seed)
        for (Safety sf : kSafeties)
            for (int m = 0; m < 2; ++m) {
                const Config c = m == 0 ? cFull(seed, sf) : cMut(seed, 0.7f, sf);
                const StateView base = scattered({0, false}, seed);
                auto run = [&](EditionContext e) { StateView s = base; s.edition = e; return generate(s, c, LockSet{}); };
                const Patch synth2 = run({2, true});
                for (int eff : {0, 1, 2, -1, 3, 9}) CHECKF(samePatch(synth2, run({eff, true})), "isSynth=true: effectiveMode %d changes the patch (seed %llu)", eff, static_cast<unsigned long long>(seed));
                const Patch fx = run({0, false});
                CHECKF(samePatch(fx, run({2, false})), "(effectiveMode 2,isSynth false) differs from FX (spec 4.1 last bullet) seed %llu", static_cast<unsigned long long>(seed));
                for (int eff : {-1, 3, 9, INT_MAX, INT_MIN}) { const Patch p = run({eff, false}); checkPatchBasics(p, "out-of-range effectiveMode"); }
            }

    // (b) Synth (2,true): always at least one sound source, FM and Source open, extended modulation targets usable
    long wideTargets = 0, fmDiffs = 0, srcDiffs = 0;
    for (std::uint64_t seed = 1; seed <= 400; ++seed)
        for (Safety sf : kSafeties) {
            const StateView s = defaults({2, true});
            const Patch p = generate(s, cFull(seed, sf), LockSet{});
            checkPatchBasics(p, "Synth(2,true) full");
            CHECKF(p.report.ok, "Synth full not ok: %s", p.report.reason);
            const Arr after = applyPatch(s, p);
            CHECK(soundSource(after));
            for (const auto& d : p.diffs) { fmDiffs += (d.specIndex >= kFmOffset && d.specIndex < kSourceOffset); srcDiffs += (d.specIndex >= kSourceOffset); }
            for (int r = 0; r < kModRowCount; ++r) wideTargets += after[static_cast<std::size_t>(modParam(r, 1))] >= 85.f;
            const StateView t = scattered({2, true}, seed);
            const Patch q = generate(t, cMut(seed, 0.8f, sf), LockSet{});
            checkPatchBasics(q, "Synth(2,true) mutate");
            CHECK(soundSource(applyPatch(t, q)));
        }
    info("R08 Synth(2,true): FM diffs %ld, Source diffs %ld, rows with target>=85: %ld", fmDiffs, srcDiffs, wideTargets);
    CHECK(fmDiffs > 0 && srcDiffs > 0 && wideTargets > 0);

    // (c) FX (0,false) with stale FM/Source state in the input: nothing in FM/Source is written, no new row targets 85..112
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
        for (Safety sf : kSafeties)
            for (int m = 0; m < 2; ++m) {
                StateView s = scattered({0, false}, seed);
                s.value[kFmEnabled] = 1.f; s.value[kSourceEnabled] = 1.f; s.value[kSourceEngine] = 1.f + static_cast<float>(seed & 1);
                const Patch p = generate(s, m == 0 ? cFull(seed, sf) : cMut(seed, 1.f, sf), LockSet{});
                for (const auto& d : p.diffs) {
                    CHECKF(d.specIndex < kFmOffset, "FX wrote %s", P(d.specIndex).id);
                    if (d.specIndex >= kModOffset && d.specIndex < kFmOffset && (d.specIndex - kModOffset) % 3 == 1) CHECKF(d.value < 85.f, "FX wrote modulation target %g", static_cast<double>(d.value));
                }
            }

    // (d) Resynth (1,false): FM/Source open only while the source is sounding; source.enabled / fm.enabled never written there
    long openDiffs = 0;
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
        for (Safety sf : kSafeties)
            for (int srcOn = 0; srcOn < 2; ++srcOn) {
                StateView s = defaults({1, false});
                s.value[kSourceEnabled] = static_cast<float>(srcOn);
                s.value[kSourceEngine] = srcOn ? 1.f + static_cast<float>(seed & 1) : 0.f;
                const Patch p = generate(s, cFull(seed, sf), LockSet{});
                for (const auto& d : p.diffs) CHECKF(d.specIndex != kSourceEnabled && d.specIndex != kFmEnabled, "Resynth wrote gate %s", P(d.specIndex).id);
                for (const auto& d : p.diffs) if (d.specIndex >= kFmOffset) { CHECKF(srcOn == 1, "Resynth with the source off wrote %s", P(d.specIndex).id); ++openDiffs; }
                if (srcOn) CHECK(applyPatch(s, p)[kSourceEngine] >= 1.f);
            }
    CHECK(openDiffs > 0);

    // (e) Synth with FM locked off: the source supplies the sound, FM parameters stay untouched
    for (std::uint64_t seed = 1; seed <= 200; ++seed)
        for (Safety sf : kSafeties) {
            StateView s = defaults({2, true});
            LockSet fmOffLocked; s.value[kFmEnabled] = 0.f; fmOffLocked.params.set(kFmEnabled);
            const Patch p = generate(s, cFull(seed, sf), fmOffLocked);
            const Arr after = applyPatch(s, p);
            CHECK(after[kFmEnabled] == 0.f && soundSource(after) && p.report.ok);
            for (const auto& d : p.diffs) CHECKF(!(d.specIndex >= kFmOffset && d.specIndex < kSourceOffset), "FM locked off but %s written", P(d.specIndex).id);
        }
}
}  // namespace

// =====================================================================================================================
// R11: idle rules, ActiveWhen re-implemented from spec 4.2 / 5.7 (evaluated on the FINAL state)
// =====================================================================================================================
namespace {
int rint2(float v) { return static_cast<int>(std::lround(v)); }
bool isFreezeTypeT(int t) { return t == 6 || t == 18; }

struct Final {
    Arr f; int mode;
    bool graph() const { return f[kGraphEnabled] > 0.5f; }
    int alg() const { return std::clamp(rint2(f[kRoutingAlgorithm]), 0, 32); }
    bool fmOn() const { return f[kFmEnabled] > 0.5f; }
    bool srcOn() const { return f[kSourceEnabled] > 0.5f && f[kSourceEngine] >= 0.5f; }
    int engine() const { return rint2(f[kSourceEngine]); }
    int type(int sl) const { return std::clamp(rint2(f[static_cast<std::size_t>(nodeParam(sl, 0))]), 0, kModuleTypeCount - 1); }
    bool slotActive(int sl) const { return f[static_cast<std::size_t>(nodeParam(sl, 1))] > 0.5f && type(sl) != 0; }
    bool rowLive(int r) const { return f[static_cast<std::size_t>(modParam(r, 0))] >= 0.5f && f[static_cast<std::size_t>(modParam(r, 1))] >= 0.5f; }
    bool closedFmSrc() const { return mode == 0 || (mode == 1 && !srcOn()); }  // spec 4.2: FM/Source parameters are closed
    // spec 5.7 target validity
    bool targetValid(int t) const {
        if (t < 1 || t > 112) return false;
        if (!graph()) return false;
        if (t <= 84) return slotActive((t - 1) / 7);
        if (mode == 0) return false;
        if (t <= 101) {
            if (!srcOn()) return false;
            if (t >= 88 && t <= 96) return engine() == 1;
            if (t >= 97) return engine() == 2;
            return true;
        }
        if (!fmOn()) return false;
        if (t >= 107) {
            if (f[kFmCustom] > 0.5f) return true;
            const int a = std::clamp(rint2(f[kFmAlgorithm]), 0, 31), op = t - 107;
            unsigned mask = fmAlgoInfo(a).unreachableMask;
            if (f[kFmMorph] > 0.f) mask |= fmAlgoInfo((a + 1) % 32).unreachableMask;
            return !(mask >> op & 1u);
        }
        return true;
    }
    bool active(int i) const {  // ActiveWhen for a parameter (spec 4.2/5.7)
        switch (P(i).active) {
            case ActiveWhen::None: return true;
            case ActiveWhen::ClassicOnly: return false;
            case ActiveWhen::GraphOn: return graph();
            case ActiveWhen::GraphFeedbackAlgo: return graph() && alg() < 32 && routeAlgoInfo(alg()).feedbackArcs > 0;
            case ActiveWhen::NodeActive: return graph() && slotActive((i - kNodeOffset) / 11);
            case ActiveWhen::GraphAlgo32: return graph() && alg() == 32;
            case ActiveWhen::LfoReferenced: {
                if (!graph()) return false;
                const int l = (i - kLfoOffset) / 4 + 1;
                for (int r = 0; r < kModRowCount; ++r)
                    if (rowLive(r) && rint2(f[static_cast<std::size_t>(modParam(r, 0))]) == l && f[static_cast<std::size_t>(modParam(r, 2))] != 0.f) return true;
                return false;
            }
            case ActiveWhen::RowLive: return graph() && rowLive((i - kModOffset) / 3);
            case ActiveWhen::ModeRS: return mode >= 1;
            case ActiveWhen::FmOnModeRS: return mode >= 1 && fmOn();
            case ActiveWhen::FmOn: return fmOn();
            case ActiveWhen::FmOnNotFx: return fmOn() && mode != 0;
            case ActiveWhen::FmOnCustom0: return fmOn() && f[kFmCustom] <= 0.5f;
            case ActiveWhen::FmOnCustom1: return fmOn() && f[kFmCustom] > 0.5f;
            case ActiveWhen::SrcOn: return srcOn() && mode != 0;
            case ActiveWhen::SrcEngine1: return srcOn() && engine() == 1 && mode != 0;
            case ActiveWhen::SrcEngine2: return srcOn() && engine() == 2 && mode != 0;
        }
        return false;
    }
};

void secR11() {
    long observedActive[17] = {};
    long writes = 0, rowsChecked = 0;
    const EditionContext eds[] = {{0, false}, {1, false}, {2, true}};
    for (const EditionContext& e : eds)
        for (int graphOn = 1; graphOn >= 0; --graphOn)
            for (int fm = 0; fm < 2; ++fm)
                for (int custom = 0; custom < 2; ++custom)
                    for (int src = 0; src < 2; ++src)
                        for (int engine = 0; engine < 3; ++engine)
                            for (int alg : {0, 9, 32}) {
                                if (src == 0 && engine > 0) continue;
                                if (src == 1 && engine == 0) continue;
                                for (Safety sf : kSafeties)
                                    for (int m = 0; m < 2; ++m)
                                        for (std::uint64_t seed = 1; seed <= 5; ++seed) {
                                            StateView s = scattered(e, seed * 17 + static_cast<std::uint64_t>(alg));
                                            s.value[kGraphEnabled] = static_cast<float>(graphOn);
                                            s.value[kFmEnabled] = static_cast<float>(fm);
                                            s.value[kFmCustom] = static_cast<float>(custom);
                                            s.value[kSourceEnabled] = static_cast<float>(src);
                                            s.value[kSourceEngine] = static_cast<float>(engine);
                                            s.value[kRoutingAlgorithm] = static_cast<float>(alg);
                                            LockSet lk;  // the gates are pinned so the scenario is what we say it is
                                            for (int gate : {kFmEnabled, kFmCustom, kSourceEnabled, kSourceEngine, kRoutingAlgorithm}) lk.params.set(static_cast<std::size_t>(gate));
                                            const Config c = m == 0 ? cFull(seed, sf) : cMut(seed, 0.9f, sf);
                                            const Patch p = generate(s, c, lk);
                                            checkPatchBasics(p, "R11");
                                            const Final fin{applyPatch(s, p), modeOf(e)};
                                            const Final inp{s.value, modeOf(e)};
                                            for (const auto& d : p.diffs) {
                                                ++writes;
                                                const auto& pi = P(d.specIndex);
                                                if (d.specIndex >= kFmOffset) CHECKF(!inp.closedFmSrc(), "closed FM/Source (mode %d src %d) but %s written", modeOf(e), src, pi.id);
                                                if (pi.cls != Classification::Random) continue;
                                                // Full mode first resets every writable modulation row to 0/0/0 (Rand.cpp modulationRowsFull); a RowLive field
                                                // that is dead in the final state is allowed only as such a reset to exactly 0.
                                                const bool resetToZero = pi.active == ActiveWhen::RowLive && d.value == 0.f && m == 0;
                                                const bool act = fin.active(d.specIndex) || resetToZero;
                                                CHECKF(act, "idle parameter %s written (ActiveWhen %d; mode %d graph %d fm %d custom %d src %d engine %d alg %d %s seed %llu)", pi.id, static_cast<int>(pi.active), modeOf(e), graphOn, fm, custom, src, engine, alg, m == 0 ? "Full" : "Mutate", static_cast<unsigned long long>(seed));
                                                ++observedActive[static_cast<int>(pi.active)];
                                            }
                                            // modulation rows the engine wrote and that are live in the final state must target a valid destination
                                            for (int r = 0; r < kModRowCount; ++r) {
                                                bool w = false;
                                                for (int f2 = 0; f2 < 3; ++f2) w |= !bitEq(fin.f[static_cast<std::size_t>(modParam(r, f2))], s.value[static_cast<std::size_t>(modParam(r, f2))]);
                                                if (!w || !fin.rowLive(r)) continue;
                                                ++rowsChecked;
                                                const int tgt = rint2(fin.f[static_cast<std::size_t>(modParam(r, 1))]);
                                                CHECKF(fin.targetValid(tgt), "row %d targets dead destination %d (mode %d graph %d fm %d custom %d src %d engine %d %s seed %llu)", r, tgt, modeOf(e), graphOn, fm, custom, src, engine, m == 0 ? "Full" : "Mutate", static_cast<unsigned long long>(seed));
                                                if (c.mode == Mode::Full) {
                                                    CHECKF(sf != Safety::Safe || tgt <= 84, "Safe row targets %d", tgt);
                                                    CHECKF(sf == Safety::Wild || tgt != 87, "non-Wild row targets transpose 87");
                                                }
                                            }
                                        }
                            }
    // graph.feedback (GraphFeedbackAlgo): swept over all 33 routing algorithms with the algorithm locked
    {
        long fbWritten = 0, fbIdleKept = 0;
        for (int alg = 0; alg <= 32; ++alg)
            for (Safety sf : kSafeties)
                for (int m = 0; m < 2; ++m)
                    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
                        StateView s = scattered({0, false}, seed);
                        s.value[kRoutingAlgorithm] = static_cast<float>(alg);
                        LockSet lk; lk.params.set(kRoutingAlgorithm);
                        const Patch p = generate(s, m == 0 ? cFull(seed, sf) : cMut(seed, 1.f, sf), lk);
                        const bool hasFeedback = alg < 32 && routeAlgoInfo(alg).feedbackArcs > 0;
                        bool wrote = false;
                        for (const auto& d : p.diffs) wrote |= d.specIndex == kGraphFeedback;
                        if (wrote) { ++fbWritten; CHECKF(hasFeedback, "graph.feedback written under algorithm %d without feedback arcs", alg); }
                        else if (!hasFeedback) ++fbIdleKept;
                        if (!hasFeedback) CHECK(bitEq(applyPatch(s, p)[kGraphFeedback], s.value[kGraphFeedback]));
                    }
        info("R11 graph.feedback writes %ld, idle keeps %ld", fbWritten, fbIdleKept);
        CHECK(fbWritten > 0 && fbIdleKept > 0);
        observedActive[static_cast<int>(ActiveWhen::GraphFeedbackAlgo)] += fbWritten;
    }
    info("R11 parameter writes inspected: %ld, rows inspected: %ld", writes, rowsChecked);
    static const char* an[17] = {"None", "ClassicOnly", "GraphOn", "GraphFeedbackAlgo", "NodeActive", "GraphAlgo32", "LfoReferenced", "RowLive", "ModeRS", "FmOnModeRS", "FmOn", "FmOnNotFx", "FmOnCustom0", "FmOnCustom1", "SrcOn", "SrcEngine1", "SrcEngine2"};
    for (int a = 0; a < 17; ++a) info("R11 Random writes per ActiveWhen %-18s %ld", an[a], observedActive[a]);
    // non-vacuity: every ActiveWhen that exists among Random parameters (except those that cannot be exercised) was seen written
    bool existsRandom[17] = {};
    for (int i = 0; i < kParamCount; ++i) if (P(i).cls == Classification::Random) existsRandom[static_cast<int>(P(i).active)] = true;
    for (int a = 0; a < 17; ++a) if (existsRandom[a] && a != static_cast<int>(ActiveWhen::ClassicOnly)) CHECKF(observedActive[a] > 0, "ActiveWhen %s: no Random write observed (rule untested)", an[a]);
    CHECKF(rowsChecked > 1000, "rows inspected %ld", rowsChecked);
}
}  // namespace

// =====================================================================================================================
// R12: extreme configurations
// =====================================================================================================================
namespace {
void secR12() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const float amounts[] = {0.f, -0.f, 1e-9f, 0.001f, 0.5f, 0.999999f, 1.f, 2.f, -1.f, nan, inf, -inf, std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min()};
    const int attempts[] = {0, 1, 5, 6, 100, -1, INT_MAX, INT_MIN};
    const std::uint32_t scopes[] = {ScopeAll, 0u, 0xFFFFFFFFu, ScopeChain, ScopeFM | ScopeSource, ScopeMotion, ScopeModuleParams | ScopeMotion};
    const EditionContext eds[] = {{0, false}, {1, false}, {2, true}, {2, false}, {0, true}, {-1, false}, {7, false}, {INT_MAX, true}};
    Pcg32 rg(2026, 1028);
    auto pick = [&](std::size_t n) { return static_cast<std::size_t>(rg.next32() % n); };
    long runs = 0, sane = 0, okPatches = 0, failedPatches = 0;
    for (int iter = 0; iter < 150000; ++iter) {
        const EditionContext e = eds[pick(std::size(eds))];
        StateView s = defaults(e);
        const int kind = static_cast<int>(pick(8));
        switch (kind) {
            case 0: break;
            case 1: for (int i = 0; i < kParamCount; ++i) s.value[static_cast<std::size_t>(i)] = static_cast<float>(P(i).minV); break;
            case 2: for (int i = 0; i < kParamCount; ++i) s.value[static_cast<std::size_t>(i)] = static_cast<float>(P(i).maxV); break;
            case 3: for (int i = 0; i < kParamCount; ++i) s.value[static_cast<std::size_t>(i)] = nan; break;
            case 4: for (int i = 0; i < kParamCount; ++i) s.value[static_cast<std::size_t>(i)] = (i & 1) ? inf : -inf; break;
            case 5: case 6: s = scattered(e, rg.next32()); break;
            case 7: s = scattered(e, rg.next32()); for (int i = 0; i < kParamCount; ++i) if (rg.next32() % 5 == 0) s.value[static_cast<std::size_t>(i)] = (rg.next32() & 1) ? nan : inf; break;
        }
        const bool isSane = kind != 3 && kind != 4 && kind != 7;
        Config c;
        c.seed = (rg.next32() % 4 == 0) ? kSeedSet[pick(std::size(kSeedSet))] : (static_cast<std::uint64_t>(rg.next32()) << 32 | rg.next32());
        c.mode = (rg.next32() & 1) ? Mode::Full : Mode::Mutate;
        c.amount = amounts[pick(std::size(amounts))];
        c.safety = kSafeties[pick(3)];
        c.scopeMask = scopes[pick(std::size(scopes))];
        c.attempt = attempts[pick(std::size(attempts))];
        c.intoB = (rg.next32() & 1) != 0;
        LockSet lk;
        switch (rg.next32() % 6) {
            case 0: lk.params.set(); break;
            case 1: lk.sections = 0xFFFFFFFFu; lk.modRows = 0xFFFF; break;
            case 2: for (int i = 0; i < kParamCount; ++i) if (rg.next32() % 3 == 0) lk.params.set(static_cast<std::size_t>(i)); break;
            case 3: lk.sections = rg.next32(); lk.modRows = static_cast<std::uint16_t>(rg.next32()); break;
            default: break;
        }
        const Patch p = generate(s, c, lk);
        ++runs;
        checkPatchBasics(p, "R12");
        p.report.ok ? ++okPatches : ++failedPatches;
        for (const auto& d : p.diffs) CHECK(!lk.isLocked(d.specIndex));
        if (c.mode == Mode::Mutate && !(c.amount > 0.f)) CHECK(p.diffs.empty());
        if (!(c.scopeMask & ScopeAll)) CHECK(p.diffs.empty());
        for (const auto& d : p.diffs) CHECK(groupBit(P(d.specIndex).group) & c.scopeMask);
        const Arr after = applyPatch(s, p);
        for (int i = 0; i < kParamCount; ++i) if (P(i).cls == Classification::Never) CHECK(bitEq(after[static_cast<std::size_t>(i)], s.value[static_cast<std::size_t>(i)]));
        if (isSane) {
            ++sane;
            for (int i = 0; i < kParamCount; ++i) CHECKF(inRangeF(i, after[static_cast<std::size_t>(i)]), "after-patch %s=%g out of range (kind %d)", P(i).id, static_cast<double>(after[static_cast<std::size_t>(i)]), kind);
        } else {
            for (const auto& d : p.diffs) CHECK(std::isfinite(d.value));
        }
    }
    info("R12 fuzz runs %ld (sane inputs %ld), ok patches %ld, not-ok patches %ld", runs, sane, okPatches, failedPatches);
    // observed (not asserted: spec silent): behaviour for amount > 1. attempt out of range is asserted in R13 (e); the old "attempt 6 != 5, != 0" observation no longer holds.
    {
        const StateView s = defaults({2, true});
        info("R12 observed: Mutate amount 2.0 == amount 1.0 : %d", static_cast<int>(samePatch(generate(s, cMut(5, 2.f, Safety::Wild), LockSet{}), generate(s, cMut(5, 1.f, Safety::Wild), LockSet{}))));
    }
    CHECK(runs == 150000);
}
}  // namespace

// =====================================================================================================================
// R13: scope / intoB / attempt
// =====================================================================================================================
namespace {
void secR13() {
    // (a) scope: every one of the 32 subsets (+ junk high bits): only groups whose bit is set are written
    long perMask[32] = {};
    for (std::uint32_t mask = 0; mask < 32; ++mask)
        for (std::uint32_t junk : {0u, 0xFFFFFFE0u})
            for (const Ed& ed : {kEds[0], kEds[1], kEds[2]})
                for (Safety sf : kSafeties)
                    for (int m = 0; m < 2; ++m)
                        for (std::uint64_t seed = 1; seed <= 10; ++seed) {
                            const StateView s = (seed & 1) ? defaults(ed.e) : scattered(ed.e, seed);
                            const std::uint32_t full = mask | junk;
                            const Config c = m == 0 ? cFull(seed, sf, full) : cMut(seed, 0.8f, sf, full);
                            const Patch p = generate(s, c, LockSet{});
                            checkPatchBasics(p, "R13 scope");
                            for (const auto& d : p.diffs) {
                                CHECKF((groupBit(P(d.specIndex).group) & mask) != 0, "scope %u: %s (group %s) written", mask, P(d.specIndex).id, kGroupName[static_cast<int>(P(d.specIndex).group)]);
                                ++perMask[mask];
                            }
                            if (mask == 0 && junk == 0) { CHECK(p.diffs.empty()); CHECK(sameArr(applyPatch(s, p), s.value)); }
                        }
    CHECK(perMask[0] == 0);
    for (std::uint32_t mask = 1; mask < 32; ++mask) CHECKF(perMask[mask] > 0, "scope %u never produced a diff (test vacuous)", mask);

    // (b) intoB only changes where the caller writes: identical patches
    for (const Ed& ed : kEds)
        for (Safety sf : kSafeties)
            for (std::uint32_t scope : {static_cast<std::uint32_t>(ScopeAll), static_cast<std::uint32_t>(ScopeFM | ScopeSource), static_cast<std::uint32_t>(ScopeMotion)})
                for (std::uint64_t seed : kSeedSet)
                    for (int m = 0; m < 2; ++m) {
                        const StateView s = scattered(ed.e, seed ^ 0xA5);
                        Config a = m == 0 ? cFull(seed, sf, scope) : cMut(seed, 0.5f, sf, scope), b = a;
                        a.intoB = false; b.intoB = true;
                        CHECK(samePatch(generate(s, a, LockSet{}), generate(s, b, LockSet{})));
                    }

    // (c) attempt k == seed ^ (k * phi) with attempt 0 (spec 6.1); the six attempts 0..5 give six different patches
    for (const Ed& ed : {kEds[0], kEds[2]})
        for (Safety sf : kSafeties)
            for (std::uint64_t seed : kSeedSet)
                for (int m = 0; m < 2; ++m) {
                    const StateView s = defaults(ed.e);
                    std::set<std::vector<std::pair<int, std::uint32_t>>> distinct;
                    for (int k = 0; k <= 5; ++k) {
                        Config a = m == 0 ? cFull(seed, sf) : cMut(seed, 0.9f, sf);
                        a.attempt = k;
                        Config b = a;
                        b.seed = seed ^ (static_cast<std::uint64_t>(k) * kPhi); b.attempt = 0;
                        const Patch pa = generate(s, a, LockSet{});
                        CHECKF(samePatch(pa, generate(s, b, LockSet{})), "attempt %d != seed^(k*phi) (seed %llu)", k, static_cast<unsigned long long>(seed));
                        std::vector<std::pair<int, std::uint32_t>> key;
                        for (const auto& d : pa.diffs) key.emplace_back(d.specIndex, bitsOf(d.value));
                        distinct.insert(key);
                    }
                    CHECKF(distinct.size() == 6, "only %zu distinct patches over attempts 0..5 (%s seed %llu %s)", distinct.size(), ed.name, static_cast<unsigned long long>(seed), m == 0 ? "Full" : "Mutate");
                }

    // (e) attempt out of range == the nearest boundary (engine clamps attempt to [0,5], feat/randomize-engine-230 ecde036):
    //     the generated patch (ok/reason/diffs, bit-identical) AND the 495 values of applyPatch must be identical.
    {
        const int lowEq[] = {-1, -2, -100, -65536, INT_MIN + 1, INT_MIN};
        const int highEq[] = {6, 7, 100, 65536, INT_MAX - 1, INT_MAX};
        long cmp = 0;
        for (const Ed& ed : kEds)
            for (Safety sf : kSafeties)
                for (std::uint64_t seed : kSeedSet)
                    for (int m = 0; m < 2; ++m)
                        for (int st = 0; st < 2; ++st) {
                            const StateView s = st == 0 ? defaults(ed.e) : scattered(ed.e, seed ^ 0x77);
                            const auto mk = [&](int attempt) { Config c = m == 0 ? cFull(seed, sf) : cMut(seed, 0.7f, sf); c.attempt = attempt; return c; };
                            const Patch p0 = generate(s, mk(0), LockSet{}), p5 = generate(s, mk(5), LockSet{});
                            const Arr v0 = applyPatch(s, p0), v5 = applyPatch(s, p5);
                            for (int k : lowEq) {
                                const Patch pk = generate(s, mk(k), LockSet{});
                                CHECKF(samePatch(pk, p0), "attempt %d != attempt 0 (patch; %s safety %d seed %llu)", k, ed.name, static_cast<int>(sf), static_cast<unsigned long long>(seed));
                                CHECKF(sameArr(applyPatch(s, pk), v0), "attempt %d != attempt 0 (495 applied values; %s safety %d seed %llu)", k, ed.name, static_cast<int>(sf), static_cast<unsigned long long>(seed));
                                ++cmp;
                            }
                            for (int k : highEq) {
                                const Patch pk = generate(s, mk(k), LockSet{});
                                CHECKF(samePatch(pk, p5), "attempt %d != attempt 5 (patch; %s safety %d seed %llu)", k, ed.name, static_cast<int>(sf), static_cast<unsigned long long>(seed));
                                CHECKF(sameArr(applyPatch(s, pk), v5), "attempt %d != attempt 5 (495 applied values; %s safety %d seed %llu)", k, ed.name, static_cast<int>(sf), static_cast<unsigned long long>(seed));
                                ++cmp;
                            }
                        }
        CHECK(cmp == 5L * 3 * 10 * 2 * 2 * 12);
        info("R13 attempt clamp: %ld out-of-range comparisons (6 low + 6 high attempts), each checked on patch and on 495 applied values", cmp);
        {   // the two boundaries are different patches (otherwise the clamp checks above would be vacuous)
            const StateView s = defaults({2, true});
            Config lo = cFull(5); lo.attempt = 0;
            Config hi = cFull(5); hi.attempt = 5;
            CHECK(!samePatch(generate(s, lo, LockSet{}), generate(s, hi, LockSet{})));
        }
    }

    // (d) motion-only scope leaves chain / FM / source untouched, chain-only leaves motion untouched
    for (std::uint64_t seed = 1; seed <= 100; ++seed)
        for (Safety sf : kSafeties) {
            const StateView s = defaults({2, true});
            const Arr a = applyPatch(s, generate(s, cFull(seed, sf, ScopeMotion), LockSet{}));
            for (int sl = 0; sl < kSlotCount; ++sl) for (int f = 0; f < 11; ++f) CHECK(bitEq(a[static_cast<std::size_t>(nodeParam(sl, f))], s.value[static_cast<std::size_t>(nodeParam(sl, f))]));
            for (int i : {kRoutingAlgorithm, kFmEnabled, kFmAlgorithm, kFmCustom, kSourceEnabled, kSourceEngine}) CHECK(bitEq(a[static_cast<std::size_t>(i)], s.value[static_cast<std::size_t>(i)]));
            const Arr b = applyPatch(s, generate(s, cFull(seed, sf, ScopeChain), LockSet{}));
            for (int i = kModOffset; i < kFmOffset; ++i) CHECK(bitEq(b[static_cast<std::size_t>(i)], s.value[static_cast<std::size_t>(i)]));
        }
}
}  // namespace

// =====================================================================================================================
// R16: allocation count
// =====================================================================================================================
namespace {
// Upper bound for operator-new calls of one generate(): measured maximum on 3600 runs was 14 (R16 INFO line in the log); bound = 32 (> 2x).
constexpr std::uint64_t kMaxAllocsPerGenerate = 32;

void secR16() {
    std::uint64_t maxAllocs = 0, total = 0, calls = 0, maxDiffs = 0;
    for (const Ed& ed : kEds)
        for (Safety sf : kSafeties)
            for (int m = 0; m < 3; ++m)
                for (std::uint64_t seed = 1; seed <= 80; ++seed) {
                    const StateView s = (seed & 1) ? defaults(ed.e) : scattered(ed.e, seed);
                    const Config c = m == 0 ? cFull(seed, sf) : cMut(seed, m == 1 ? 0.5f : 1.f, sf);
                    const LockSet lk;
                    g_allocCount.store(0);
                    g_countAllocs.store(true);
                    Patch p = generate(s, c, lk);
                    g_countAllocs.store(false);
                    const std::uint64_t n = g_allocCount.load();
                    maxAllocs = std::max(maxAllocs, n); total += n; ++calls; maxDiffs = std::max<std::uint64_t>(maxDiffs, p.diffs.size());
                    CHECKF(n <= kMaxAllocsPerGenerate, "generate() performed %llu allocations (bound %llu, %zu diffs)", static_cast<unsigned long long>(n), static_cast<unsigned long long>(kMaxAllocsPerGenerate), p.diffs.size());
                }
    info("R16 generate(): calls %llu, allocations max %llu, mean %.2f, largest patch %llu diffs, bound %llu",
         static_cast<unsigned long long>(calls), static_cast<unsigned long long>(maxAllocs), static_cast<double>(total) / static_cast<double>(calls), static_cast<unsigned long long>(maxDiffs), static_cast<unsigned long long>(kMaxAllocsPerGenerate));
    {   // the counter is live: an empty patch allocates nothing, a populated one does
        const StateView s = defaults({2, true});
        g_allocCount.store(0); g_countAllocs.store(true);
        Patch e = generate(s, cMut(1, 0.f), LockSet{});
        g_countAllocs.store(false);
        CHECKF(e.diffs.empty() && g_allocCount.load() == 0, "empty patch allocated %llu times", static_cast<unsigned long long>(g_allocCount.load()));
        g_allocCount.store(0); g_countAllocs.store(true);
        Patch f = generate(s, cFull(1, Safety::Wild), LockSet{});
        g_countAllocs.store(false);
        CHECK(!f.diffs.empty() && g_allocCount.load() > 0);
    }
    // the pure helpers (applyPatch, tables, math, RNG, lock queries) allocate nothing
    const StateView s = scattered({2, true}, 5);
    const Patch p = generate(s, cFull(3, Safety::Wild), LockSet{});
    LockSet lk; lk.params.set(3); lk.sections = kLockFm; lk.modRows = 3;
    volatile float sink = 0.f; volatile int isink = 0;
    g_allocCount.store(0); g_countAllocs.store(true);
    for (int k = 0; k < 2000; ++k) {
        const Arr a = applyPatch(s, p);
        sink = sink + a[static_cast<std::size_t>(k % kParamCount)];
        sink = sink + trimDbForLoudness(static_cast<float>(k % 40) - 30.f);
        isink = isink + lk.isLocked(k % kParamCount);
        isink = isink + paramIndex("fm.index") + static_cast<int>(paramTable()[static_cast<std::size_t>(k % kParamCount)].hash & 1);
        sink = sink + static_cast<float>(log2Det(1.0 + k) + exp2Det(k * 0.001) + powDet(1.5, 0.3));
        Pcg32 gen(static_cast<std::uint64_t>(k), 5); sink = sink + static_cast<float>(gen.nextUnit()) + static_cast<float>(fnv1a64("abc") & 1);
    }
    g_countAllocs.store(false);
    CHECKF(g_allocCount.load() == 0, "applyPatch/trim/isLocked/tables/math allocated %llu times", static_cast<unsigned long long>(g_allocCount.load()));
}
}  // namespace

// =====================================================================================================================
// R18: not triggered => bit-identical
// =====================================================================================================================
namespace {
void secR18() {
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const float zeroAmounts[] = {0.f, -0.f, -1e-30f, -1.f, -inf};
    for (const Ed& ed : kEds)
        for (int variant = 0; variant < 4; ++variant) {
            StateView s = variant == 0 ? defaults(ed.e) : scattered(ed.e, 77 + static_cast<std::uint64_t>(variant));
            if (variant == 2) for (int i = 0; i < kParamCount; ++i) s.value[static_cast<std::size_t>(i)] = (i % 4 == 0) ? -0.0f : ((i % 4 == 1) ? nan : s.value[static_cast<std::size_t>(i)]);
            if (variant == 3) for (int i = 0; i < kParamCount; ++i) if (i % 7 == 0) s.value[static_cast<std::size_t>(i)] = (i & 1) ? inf : -inf;
            for (Safety sf : kSafeties)
                for (std::uint64_t seed : kSeedSet) {
                    for (float a : zeroAmounts) {  // Mutate with amount <= 0 (spec 6.2)
                        const Patch p = generate(s, cMut(seed, a, sf), LockSet{});
                        CHECKF(p.diffs.empty() && p.report.ok, "Mutate amount %g: %zu diffs ok=%d", static_cast<double>(a), p.diffs.size(), p.report.ok);
                        CHECK(sameArr(applyPatch(s, p), s.value));
                    }
                    for (int m = 0; m < 2; ++m) {  // scopeMask 0
                        const Patch p = generate(s, m == 0 ? cFull(seed, sf, 0) : cMut(seed, 1.f, sf, 0), LockSet{});
                        CHECK(p.diffs.empty()); CHECK(sameArr(applyPatch(s, p), s.value));
                    }
                    for (int m = 0; m < 2; ++m) {  // every parameter locked
                        LockSet all; all.params.set();
                        const Patch p = generate(s, m == 0 ? cFull(seed, sf) : cMut(seed, 1.f, sf), all);
                        CHECK(p.diffs.empty()); CHECK(sameArr(applyPatch(s, p), s.value));
                    }
                    {   // the five writable groups locked
                        LockSet lk = lockGroup(Group::Chain);
                        for (Group gr : {Group::ModuleParams, Group::FM, Group::Source, Group::Motion}) lk.params |= lockGroup(gr).params;
                        for (int m = 0; m < 2; ++m) {
                            const Patch p = generate(s, m == 0 ? cFull(seed, sf) : cMut(seed, 1.f, sf), lk);
                            CHECK(p.diffs.empty()); CHECK(sameArr(applyPatch(s, p), s.value));
                        }
                    }
                }
        }
    {   // the empty patch is the identity (also with NaN / -0.0); out-of-range indices are ignored
        StateView s = defaults({2, true});
        s.value[10] = std::numeric_limits<float>::quiet_NaN(); s.value[11] = -0.0f;
        const Patch none;
        CHECK(sameArr(applyPatch(s, none), s.value));
        Patch bad; bad.diffs.push_back({-1, 1.f}); bad.diffs.push_back({kParamCount, 1.f}); bad.diffs.push_back({INT_MAX, 1.f});
        CHECK(sameArr(applyPatch(s, bad), s.value));
    }
}
}  // namespace

// ---- driver ---------------------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
    struct Sec { const char* name; void (*fn)(const char*); };
    const Sec secs[] = {
        {"R02", [](const char*) { secR02(); }}, {"R05", secR05},
        {"R06", [](const char*) { secR06(); }}, {"R08", [](const char*) { secR08(); }},
        {"R11", [](const char*) { secR11(); }}, {"R12", [](const char*) { secR12(); }},
        {"R13", [](const char*) { secR13(); }}, {"R16", [](const char*) { secR16(); }},
        {"R18", [](const char*) { secR18(); }}};
    const std::string want = argc > 1 ? argv[1] : "all";
    const char* tsv = argc > 2 ? argv[2] : "";
    int totalFail = 0, ran = 0;
    for (const Sec& sc : secs) {
        if (want != "all" && want != sc.name) continue;
        g = Tally{};
        const auto t0 = std::chrono::steady_clock::now();
        sc.fn(tsv);
        const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("SECTION %s: %d checks, %d failures, %.2f s\n", sc.name, g.checks, g.failures, sec);
        totalFail += g.failures; ++ran;
    }
    if (ran == 0) { std::fprintf(stderr, "unknown section '%s'\n", want.c_str()); return 2; }
    std::printf("aether_rand_invariants: %d section(s), %d failure(s)\n", ran, totalFail);
    return totalFail == 0 ? 0 : 1;
}
