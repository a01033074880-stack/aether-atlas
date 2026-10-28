// AETHER 2.3.0 Controlled Randomization engine, phase P1. See include/aether/Rand.h and docs/randomization-spec.md.
#include "aether/Rand.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace aether::rand {

// ---- deterministic math ---------------------------------------------------------------------------------------------
double log2Det(double x) noexcept {
    int e = 0;
    double m = std::frexp(x, &e);  // m in [0.5, 1)
    if (m < 0.70710678118654752) { m *= 2.0; e -= 1; }
    const double s = (m - 1.0) / (m + 1.0), s2 = s * s;
    double acc = 0.0;
    for (int k = 23; k >= 1; k -= 2) acc = acc * s2 + 1.0 / static_cast<double>(k);
    return static_cast<double>(e) + 2.0 * s * acc * 1.4426950408889634074;
}
double exp2Det(double x) noexcept {
    if (x > 1000.0) x = 1000.0;
    if (x < -1000.0) x = -1000.0;
    const double n = std::floor(x + 0.5);
    const double y = (x - n) * 0.69314718055994530942;
    double acc = 1.0;
    for (int k = 18; k >= 1; --k) acc = 1.0 + acc * y / static_cast<double>(k);
    return std::ldexp(acc, static_cast<int>(n));
}
double powDet(double base, double exponent) noexcept { return exp2Det(exponent * log2Det(base)); }

// ---- generated tables -----------------------------------------------------------------------------------------------
namespace {
#include "RandTable.inc"
}
std::span<const ParamInfo, kParamCount> paramTable() noexcept { return std::span<const ParamInfo, kParamCount>(kParamTable); }
const MacroInfo& macroInfo(int t, int k) noexcept { return kMacroTable[static_cast<std::size_t>(std::clamp(t, 0, kModuleTypeCount - 1) * 4 + std::clamp(k, 0, 3))]; }
const FmAlgoInfo& fmAlgoInfo(int a) noexcept { return kFmAlgoTable[static_cast<std::size_t>(std::clamp(a, 0, kFmAlgorithmCount - 1))]; }
const RouteAlgoInfo& routeAlgoInfo(int a) noexcept { return kRouteAlgoTable[static_cast<std::size_t>(std::clamp(a, 0, kRouteAlgorithmCount - 1))]; }
int paramIndex(std::string_view id) noexcept {
    for (int i = 0; i < kParamCount; ++i)
        if (id == kParamTable[static_cast<std::size_t>(i)].id) return i;
    return -1;
}

bool LockSet::isLocked(int i) const noexcept {
    if (i < 0 || i >= kParamCount) return false;
    if (params.test(static_cast<std::size_t>(i))) return true;
    if (i >= kNodeOffset && i < kRouteOffset) {
        const int s = (i - kNodeOffset) / 11;
        if (s < kSlotCount && (sections >> s & 1u)) return true;
    }
    if (i >= kFmOffset && i < kSourceOffset && (sections & kLockFm)) return true;
    if (i >= kSourceOffset && (sections & kLockSource)) return true;
    if (i >= kLfoOffset && i < kModOffset) {
        const int l = (i - kLfoOffset) / 4;
        if (sections >> (14 + l) & 1u) return true;
    }
    if (i >= kModOffset && i < kFmOffset) {
        if (sections & kLockMod) return true;
        const int r = (i - kModOffset) / 3;
        if (modRows >> r & 1u) return true;
    }
    return false;
}

float trimDbForLoudness(float measuredDb, float targetDb) noexcept {
    if (!std::isfinite(measuredDb) || !std::isfinite(targetDb)) return 0.f;
    return std::clamp(targetDb - measuredDb, -24.f, 0.f);
}

std::array<float, kParamCount> applyPatch(const StateView& s, const Patch& p) noexcept {
    auto v = s.value;
    for (const auto& d : p.diffs)
        if (d.specIndex >= 0 && d.specIndex < kParamCount) v[static_cast<std::size_t>(d.specIndex)] = d.value;
    return v;
}

namespace {

// ---- module / rule constants ----------------------------------------------------------------------------------------
constexpr int kActiveCap[3] = {5, 8, 8};              // spec 3.2 active slots (min 2)
constexpr int kRepeatCap[3] = {1, 2, 3};              // spec 5.2 same-type repeat limit
constexpr int kHighRiskCap[3] = {2, 3, 5};
constexpr int kFreezeTypeCap[3] = {0, 1, 2};
constexpr int kRowMin[3] = {1, 2, 3};
constexpr int kRowCap[3] = {4, 8, 12};                // spec 5.6
constexpr double kFmIndexCap[3] = {3.5, 6.0, 12.0};   // spec 5.4 rule 3
constexpr double kMinDepth = 0.02;
constexpr double kFreezeThreshold = 0.5;

constexpr bool isFreezeType(int t) { return t == 6 || t == 18; }
constexpr int freezeKnob(int t) { return t == 6 ? 3 : (t == 18 ? 2 : -1); }  // Metadata.cpp freezeMacro
constexpr bool isHighRisk(int t) { return t == 11 || t == 8 || t == 7 || t == 9 || t == 10 || t == 14 || t == 2 || t == 12; }
constexpr bool isSpaceLike(int t) { return t == 4 || t == 15 || t == 20; }

enum class Cat { Knob, Frozen, MixPan, Level, SrcGeneral, SrcEngine, Transpose, FmIndex, FmFeedback, FmMorphSpread, FmLevel };
constexpr double kDepthCap[11][3] = {
    {0.3, 0.5, 1.0},    // Knob
    {0.0, 0.5, 1.0},    // Frozen (further limited by value + sum < 0.5)
    {0.3, 0.5, 1.0},    // MixPan
    {0.06, 0.1, 0.25},  // Level
    {0.15, 0.25, 0.5},  // SrcGeneral (85,86)
    {0.3, 0.5, 1.0},    // SrcEngine (88..101)
    {0.0, 0.0, 0.02},   // Transpose (87)
    {0.1, 0.15, 0.3},   // FmIndex (102)
    {0.1, 0.2, 0.4},    // FmFeedback (104)
    {0.3, 0.5, 1.0},    // FmMorphSpread (103,106)
    {0.15, 0.3, 0.6}};  // FmLevel (105,107..112)

constexpr double kRatioSafe[] = {0.5, 1, 2, 3, 4};
constexpr double kRatioNormal[] = {0.5, 1, 2, 3, 4, 1.5, 5, 6, 8};
constexpr double kRatioWild[] = {0.5, 1, 2, 3, 4, 1.5, 5, 6, 8, 0.25, 0.75, 7, 9, 11, 12, 16, 1.414, 2.718, 3.14};
constexpr double kRatioCarrier[] = {0.5, 1, 2};

constexpr std::uint32_t groupBit(Group g) {
    switch (g) {
        case Group::Chain: return ScopeChain;
        case Group::ModuleParams: return ScopeModuleParams;
        case Group::FM: return ScopeFM;
        case Group::Source: return ScopeSource;
        case Group::Motion: return ScopeMotion;
        default: return 0;
    }
}
constexpr std::uint64_t kPhi64 = 0x9E3779B97F4A7C15ULL;

struct EW {  // effective sampling window (unit values)
    Dist d;
    double lo, hi, ka, kb, center, p;
};

int rint_(double v) { return (v == v) ? static_cast<int>(std::floor(std::clamp(v, -1.0e6, 1.0e6) + 0.5)) : 0; }
std::uint32_t bitsOf(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }

class Engine {
public:
    Engine(const StateView& in, const Config& cfg, const LockSet& locks)
        : cfg_(cfg), locks_(locks), edition_(in.edition), raw_(in.value),
          mode_(in.edition.isSynth ? 2 : (in.edition.effectiveMode == 1 ? 1 : 0)),
          si_(cfg.safety == Safety::Safe ? 0 : (cfg.safety == Safety::Normal ? 1 : 2)),
          aseed_(cfg.seed ^ (static_cast<std::uint64_t>(std::max(cfg.attempt, 0)) * kPhi64)),
          rs_(aseed_, fnv1a64("@structure")), rm_(aseed_, fnv1a64("@modulation")) {
        for (int i = 0; i < kParamCount; ++i) {
            const auto& pi = kParamTable[static_cast<std::size_t>(i)];
            float v = raw_[static_cast<std::size_t>(i)];
            if (!std::isfinite(v)) v = static_cast<float>(pi.defaultV);
            work_[static_cast<std::size_t>(i)] = std::clamp(v, static_cast<float>(pi.minV), static_cast<float>(pi.maxV));
        }
        fmSrcOpen_ = mode_ == 2 || (mode_ == 1 && srcOn());
    }

    // -- state predicates (on the working state) --
    double get(int i) const { return static_cast<double>(work_[static_cast<std::size_t>(i)]); }
    bool graphOn() const { return get(kGraphEnabled) > 0.5; }
    bool fmOn() const { return get(kFmEnabled) > 0.5; }
    bool srcOn() const { return get(kSourceEnabled) > 0.5 && get(kSourceEngine) >= 0.5; }
    int engine() const { return rint_(get(kSourceEngine)); }
    int algorithm() const { return std::clamp(rint_(get(kRoutingAlgorithm)), 0, 32); }
    int moduleType(int s) const { return std::clamp(rint_(get(nodeParam(s, 0))), 0, kModuleTypeCount - 1); }
    bool slotActive(int s) const { return get(nodeParam(s, 1)) > 0.5 && moduleType(s) != 0; }
    int activeSlots() const { int n = 0; for (int s = 0; s < kSlotCount; ++s) n += slotActive(s); return n; }
    int fmAlgo() const { return std::clamp(rint_(get(kFmAlgorithm)), 0, 31); }
    bool rowLive(int r) const { return get(modParam(r, 0)) >= 0.5 && get(modParam(r, 1)) >= 0.5; }
    int rowSource(int r) const { return rint_(get(modParam(r, 0))); }
    int rowTarget(int r) const { return rint_(get(modParam(r, 1))); }
    void setWork(const std::array<float, kParamCount>& v) { work_ = v; }
    const std::array<float, kParamCount>& work() const { return work_; }
    int modeValue() const { return mode_; }
    bool fmSrcOpen() const { return fmSrcOpen_; }

    bool inScope(const ParamInfo& pi) const { return (groupBit(pi.group) & cfg_.scopeMask) != 0; }
    bool groupClosed(const ParamInfo& pi) const {
        return (pi.group == Group::FM || pi.group == Group::Source) && !fmSrcOpen_;
    }
    bool canWrite(int i) const {
        const auto& pi = kParamTable[static_cast<std::size_t>(i)];
        if (pi.cls == Classification::Never || locks_.isLocked(i) || !inScope(pi) || groupClosed(pi)) return false;
        if (i == kGraphTrim) return false;  // P1 never writes the trim (audition phase does)
        return true;
    }

    // target validity (spec 5.7), on the working state
    bool targetValid(int t) const {
        if (t < 1 || t > 112) return false;
        if (t <= 84) return slotActive((t - 1) / 7);
        if (mode_ == 0 || !graphOn()) return false;
        if (t <= 101) {
            if (!srcOn()) return false;
            if (t >= 88 && t <= 96) return engine() == 1;
            if (t >= 97) return engine() == 2;
            return true;
        }
        if (!fmOn()) return false;
        if (t >= 107) {
            if (get(kFmCustom) > 0.5) return true;
            const int op = t - 107;
            unsigned mask = fmAlgoInfo(fmAlgo()).unreachableMask;
            if (get(kFmMorph) > 0.0) mask |= fmAlgoInfo((fmAlgo() + 1) % 32).unreachableMask;
            return !(mask >> op & 1u);
        }
        return true;
    }
    Cat category(int t) const {
        if (t <= 84) {
            const int attr = (t - 1) % 7, slot = (t - 1) / 7;
            if (attr < 4) return freezeKnob(moduleType(slot)) == attr ? Cat::Frozen : Cat::Knob;
            return attr == 5 ? Cat::Level : Cat::MixPan;
        }
        if (t == 85 || t == 86) return Cat::SrcGeneral;
        if (t == 87) return Cat::Transpose;
        if (t <= 101) return Cat::SrcEngine;
        if (t == 102) return Cat::FmIndex;
        if (t == 104) return Cat::FmFeedback;
        if (t == 103 || t == 106) return Cat::FmMorphSpread;
        return Cat::FmLevel;
    }
    double knobValueOfTarget(int t) const {  // for Cat::Frozen: current knob value of the target
        const int slot = (t - 1) / 7, attr = (t - 1) % 7;
        return get(nodeParam(slot, 5 + attr));
    }
    // largest |depth| a (new) row onto target t may have given the other rows already on that target
    double depthBudget(int t, int exceptRow) const {
        const Cat c = category(t);
        double cap = kDepthCap[static_cast<int>(c)][si_];
        double others = 0.0;
        for (int r = 0; r < kModRowCount; ++r)
            if (r != exceptRow && rowLive(r) && rowTarget(r) == t) others += std::abs(get(modParam(r, 2)));
        if (c == Cat::Frozen) cap = std::min(cap, kFreezeThreshold - knobValueOfTarget(t) - 1e-4);
        return cap - others;
    }
    double fmMaxIncoming() const {
        if (get(kFmCustom) > 0.5) {
            double best = 1.0;
            for (int a = 0; a < 6; ++a) {
                double row = 0, col = 0;
                for (int b = 0; b < 6; ++b) { row += std::abs(get(kEdgeOffset + a * 6 + b)); col += std::abs(get(kEdgeOffset + b * 6 + a)); }
                best = std::max({best, row, col});
            }
            return best;
        }
        double m = fmAlgoInfo(fmAlgo()).maxIncoming;
        if (get(kFmMorph) > 0.0) m = std::max(m, fmAlgoInfo((fmAlgo() + 1) % 32).maxIncoming);
        return m;
    }
    unsigned carrierMask() const {
        if (get(kFmCustom) > 0.5) {
            unsigned m = 0;
            for (int o = 0; o < 6; ++o) if (get(opParam(o, 2)) > 0.5) m |= 1u << o;
            return m;
        }
        unsigned m = fmAlgoInfo(fmAlgo()).carrierMask;
        if (get(kFmMorph) > 0.0) m |= fmAlgoInfo((fmAlgo() + 1) % 32).carrierMask;
        return m;
    }

    bool isActive(int i) const {
        const auto& pi = kParamTable[static_cast<std::size_t>(i)];
        switch (pi.active) {
            case ActiveWhen::None: return true;
            case ActiveWhen::ClassicOnly: return false;
            case ActiveWhen::GraphOn: return graphOn();
            case ActiveWhen::GraphFeedbackAlgo: return graphOn() && algorithm() < 32 && routeAlgoInfo(algorithm()).feedbackArcs > 0;
            case ActiveWhen::NodeActive: return graphOn() && slotActive((i - kNodeOffset) / 11);
            case ActiveWhen::GraphAlgo32: return graphOn() && algorithm() == 32;
            case ActiveWhen::LfoReferenced: {
                if (!graphOn()) return false;
                const int l = (i - kLfoOffset) / 4 + 1;
                for (int r = 0; r < kModRowCount; ++r)
                    if (rowLive(r) && rowSource(r) == l && targetValid(rowTarget(r)) && get(modParam(r, 2)) != 0.0) return true;
                return false;
            }
            case ActiveWhen::RowLive: return graphOn() && rowLive((i - kModOffset) / 3);
            case ActiveWhen::ModeRS: return mode_ >= 1;
            case ActiveWhen::FmOnModeRS: return mode_ >= 1 && fmOn();
            case ActiveWhen::FmOn: return fmOn();
            case ActiveWhen::FmOnNotFx: return fmOn() && mode_ != 0;
            case ActiveWhen::FmOnCustom0: return fmOn() && get(kFmCustom) <= 0.5;
            case ActiveWhen::FmOnCustom1: return fmOn() && get(kFmCustom) > 0.5;
            case ActiveWhen::SrcOn: return srcOn() && mode_ != 0;
            case ActiveWhen::SrcEngine1: return srcOn() && engine() == 1 && mode_ != 0;
            case ActiveWhen::SrcEngine2: return srcOn() && engine() == 2 && mode_ != 0;
        }
        return false;
    }

    // effective window of a Random parameter on the working state
    EW window(int i) const {
        const auto& pi = kParamTable[static_cast<std::size_t>(i)];
        if (i >= kNodeOffset && i < kRouteOffset && pi.group == Group::ModuleParams && (i - kNodeOffset) % 11 >= 5) {
            const int slot = (i - kNodeOffset) / 11, knob = (i - kNodeOffset) % 11 - 5;
            const int ty = moduleType(slot);
            const MacroInfo& m = macroInfo(ty, knob);
            EW mw{m.dist, m.win[si_].lo, m.win[si_].hi, m.ka, m.kb, std::clamp(m.center, m.win[si_].lo, m.win[si_].hi), m.sparseP[si_]};
            // spec 5.2: the freeze knob stays < 0.5 unless Wild (the 3.4 table's Normal P=0.05 contradicts this; the stricter rule wins)
            if (si_ < 2 && freezeKnob(ty) == knob) { mw.p = 0.0; mw.hi = std::min(mw.hi, 0.0); mw.lo = std::min(mw.lo, mw.hi); }
            return mw;
        }
        EW w{pi.dist, pi.win[si_].lo, pi.win[si_].hi, pi.ka, pi.kb, 0.0, pi.sparseP[si_]};
        if (i == kFmIndex) {
            const double m = fmMaxIncoming();
            if (m > 1e-9) w.hi = std::min(w.hi, kFmIndexCap[si_] / m);
            w.lo = std::min(w.lo, w.hi);
        }
        w.center = std::clamp(pi.defaultV, w.lo, w.hi);
        return w;
    }

    // -- writes --
    float snap(int i, double v) const {
        const auto& pi = kParamTable[static_cast<std::size_t>(i)];
        if (!(v == v)) v = pi.defaultV;
        v = std::clamp(v, pi.minV, pi.maxV);
        if (pi.type == ParamType::Bool) v = v >= 0.5 ? 1.0 : 0.0;
        else if (pi.type == ParamType::Choice) v = std::clamp(std::floor(v + 0.5), 0.0, static_cast<double>(pi.numChoices - 1));
        else if (pi.step > 0.0) v = pi.minV + std::floor((v - pi.minV) / pi.step + 0.5) * pi.step;
        float f = static_cast<float>(v);
        f = std::clamp(f, static_cast<float>(pi.minV), static_cast<float>(pi.maxV));
        return f;
    }
    void put(int i, double v) { work_[static_cast<std::size_t>(i)] = snap(i, v); written_.set(static_cast<std::size_t>(i)); }

    Pcg32 stream(int i) const { return Pcg32(aseed_, kParamTable[static_cast<std::size_t>(i)].hash); }
    static void draw(Pcg32& g, double (&u)[4]) { for (double& x : u) x = g.nextUnit(); }

    // -- distributions (unit space, deterministic) --
    static double sampleFull(const EW& w, const double (&u)[4]) {
        const double span = w.hi - w.lo;
        switch (w.d) {
            case Dist::Uniform: return w.lo + span * u[0];
            case Dist::Log:
                if (w.lo > 0.0 && w.hi > w.lo) { const double l = log2Det(w.lo); return exp2Det(l + u[0] * (log2Det(w.hi) - l)); }
                return w.lo + span * u[0];
            case Dist::Kumaraswamy: {
                const double inner = 1.0 - powDet(1.0 - u[0], 1.0 / w.kb);
                const double t = w.ka == 1.0 ? inner : powDet(inner, 1.0 / w.ka);
                return w.lo + span * t;
            }
            case Dist::Centered: {
                const double t = u[1] - u[2];
                return t >= 0.0 ? w.center + t * (w.hi - w.center) : w.center + t * (w.center - w.lo);
            }
            case Dist::Sparse:
                if (u[0] < w.p) return (w.lo + 0.6 * span) + (1.0 - 0.6) * span * u[1];
                return std::clamp(0.0, w.lo, w.hi);
            case Dist::Choice: {
                const double lo = std::ceil(w.lo), hi = std::floor(w.hi);
                const double n = hi - lo + 1.0;
                if (n < 1.0) return lo;
                return lo + std::min(std::floor(u[0] * n), n - 1.0);
            }
            case Dist::Bool: return u[0] < 0.5 ? 1.0 : 0.0;
            default: return w.lo;
        }
    }
    static double toT(const EW& w, double x) {
        const double span = w.hi - w.lo;
        if (!(span > 0.0)) return 0.5;
        if (w.d == Dist::Log && w.lo > 0.0) { const double l = log2Det(w.lo); return (log2Det(x) - l) / (log2Det(w.hi) - l); }
        return (x - w.lo) / span;
    }
    static double fromT(const EW& w, double t) {
        const double span = w.hi - w.lo;
        if (!(span > 0.0)) return w.lo;
        if (w.d == Dist::Log && w.lo > 0.0) { const double l = log2Det(w.lo); return exp2Det(l + t * (log2Det(w.hi) - l)); }
        return w.lo + t * span;
    }
    static double reflect01(double t) {
        for (int k = 0; k < 4 && (t < 0.0 || t > 1.0); ++k) t = t < 0.0 ? -t : 2.0 - t;
        return std::clamp(t, 0.0, 1.0);
    }

    // -- Full: chain structure (spec 5.1, 5.2, 5.3) --
    int pickWeighted(Pcg32& g, const double* w, int n) const {
        double total = 0;
        for (int k = 0; k < n; ++k) total += w[k];
        const double r = g.nextUnit();
        if (!(total > 0.0)) return -1;
        double acc = 0, target = r * total;
        int last = -1;
        for (int k = 0; k < n; ++k) {
            if (w[k] <= 0.0) continue;
            last = k;
            acc += w[k];
            if (target < acc) return k;
        }
        return last;
    }

    void chainStructure() {
        if (!graphOn()) return;
        const int cap = kActiveCap[si_];
        std::array<bool, kSlotCount> fixed{};
        int lockedActive = 0;
        std::array<int, kModuleTypeCount> typeCount{};
        int hr = 0, fz = 0;
        for (int s = 0; s < kSlotCount; ++s) {
            fixed[static_cast<std::size_t>(s)] = !(canWrite(nodeParam(s, 0)) && canWrite(nodeParam(s, 1)));
            if (fixed[static_cast<std::size_t>(s)] && slotActive(s)) {
                ++lockedActive;
                const int t = moduleType(s);
                ++typeCount[static_cast<std::size_t>(t)];
                hr += isHighRisk(t); fz += isFreezeType(t);
            }
        }
        const double uCount = rs_.nextUnit();
        const int target = 2 + static_cast<int>(std::floor(uCount * static_cast<double>(cap - 2 + 1)));
        int add = std::max(0, target - lockedActive);
        // candidate positions: free slots among the first eight, weights 8..1 (earlier slots preferred)
        std::array<int, kSlotCount> pos{};
        int nPos = 0;
        for (int s = 0; s < 8; ++s) if (!fixed[static_cast<std::size_t>(s)]) pos[static_cast<std::size_t>(nPos++)] = s;
        std::array<bool, kSlotCount> chosen{};
        add = std::min(add, nPos);
        for (int k = 0; k < add; ++k) {
            double w[8]; int idx[8]; int m = 0;
            for (int j = 0; j < nPos; ++j) if (!chosen[static_cast<std::size_t>(pos[static_cast<std::size_t>(j)])]) { w[m] = static_cast<double>(8 - pos[static_cast<std::size_t>(j)]); idx[m++] = pos[static_cast<std::size_t>(j)]; }
            const int pk = pickWeighted(rs_, w, m);
            if (pk >= 0) chosen[static_cast<std::size_t>(idx[pk])] = true;
        }
        std::array<int, kSlotCount> newType{};
        int lastChosen = -1;
        for (int s = 0; s < kSlotCount; ++s) {
            if (!chosen[static_cast<std::size_t>(s)]) continue;
            double w[kModuleTypeCount];
            w[0] = 0.0;
            for (int t = 1; t < kModuleTypeCount; ++t) {
                bool ok = typeCount[static_cast<std::size_t>(t)] < kRepeatCap[si_];
                if (ok && isHighRisk(t) && hr >= kHighRiskCap[si_]) ok = false;
                if (ok && isFreezeType(t) && fz >= kFreezeTypeCap[si_]) ok = false;
                w[t] = ok ? 1.0 : 0.0;
            }
            int t = pickWeighted(rs_, w, kModuleTypeCount);
            if (t < 1) t = 1;
            newType[static_cast<std::size_t>(s)] = t;
            ++typeCount[static_cast<std::size_t>(t)];
            hr += isHighRisk(t); fz += isFreezeType(t);
            lastChosen = s;
        }
        // a space / diffuser / chorus module with probability 0.6 (spec 5.2, a listening prior)
        const double uSpace = rs_.nextUnit();
        if (lastChosen >= 0 && uSpace < 0.6) {
            bool has = false;
            for (int t = 1; t < kModuleTypeCount; ++t) if (isSpaceLike(t) && typeCount[static_cast<std::size_t>(t)] > 0) has = true;
            if (!has) {
                const int old = newType[static_cast<std::size_t>(lastChosen)];
                --typeCount[static_cast<std::size_t>(old)];
                hr -= isHighRisk(old); fz -= isFreezeType(old);
                static constexpr int space[3] = {4, 15, 20};
                const int t = space[static_cast<int>(std::floor(rs_.nextUnit() * 3.0)) % 3];
                newType[static_cast<std::size_t>(lastChosen)] = t;
                ++typeCount[static_cast<std::size_t>(t)];
            }
        }
        for (int s = 0; s < kSlotCount; ++s) {
            if (fixed[static_cast<std::size_t>(s)]) continue;
            if (chosen[static_cast<std::size_t>(s)]) { put(nodeParam(s, 1), 1.0); put(nodeParam(s, 0), newType[static_cast<std::size_t>(s)]); }
            else { put(nodeParam(s, 1), 0.0); put(nodeParam(s, 0), 0.0); }
        }
        // routing algorithm: Safe = the 20 feedback-free algorithms, Normal weights 1 / 0.5, Wild uniform 0..31 (32 is not used)
        if (canWrite(kRoutingAlgorithm)) {
            double w[32];
            for (int a = 0; a < 32; ++a) {
                const bool fb = routeAlgoInfo(a).feedbackArcs > 0;
                w[a] = si_ == 0 ? (fb ? 0.0 : 1.0) : (si_ == 1 ? (fb ? 0.5 : 1.0) : 1.0);
            }
            int a = pickWeighted(rs_, w, 32);
            put(kRoutingAlgorithm, a < 0 ? 0 : a);
        }
    }

    // -- Full: FM / Source structure (spec 4.2, 5.4, 5.5) --
    void fmSourceStructure() {
        if (!fmSrcOpen_) return;
        const bool fmW = canWrite(kFmEnabled);
        const bool freeSrc = canWrite(kSourceEnabled) && canWrite(kSourceEngine);
        const double u = rs_.nextUnit(), u2 = rs_.nextUnit(), u3 = rs_.nextUnit();
        bool wantFm = fmOn(), wantSrc = srcOn();
        if (mode_ == 2) {
            int combo = 0;  // 0 FM only, 1 Source only, 2 both
            if (si_ == 0) combo = u < 0.3 ? 2 : 0;
            else if (si_ == 1) combo = u < 0.5 ? 0 : (u < 0.65 ? 1 : 2);
            else combo = u < 0.35 ? 0 : (u < 0.65 ? 1 : 2);
            if (fmW) wantFm = combo != 1;
            if (freeSrc) wantSrc = combo != 0;
            if (!wantFm && !wantSrc) {
                if (fmW) wantFm = true;
                else if (freeSrc) wantSrc = true;
                else { failReason_ = "no-sound-source"; return; }
            }
            if (fmW) put(kFmEnabled, wantFm ? 1.0 : 0.0);
            if (freeSrc) {
                if (wantSrc) { put(kSourceEnabled, 1.0); put(kSourceEngine, u2 < (si_ == 0 ? 0.7 : 0.5) ? 1.0 : 2.0); }
                else put(kSourceEnabled, 0.0);
            }
        } else if (mode_ == 1) {
            if (canWrite(kSourceEngine)) put(kSourceEngine, u2 < (si_ == 0 ? 0.7 : 0.5) ? 1.0 : 2.0);
        }
        if (fmOn() && canWrite(kFmAlgorithm)) {
            double w[32];
            for (int a = 0; a < 32; ++a) w[a] = (si_ == 0 && fmAlgoInfo(a).hasCycle) ? 0.0 : 1.0;
            const double saved = u3;  // keeps the draw count fixed
            (void)saved;
            int a = pickWeighted(rs_, w, 32);
            put(kFmAlgorithm, a < 0 ? 0 : a);
        }
        if (fmOn() && canWrite(kFmCustom)) put(kFmCustom, 0.0);  // custom matrices are not generated (frozen 9.1.1 #3)
    }

    // -- value sampling --
    bool isRatio(int i) const { return i >= kOpOffset && i < kEdgeOffset && (i - kOpOffset) % 6 == 0; }
    std::span<const double> ratioSet(int op) const {
        if ((carrierMask() >> op) & 1u) return std::span<const double>(kRatioCarrier);
        if (si_ == 0) return std::span<const double>(kRatioSafe);
        if (si_ == 1) return std::span<const double>(kRatioNormal);
        return std::span<const double>(kRatioWild);
    }
    bool handledByMotionStage(const ParamInfo& pi) const { return pi.group == Group::Motion; }

    void sampleFullValue(int i) {
        const auto& pi = kParamTable[static_cast<std::size_t>(i)];
        if (pi.cls != Classification::Random || !canWrite(i) || handledByMotionStage(pi) || !isActive(i)) return;
        auto g = stream(i);
        double u[4];
        draw(g, u);
        if (isRatio(i)) {
            const int op = (i - kOpOffset) / 6;
            const auto set = ratioSet(op);
            const unsigned cm = carrierMask();
            const bool anchor = cm != 0 && op == static_cast<int>(std::countr_zero(cm));
            put(i, anchor ? 1.0 : set[static_cast<std::size_t>(std::min(static_cast<int>(std::floor(u[0] * static_cast<double>(set.size()))), static_cast<int>(set.size()) - 1))]);
            return;
        }
        put(i, sampleFull(window(i), u));
    }

    void sampleMutateValue(int i, double r, double p, double a) {
        const auto& pi = kParamTable[static_cast<std::size_t>(i)];
        if (pi.cls != Classification::Random || !canWrite(i) || pi.role == Role::ModDepth || !isActive(i)) return;
        auto g = stream(i);
        double u[4];
        draw(g, u);
        const double cur = get(i);
        if (isRatio(i)) {
            if (!(u[0] < (1.0 - p) * a)) return;
            const auto set = ratioSet((i - kOpOffset) / 6);
            put(i, set[static_cast<std::size_t>(std::min(static_cast<int>(std::floor(u[3] * static_cast<double>(set.size()))), static_cast<int>(set.size()) - 1))]);
            return;
        }
        const EW w = window(i);
        switch (w.d) {
            case Dist::Bool:
                if (u[0] < (1.0 - p) * a) put(i, cur >= 0.5 ? 0.0 : 1.0);
                return;
            case Dist::Choice: {
                if (!(u[0] < (1.0 - p) * a)) return;
                const double u4[4] = {u[3], 0, 0, 0};
                put(i, sampleFull(w, u4));
                return;
            }
            case Dist::Sparse: {
                if (!(u[0] >= p)) return;
                if (std::abs(cur) < 1e-9) { if (!(w.p > 0.0)) return; const double u4[4] = {0.0, u[3], 0, 0}; EW w2 = w; w2.p = 2.0; put(i, sampleFull(w2, u4)); }
                else put(i, std::clamp(0.0, w.lo, w.hi));
                return;
            }
            case Dist::Uniform: case Dist::Log: case Dist::Kumaraswamy: case Dist::Centered: {
                if (!(u[0] >= p)) return;
                const double x = std::clamp(cur, w.lo, w.hi);
                const double step = (w.d == Dist::Kumaraswamy || w.d == Dist::Centered) ? 0.8 : 1.0;
                const double delta = r * step * (u[1] - u[2]);
                put(i, fromT(w, reflect01(toT(w, x) + delta)));
                return;
            }
            default: return;
        }
    }

    // -- motion: LFOs and modulation rows --
    bool rowLocked(int r) const {
        for (int f = 0; f < 3; ++f) if (locks_.isLocked(modParam(r, f))) return true;
        return false;
    }
    bool rowWritable(int r) const {
        for (int f = 0; f < 3; ++f) if (!canWrite(modParam(r, f))) return false;
        return true;
    }

    void modulationRowsFull() {
        if (!graphOn()) return;
        for (int r = 0; r < kModRowCount; ++r)
            if (rowWritable(r)) { put(modParam(r, 0), 0.0); put(modParam(r, 1), 0.0); put(modParam(r, 2), 0.0); }
        int liveLocked = 0;
        for (int r = 0; r < kModRowCount; ++r) liveLocked += rowLive(r);
        const double uRows = rm_.nextUnit();
        const int total = kRowMin[si_] + static_cast<int>(std::floor(uRows * static_cast<double>(kRowCap[si_] - kRowMin[si_] + 1)));
        int need = std::max(0, total - liveLocked);
        for (int r = 0; r < 12 && need > 0; ++r) {
            const double uS = rm_.nextUnit(), uT = rm_.nextUnit();
            (void)uS; (void)uT;
            if (!rowWritable(r)) continue;
            // candidate targets on the current working state
            int cand[112]; int nc = 0;
            for (int t = 1; t <= 112; ++t) {
                if (si_ == 0 && t > 84) continue;
                if (t == 87 && si_ < 2) continue;
                if (!targetValid(t)) continue;
                if (category(t) == Cat::Frozen && si_ == 0) continue;
                cand[nc++] = t;
            }
            if (nc == 0) break;
            bool placed = false;
            double uSrcv = uS, uTgtv = uT;
            for (int attempt = 0; attempt < 8 && !placed; ++attempt) {
                if (attempt > 0) { uSrcv = rm_.nextUnit(); uTgtv = rm_.nextUnit(); }
                const int src = 1 + std::min(static_cast<int>(std::floor(uSrcv * 9.0)), 8);
                const int tgt = cand[std::min(static_cast<int>(std::floor(uTgtv * static_cast<double>(nc))), nc - 1)];
                bool dup = false;
                for (int q = 0; q < kModRowCount; ++q) if (q != r && rowLive(q) && rowSource(q) == src && rowTarget(q) == tgt) dup = true;
                if (dup) continue;
                const double budget = depthBudget(tgt, r);
                if (budget < kMinDepth) continue;
                auto g = stream(modParam(r, 2));
                double u[4];
                draw(g, u);
                const double cap = std::min(kDepthCap[static_cast<int>(category(tgt))][si_], budget);
                const double tri = u[1] - u[2];
                double mag = std::max(kMinDepth, std::abs(tri) * cap);
                mag = std::min(mag, std::max(kMinDepth, cap));
                put(modParam(r, 0), src);
                put(modParam(r, 1), tgt);
                put(modParam(r, 2), tri >= 0.0 ? mag : -mag);
                placed = true;
            }
            if (placed) --need;
        }
    }

    void lfoValues(bool mutate, double r, double p, double a) {
        for (int l = 0; l < 4; ++l)
            for (int f = 0; f < 3; ++f) {
                const int i = lfoParam(l, f);
                if (!canWrite(i) || !isActive(i)) continue;
                if (mutate) sampleMutateValue(i, r, p, a);
                else {
                    auto g = stream(i);
                    double u[4];
                    draw(g, u);
                    put(i, sampleFull(window(i), u));
                }
            }
    }

    void mutateDepths(double r, double p) {
        if (!graphOn()) return;
        for (int row = 0; row < kModRowCount; ++row) {
            const int i = modParam(row, 2);
            if (!canWrite(i) || !isActive(i) || !targetValid(rowTarget(row))) continue;
            auto g = stream(i);
            double u[4];
            draw(g, u);
            if (!(u[0] >= p)) continue;
            const int tgt = rowTarget(row);
            const double budget = depthBudget(tgt, row);
            if (budget < kMinDepth) continue;
            const double cap = std::min(kDepthCap[static_cast<int>(category(tgt))][si_], budget);
            double x = std::clamp(get(i), -cap, cap);
            double t = (x + cap) / (2.0 * cap);
            t = reflect01(t + r * 0.8 * (u[1] - u[2]));
            double nv = -cap + t * 2.0 * cap;
            if (std::abs(nv) < kMinDepth) nv = nv < 0.0 ? -kMinDepth : kMinDepth;
            put(i, nv);
        }
    }

    // -- drivers --
    void runFull() {
        if (cfg_.scopeMask & ScopeChain) chainStructure();
        if (cfg_.scopeMask & (ScopeFM | ScopeSource)) fmSourceStructure();
        if (failReason_) return;
        if (canWrite(kGraphFeedback) && isActive(kGraphFeedback)) {
            auto g = stream(kGraphFeedback);
            double u[4];
            draw(g, u);
            put(kGraphFeedback, sampleFull(window(kGraphFeedback), u));
        }
        sampleFullValue(kFmMorph);  // before fm.index: the index cap looks at the morph neighbour
        for (int i = 0; i < kParamCount; ++i) {
            if (i == kFmMorph || i == kGraphFeedback) continue;
            sampleFullValue(i);
        }
        if (cfg_.scopeMask & ScopeMotion) { modulationRowsFull(); lfoValues(false, 0, 0, 0); }
    }
    void runMutate(double a) {
        const double r = 0.35 * powDet(a, 0.8);
        const double p = 0.25 + 0.65 * powDet(1.0 - a, 1.5);
        sampleMutateValue(kFmMorph, r, p, a);
        for (int i = 0; i < kParamCount; ++i) {
            if (i == kFmMorph) continue;
            const auto& pi = kParamTable[static_cast<std::size_t>(i)];
            if (pi.group == Group::Motion) continue;
            sampleMutateValue(i, r, p, a);
        }
        if (cfg_.scopeMask & ScopeMotion) { mutateDepths(r, p); lfoValues(true, r, p, a); }
    }

    Patch finish() {
        Patch out;
        for (int i = 0; i < kParamCount; ++i) {
            if (!written_.test(static_cast<std::size_t>(i))) continue;
            if (bitsOf(work_[static_cast<std::size_t>(i)]) == bitsOf(raw_[static_cast<std::size_t>(i)])) continue;
            out.diffs.push_back({i, work_[static_cast<std::size_t>(i)]});
        }
        out.report.activeSlots = activeSlots();
        out.report.algorithm = algorithm();
        for (int r = 0; r < kModRowCount; ++r) out.report.modulationRows += rowLive(r);
        if (failReason_) { out.diffs.clear(); out.report.ok = false; out.report.reason = failReason_; }
        return out;
    }
    const char* failReason() const { return failReason_; }
    bool anyGraphScope() const { return (cfg_.scopeMask & (ScopeChain | ScopeModuleParams | ScopeMotion)) != 0; }

private:
    Config cfg_;
    const LockSet& locks_;
    EditionContext edition_;
    std::array<float, kParamCount> raw_;
    std::array<float, kParamCount> work_{};
    std::bitset<kParamCount> written_;
    int mode_;
    int si_;
    std::uint64_t aseed_;
    bool fmSrcOpen_ = false;
    Pcg32 rs_, rm_;
    const char* failReason_ = nullptr;
};

}  // namespace

Patch generate(const StateView& in, const Config& cfg, const LockSet& locks) noexcept {
    try {
        if (cfg.mode == Mode::Mutate && !(cfg.amount > 0.f)) {
            // Mutate with amount == 0 (or NaN / negative): empty patch, no sampling at all (spec 6.2).
            Engine e(in, cfg, locks);
            Patch p = e.finish();
            p.report.reason = "mutate-amount-0";
            return p;
        }
        Engine e(in, cfg, locks);
        if (cfg.mode == Mode::Full) e.runFull();
        else e.runMutate(std::min(static_cast<double>(cfg.amount), 1.0));
        Patch p = e.finish();
        if (!p.report.ok) return p;
        if (cfg.mode == Mode::Full && p.diffs.empty() && !e.graphOn() && e.anyGraphScope()) {
            p.report.ok = false;
            p.report.reason = "graph-disabled";
        }
        const Patch::Report v = validate(in, p, cfg, locks);
        if (!v.ok) { p.diffs.clear(); p.report = v; }
        return p;
    } catch (...) {
        Patch p;
        p.report.ok = false;
        p.report.reason = "internal-error";
        return p;
    }
}

Patch::Report validate(const StateView& before, const Patch& patch, const Config& cfg, const LockSet& locks) {
    Patch::Report rep = patch.report;
    auto fail = [&](const char* why) { rep.ok = false; rep.reason = why; return rep; };
    Engine ev(before, cfg, locks);
    const auto result = applyPatch(before, patch);
    int prev = -1;
    for (const auto& d : patch.diffs) {
        if (d.specIndex <= prev || d.specIndex >= kParamCount) return fail("diffs-not-ascending-or-out-of-range");
        prev = d.specIndex;
        const auto& pi = kParamTable[static_cast<std::size_t>(d.specIndex)];
        if (!std::isfinite(d.value)) return fail("non-finite-value");
        if (d.value < static_cast<float>(pi.minV) || d.value > static_cast<float>(pi.maxV)) return fail("value-out-of-range");
        if (pi.cls == Classification::Never) return fail("never-parameter-written");
        if (d.specIndex == kGraphTrim) return fail("trim-written-by-engine");
        if (locks.isLocked(d.specIndex)) return fail("locked-parameter-written");
        if (!(groupBit(pi.group) & cfg.scopeMask)) return fail("outside-scope");
        if (ev.groupClosed(pi)) return fail("edition-closed-section-written");
        if (pi.type != ParamType::Float && d.value != std::floor(d.value)) return fail("discrete-value-not-integer");
        if (pi.step > 0.0) {
            const double k = (static_cast<double>(d.value) - pi.minV) / pi.step;
            if (std::abs(k - std::floor(k + 0.5)) > 1e-3) return fail("value-off-step-grid");
        }
    }
    if (cfg.mode == Mode::Mutate && !(cfg.amount > 0.f) && !patch.diffs.empty()) return fail("mutate-amount-0-not-empty");
    // edition: nothing may point a written row at FM/Source targets in FX or in a closed section
    {
        auto sane = result;  // same sanitising as the generator's working copy (NaN / Inf inputs stay harmless)
        for (int i = 0; i < kParamCount; ++i) {
            const auto& pi = kParamTable[static_cast<std::size_t>(i)];
            float v = sane[static_cast<std::size_t>(i)];
            if (!std::isfinite(v)) v = static_cast<float>(pi.defaultV);
            sane[static_cast<std::size_t>(i)] = std::clamp(v, static_cast<float>(pi.minV), static_cast<float>(pi.maxV));
        }
        ev.setWork(sane);
    }
    for (const auto& d : patch.diffs) {
        const int i = d.specIndex;
        if (i >= kModOffset && i < kFmOffset && (i - kModOffset) % 3 == 1 && d.value >= 85.f && (ev.modeValue() == 0 || !ev.fmSrcOpen()))
            return fail("fm-source-target-in-closed-edition");
    }
    // written Random values stay inside their effective window (non-structural, non-motion-row)
    for (const auto& d : patch.diffs) {
        const int i = d.specIndex;
        const auto& pi = kParamTable[static_cast<std::size_t>(i)];
        if (pi.cls != Classification::Random || pi.role == Role::ModDepth) continue;
        if (pi.dist == Dist::None || pi.dist == Dist::Derived || pi.dist == Dist::PerType) {
            if (!(i >= kNodeOffset && i < kRouteOffset)) continue;
        }
        if (i >= kOpOffset && i < kEdgeOffset && (i - kOpOffset) % 6 == 0) continue;  // ratio sets are checked below
        const EW w = ev.window(i);
        if (w.d == Dist::None) continue;
        const double v = d.value, eps = 1e-5 * (1.0 + std::abs(w.hi - w.lo));
        if (w.d == Dist::Sparse) { if (!(std::abs(v) < 1e-9 || (v >= w.lo - eps && v <= w.hi + eps))) return fail("value-outside-window"); }
        else if (v < w.lo - eps || v > w.hi + eps) return fail("value-outside-window");
    }
    for (const auto& d : patch.diffs) {
        const int i = d.specIndex;
        if (i >= kOpOffset && i < kEdgeOffset && (i - kOpOffset) % 6 == 0) {
            const int op = (i - kOpOffset) / 6;
            bool inSet = false;
            for (double s : ev.ratioSet(op)) if (std::abs(static_cast<double>(d.value) - s) < 1e-6) inSet = true;
            if (!inSet) return fail("fm-ratio-outside-set");
        }
    }
    rep.activeSlots = ev.activeSlots();
    rep.algorithm = ev.algorithm();
    rep.modulationRows = 0;
    for (int r = 0; r < kModRowCount; ++r) rep.modulationRows += ev.rowLive(r);
    if (cfg.mode != Mode::Full) return rep;

    // ---- Full: structure rules (spec 7.2) - only for what the engine controls; locked items may exceed caps ----
    const int si = cfg.safety == Safety::Safe ? 0 : (cfg.safety == Safety::Normal ? 1 : 2);
    const bool graph = ev.graphOn();
    if (graph && (cfg.scopeMask & ScopeChain)) {
        int lockedActive = 0, total = 0;
        std::array<int, kModuleTypeCount> cnt{}, lcnt{};
        int hr = 0, lhr = 0, fz = 0, lfz = 0;
        bool anyLocked = false;
        for (int s = 0; s < kSlotCount; ++s) {
            const bool fixed = locks.isLocked(nodeParam(s, 0)) || locks.isLocked(nodeParam(s, 1));
            anyLocked |= fixed;
            if (!ev.slotActive(s)) continue;
            const int t = ev.moduleType(s);
            ++total; ++cnt[static_cast<std::size_t>(t)]; hr += isHighRisk(t); fz += isFreezeType(t);
            if (fixed) { ++lockedActive; ++lcnt[static_cast<std::size_t>(t)]; lhr += isHighRisk(t); lfz += isFreezeType(t); }
        }
        if (total > std::max(kActiveCap[si], lockedActive)) return fail("too-many-active-slots");
        if (!anyLocked && total < 2) return fail("too-few-active-slots");
        for (int t = 1; t < kModuleTypeCount; ++t)
            if (cnt[static_cast<std::size_t>(t)] > std::max(kRepeatCap[si], lcnt[static_cast<std::size_t>(t)])) return fail("module-type-repeat-cap");
        if (hr > std::max(kHighRiskCap[si], lhr)) return fail("high-risk-module-cap");
        if (fz > std::max(kFreezeTypeCap[si], lfz)) return fail("freeze-module-cap");
        for (int s = 0; s < kSlotCount; ++s)  // slots 9..12 are never switched on by the engine
            if (s >= 8 && ev.slotActive(s) && !locks.isLocked(nodeParam(s, 1)) && !locks.isLocked(nodeParam(s, 0)) && bitsOf(before.value[static_cast<std::size_t>(nodeParam(s, 1))]) != bitsOf(result[static_cast<std::size_t>(nodeParam(s, 1))]))
                return fail("slot-beyond-8-activated");
        if (!locks.isLocked(kRoutingAlgorithm) && ev.algorithm() > 31) return fail("algorithm-32-generated");
        if (!locks.isLocked(kRoutingAlgorithm) && si == 0 && routeAlgoInfo(ev.algorithm()).feedbackArcs > 0) return fail("safe-algorithm-with-feedback-arcs");
    }
    if (graph && si < 2) {  // no infinite tail outside Wild: freeze knob + modulation depth < 0.5 (spec 5.2 / 7.2)
        for (int sl = 0; sl < kSlotCount; ++sl) {
            if (!ev.slotActive(sl) || !isFreezeType(ev.moduleType(sl))) continue;
            const int knob = freezeKnob(ev.moduleType(sl));
            if (locks.isLocked(nodeParam(sl, 5 + knob))) continue;
            if (ev.get(nodeParam(sl, 5 + knob)) >= kFreezeThreshold) return fail("freeze-knob-over-threshold");
        }
    }
    if (graph && (cfg.scopeMask & ScopeMotion)) {
        int live = 0, liveLocked = 0;
        for (int r = 0; r < kModRowCount; ++r) {
            const bool lockedRow = ev.rowLocked(r);
            if (ev.rowLive(r)) { ++live; liveLocked += lockedRow; }
            if (lockedRow) continue;
            const int src = ev.rowSource(r), tgt = ev.rowTarget(r);
            const double depth = ev.get(modParam(r, 2));
            if (src == 0 && tgt == 0 && depth == 0.0) continue;
            if (r >= 12) return fail("modulation-row-beyond-12");
            if (src == 0 || tgt == 0) return fail("half-empty-modulation-row");
            if (std::abs(depth) < kMinDepth - 1e-6) return fail("dead-modulation-row");
            if (!ev.targetValid(tgt)) return fail("dead-modulation-target");
            if (si == 0 && tgt > 84) return fail("safe-extended-target");
            if (tgt == 87 && si < 2) return fail("transpose-target-generated");
            const Cat c = ev.category(tgt);
            if (c == Cat::Frozen) {
                if (si == 0) return fail("frozen-knob-targeted-in-safe");
                double sum = 0;
                for (int q = 0; q < kModRowCount; ++q) if (ev.rowLive(q) && ev.rowTarget(q) == tgt) sum += std::abs(ev.get(modParam(q, 2)));
                if (ev.knobValueOfTarget(tgt) + sum >= kFreezeThreshold) return fail("frozen-knob-depth-over-threshold");
            }
            if (std::abs(depth) > kDepthCap[static_cast<int>(c)][si] + 1e-5) return fail("modulation-depth-over-cap");
            double sum = 0, lsum = 0;
            for (int q = 0; q < kModRowCount; ++q) if (ev.rowLive(q) && ev.rowTarget(q) == tgt) { sum += std::abs(ev.get(modParam(q, 2))); if (ev.rowLocked(q)) lsum += std::abs(ev.get(modParam(q, 2))); }
            if (sum > std::max(kDepthCap[static_cast<int>(c)][si], lsum) + 1e-5) return fail("modulation-target-depth-sum-over-cap");
            for (int q = r + 1; q < kModRowCount; ++q)
                if (ev.rowLive(q) && ev.rowSource(q) == src && ev.rowTarget(q) == tgt && !ev.rowLocked(q)) return fail("duplicate-modulation-row");
        }
        if (live > std::max(kRowCap[si], liveLocked)) return fail("too-many-modulation-rows");
    }
    if (ev.modeValue() == 2 && (cfg.scopeMask & (ScopeFM | ScopeSource)) && !ev.fmOn() && !ev.srcOn()) {
        const bool fmFree = !locks.isLocked(kFmEnabled);
        const bool srcFree = !locks.isLocked(kSourceEnabled) && !locks.isLocked(kSourceEngine);
        if (fmFree || srcFree) return fail("synth-without-sound-source");
    }
    if (ev.fmSrcOpen() && ev.fmOn() && (cfg.scopeMask & ScopeFM)) {
        const double m = ev.fmMaxIncoming();
        if (!locks.isLocked(kFmIndex) && m > 1e-9 && ev.get(kFmIndex) * m > kFmIndexCap[si] + 1e-4 && ev.get(kFmIndex) > ev.window(kFmIndex).lo + 1e-6)
            return fail("fm-index-cap");
        if (si == 0 && !locks.isLocked(kFmAlgorithm) && fmAlgoInfo(ev.fmAlgo()).hasCycle) return fail("safe-fm-algorithm-with-cycle");
    }
    return rep;
}

}  // namespace aether::rand
