// AETHER 2.3.0 - randomization: processor-side wiring. See RandomizeController.h for the contract and the reasons behind
// the two design decisions that are easy to get wrong:
//  1. Live face writes use APVTS::replaceState (whole-state replacement, project decision), not a per-parameter gesture loop.
//  2. The undo ring records the PHYSICAL A/B face, so an undo after an A/B toggle still lands on the face it came from.
#include "RandomizeController.h"
#include "PluginProcessor.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace aether::rand {
namespace {

// parameterSpecs() index by parameter id, built once (thread-safe static init).
const std::unordered_map<std::string, int>& indexById() {
    static const std::unordered_map<std::string, int> map = [] {
        std::unordered_map<std::string, int> m;
        const auto& specs = aether::parameterSpecs();
        m.reserve(specs.size());
        for (std::size_t i = 0; i < specs.size(); ++i) m.emplace(specs[i].id.toStdString(), static_cast<int>(i));
        return m;
    }();
    return map;
}
float clampToSpec(int index, float value) {
    const auto& spec = aether::parameterSpecs()[static_cast<std::size_t>(index)];
    return std::clamp(value, spec.minimum, spec.maximum);
}
float defaultOf(int index) { return aether::parameterSpecs()[static_cast<std::size_t>(index)].defaultValue; }

// "value" of a parameter child: finite numbers only, clamped to the spec range, default otherwise.
float parseChildValue(const juce::ValueTree& child, int index) {
    const juce::var v = child.getProperty("value");
    if (v.isVoid()) return defaultOf(index);
    const double d = static_cast<double>(v);
    if (!std::isfinite(d)) return defaultOf(index);
    return clampToSpec(index, static_cast<float>(d));
}
bool bitsEqual(float a, float b) noexcept { return std::memcmp(&a, &b, sizeof a) == 0; }

// ---- recipe <-> text ---------------------------------------------------------------------------------------------------
juce::String hex64(std::uint64_t v) { return juce::String::toHexString(static_cast<juce::int64>(v)); }
bool parseHex64(const juce::String& s, std::uint64_t& out) {
    const auto t = s.trim().toStdString();
    if (t.empty() || t.size() > 16) return false;
    std::uint64_t v = 0;
    const auto r = std::from_chars(t.data(), t.data() + t.size(), v, 16);
    if (r.ec != std::errc{} || r.ptr != t.data() + t.size()) return false;
    out = v;
    return true;
}
juce::String floatText(float v) {  // shortest text that parses back to the same float
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return juce::String(buf, static_cast<std::size_t>(r.ptr - buf));
}
bool parseFloat(const juce::String& s, float& out) {
    const auto t = s.trim().toStdString();
    float v = 0;
    const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
    if (t.empty() || r.ec != std::errc{} || r.ptr != t.data() + t.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}
bool parseInt(const juce::String& s, std::int64_t& out) {
    const auto t = s.trim().toStdString();
    std::int64_t v = 0;
    const auto r = std::from_chars(t.data(), t.data() + t.size(), v);
    if (t.empty() || r.ec != std::errc{} || r.ptr != t.data() + t.size()) return false;
    out = v;
    return true;
}
bool lockSetEmpty(const LockSet& l) noexcept { return l.params.none() && l.sections == 0 && l.modRows == 0; }

void writeRecipe(juce::ValueTree& node, const RandomRecipe& r) {
    node.setProperty("seed", hex64(r.seed), nullptr);
    node.setProperty("algo", r.algoVersion, nullptr);
    node.setProperty("rmode", static_cast<int>(r.mode), nullptr);
    node.setProperty("amount", floatText(r.amount), nullptr);
    node.setProperty("safety", static_cast<int>(r.safety), nullptr);
    node.setProperty("scope", juce::String(static_cast<juce::int64>(r.scopeMask)), nullptr);
    node.setProperty("attempt", r.attempt, nullptr);
    node.setProperty("trim", floatText(r.trimDb), nullptr);
    node.setProperty("intoB", r.intoB, nullptr);
    node.setProperty("edition", r.edition, nullptr);
    node.setProperty("baseline", hex64(r.baselineHash), nullptr);
    node.setProperty("result", hex64(r.resultHash), nullptr);
    node.setProperty("source", juce::String(juce::CharPointer_UTF8(r.sourcePreset.c_str())), nullptr);
    node.setProperty("sourceProgram", r.sourceProgram, nullptr);
    node.setProperty("timeUtcMs", juce::String(static_cast<juce::int64>(r.timeUtcMs)), nullptr);
    node.setProperty("rversion", static_cast<int>(r.version), nullptr);
}
bool readRecipe(const juce::ValueTree& node, RandomRecipe& out) {
    if (!node.hasProperty("seed")) return false;
    RandomRecipe r;
    std::int64_t i = 0;
    if (!parseHex64(node["seed"].toString(), r.seed)) return false;
    if (!parseInt(node["algo"].toString(), i)) return false;
    r.algoVersion = static_cast<int>(i);
    if (!parseInt(node["rmode"].toString(), i) || i < 0 || i > 1) return false;
    r.mode = static_cast<Mode>(i);
    if (!parseFloat(node["amount"].toString(), r.amount)) return false;
    if (!parseInt(node["safety"].toString(), i) || i < 0 || i > 2) return false;
    r.safety = static_cast<Safety>(i);
    if (!parseInt(node["scope"].toString(), i) || i < 0 || i > 0xffffffffLL) return false;
    r.scopeMask = static_cast<std::uint32_t>(i);
    if (!parseInt(node["attempt"].toString(), i)) return false;
    r.attempt = static_cast<int>(i);
    if (!parseFloat(node["trim"].toString(), r.trimDb)) return false;
    r.intoB = static_cast<bool>(node.getProperty("intoB", false));
    if (!parseInt(node["edition"].toString(), i)) return false;
    r.edition = static_cast<int>(i);
    if (!parseHex64(node["baseline"].toString(), r.baselineHash) || !parseHex64(node["result"].toString(), r.resultHash)) return false;
    r.sourcePreset = node["source"].toString().toStdString();
    if (!parseInt(node["sourceProgram"].toString(), i)) return false;
    r.sourceProgram = static_cast<int>(i);
    if (!parseInt(node["timeUtcMs"].toString(), i)) return false;
    r.timeUtcMs = i;
    if (!parseInt(node["rversion"].toString(), i) || i < 1 || i > 255) return false;
    r.version = static_cast<std::uint8_t>(i);
    r.valid = true;
    out = r;
    return true;
}

}  // namespace

RandomizeController::RandomizeController(AetherAudioProcessor& owner) noexcept : proc_(owner) {}

bool RandomizeController::onMessageThread() const noexcept {
    // Reject only when a MessageManager exists and this is not its thread (headless tools without one are allowed).
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    return mm == nullptr || mm->isThisTheMessageThread();
}

Face RandomizeController::liveFace() const noexcept { return proc_.snapshotB.load() ? Face::B : Face::A; }

bool RandomizeController::panicPending() const noexcept { return proc_.panic.load(std::memory_order_relaxed); }

// ---- snapshot read ---------------------------------------------------------------------------------------------------
StateView RandomizeController::captureSnapshot(Face face) const {
    StateView view;
    if (face == liveFace()) {
        for (int i = 0; i < kParamCount; ++i) {
            const float v = proc_.allRaw[static_cast<std::size_t>(i)]->load(std::memory_order_relaxed);
            view.value[static_cast<std::size_t>(i)] = std::isfinite(v) ? v : defaultOf(i);
        }
    } else {
        for (int i = 0; i < kParamCount; ++i) view.value[static_cast<std::size_t>(i)] = defaultOf(i);
        const juce::ScopedLock lock(proc_.metadataLock);
        const auto& map = indexById();
        for (int c = 0; c < proc_.snapshot.getNumChildren(); ++c) {
            const auto child = proc_.snapshot.getChild(c);
            const auto it = map.find(child.getProperty("id").toString().toStdString());
            if (it != map.end()) view.value[static_cast<std::size_t>(it->second)] = parseChildValue(child, it->second);
        }
    }
    view.edition.effectiveMode = proc_.effectiveMode(view.value[static_cast<std::size_t>(aether::mode)]);
    view.edition.isSynth = view.edition.effectiveMode == 2;
    return view;
}

std::vector<RandomizeController::Diff> RandomizeController::effectiveDiffs(const std::array<float, kParamCount>& before, std::span<const Patch::Diff> diffs) const {
    std::vector<Diff> out;
    out.reserve(diffs.size());
    for (const auto& d : diffs) {
        if (d.specIndex < 0 || d.specIndex >= kParamCount || !std::isfinite(d.value)) continue;
        const float v = clampToSpec(d.specIndex, d.value);
        if (!bitsEqual(v, before[static_cast<std::size_t>(d.specIndex)])) out.push_back({d.specIndex, v});
    }
    return out;
}

// ---- the one writer ----------------------------------------------------------------------------------------------------
// Face == live: copyState -> set "value" of the changed children -> state.replaceState(tree)  (whole-state replacement).
// Face != live: the `snapshot` tree is edited in place under metadataLock; the host never hears about it.
Outcome RandomizeController::writeFace(Face face, std::span<const Diff> diffs, bool allowPanic) {
    Outcome o;
    o.face = face;
    o.wroteLive = face == liveFace();
    o.changedParams = static_cast<int>(diffs.size());
    Patch changed;
    changed.diffs.reserve(diffs.size());
    for (const auto& d : diffs) changed.diffs.push_back({d.index, d.value});
    o.structural = patchIsStructural(changed);
    if (diffs.empty()) { o.ok = true; return o; }

    const auto& map = indexById();
    auto apply = [&](juce::ValueTree& tree) {
        std::array<juce::ValueTree, kParamCount> child{};
        for (int c = 0; c < tree.getNumChildren(); ++c) {
            auto node = tree.getChild(c);
            const auto it = map.find(node.getProperty("id").toString().toStdString());
            if (it != map.end()) child[static_cast<std::size_t>(it->second)] = node;
        }
        for (const auto& d : diffs) {
            auto& node = child[static_cast<std::size_t>(d.index)];
            if (node.isValid()) node.setProperty("value", static_cast<double>(d.value), nullptr);
        }
    };

    if (o.wroteLive) {
        auto tree = proc_.state.copyState();
        apply(tree);
        proc_.state.replaceState(tree);
        if (allowPanic && o.structural) { proc_.requestPanic(); o.panicRequested = true; }
    } else {
        const juce::ScopedLock lock(proc_.metadataLock);
        if (!proc_.snapshot.isValid()) { o.ok = false; o.reason = "no-alternate"; o.changedParams = 0; return o; }
        apply(proc_.snapshot);
    }
    o.ok = true;
    return o;
}

Outcome RandomizeController::applySnapshot(Face face, const std::array<float, kParamCount>& values) {
    if (!onMessageThread()) { Outcome o; o.reason = "not-message-thread"; return o; }
    const auto before = captureSnapshot(face);
    std::vector<Patch::Diff> want;
    want.reserve(kParamCount);
    for (int i = 0; i < kParamCount; ++i) want.push_back({i, values[static_cast<std::size_t>(i)]});
    const auto diffs = effectiveDiffs(before.value, want);
    return writeFace(face, diffs, true);
}
Outcome RandomizeController::applySnapshotToB(const std::array<float, kParamCount>& values) { return applySnapshot(Face::B, values); }

// ---- random commit -----------------------------------------------------------------------------------------------------
Outcome RandomizeController::randomize(const Config& cfg, const LockSet& locks) {
    if (!onMessageThread()) { Outcome o; o.reason = "not-message-thread"; return o; }
    const Face target = cfg.intoB ? Face::B : liveFace();
    const StateView view = captureSnapshot(target);
    const Patch patch = generate(view, cfg, locks);
    if (!patch.report.ok) { Outcome o; o.face = target; o.wroteLive = target == liveFace(); o.reason = patch.report.reason; return o; }
    return commitRandomPatch(patch, cfg, 0.f);
}

Outcome RandomizeController::commitRandomPatch(const Patch& patch, const Config& cfg, float trimDb) {
    Outcome fail;
    if (!onMessageThread()) { fail.reason = "not-message-thread"; return fail; }
    if (!patch.report.ok) { fail.reason = patch.report.reason; return fail; }
    const Face target = cfg.intoB ? Face::B : liveFace();
    const bool live = target == liveFace();
    const StateView before = captureSnapshot(target);
    const auto diffs = effectiveDiffs(before.value, patch.diffs);
    if (diffs.empty()) { Outcome o; o.ok = true; o.face = target; o.wroteLive = live; return o; }

    // (2) push the whole target face BEFORE writing
    juce::String prevName;
    { const juce::ScopedLock lock(proc_.metadataLock); prevName = proc_.presetName; }
    const int prevProgram = proc_.currentProgram.load();
    const std::string prevNameUtf8 = prevName.toStdString();
    RandomRecipe previousRecipe;
    {
        const juce::ScopedLock lock(dataLock_);
        previousRecipe = recipe_;
        RandomSnapshot entry;
        entry.face = target;
        entry.values = before.value;
        entry.presetName = prevNameUtf8;
        entry.recipe = recipe_;
        entry.program = prevProgram;
        entry.nameChanged = live;
        ring_.push(std::move(entry));
    }

    // (3) write
    Outcome o = writeFace(target, diffs, true);

    // (4) recipe (+ name for the live face)
    const StateView after = captureSnapshot(target);
    RandomRecipe r;
    r.version = kRecipeVersion;
    r.seed = cfg.seed & ((1ULL << 40) - 1);
    r.algoVersion = kRandAlgoVersion;
    r.mode = cfg.mode;
    r.amount = cfg.amount;
    r.safety = cfg.safety;
    r.scopeMask = cfg.scopeMask;
    r.attempt = cfg.attempt;
    r.trimDb = trimDb;
    r.intoB = cfg.intoB;
    r.edition = before.edition.effectiveMode;
    r.baselineHash = hashValues(before.value);
    r.resultHash = hashValues(after.value);
    // The source preset is remembered across re-rolls: when the current name is still the previous random name, keep ITS source.
    if (previousRecipe.valid && !previousRecipe.sourcePreset.empty() && prevNameUtf8 == randomPresetName(previousRecipe.sourcePreset, previousRecipe.seed)) {
        r.sourcePreset = previousRecipe.sourcePreset;
        r.sourceProgram = previousRecipe.sourceProgram;
    } else {
        r.sourcePreset = prevNameUtf8;
        r.sourceProgram = prevProgram;
    }
    r.timeUtcMs = juce::Time::currentTimeMillis();
    r.valid = true;
    { const juce::ScopedLock lock(dataLock_); recipe_ = r; }
    if (live) {
        const auto name = randomPresetName(r.sourcePreset, r.seed);
        const juce::ScopedLock lock(proc_.metadataLock);
        proc_.presetName = juce::String(juce::CharPointer_UTF8(name.c_str()));
    }
    return o;
}

// ---- undo --------------------------------------------------------------------------------------------------------------
Outcome RandomizeController::undoRandom() {
    Outcome o;
    if (!onMessageThread()) { o.reason = "not-message-thread"; return o; }
    RandomSnapshot entry;
    {
        const juce::ScopedLock lock(dataLock_);
        if (!ring_.canUndo()) { o.reason = "no-undo"; return o; }
        entry = *ring_.peek();
    }
    const StateView before = captureSnapshot(entry.face);
    std::vector<Diff> diffs;
    for (int i = 0; i < kParamCount; ++i)
        if (!bitsEqual(entry.values[static_cast<std::size_t>(i)], before.value[static_cast<std::size_t>(i)]))
            diffs.push_back({i, entry.values[static_cast<std::size_t>(i)]});
    o = writeFace(entry.face, diffs, true);
    if (!o.ok) return o;  // entry stays on the ring
    {
        const juce::ScopedLock lock(dataLock_);
        RandomSnapshot discard;
        ring_.pop(discard);
        recipe_ = entry.recipe;
    }
    if (entry.nameChanged) {
        { const juce::ScopedLock lock(proc_.metadataLock); proc_.presetName = juce::String(juce::CharPointer_UTF8(entry.presetName.c_str())); }
        proc_.currentProgram.store(entry.program);
    }
    return o;
}
bool RandomizeController::canUndoRandom() const noexcept { const juce::ScopedLock lock(dataLock_); return ring_.canUndo(); }
int RandomizeController::undoDepth() const noexcept { const juce::ScopedLock lock(dataLock_); return ring_.size(); }
void RandomizeController::clearUndoRandom() noexcept { const juce::ScopedLock lock(dataLock_); ring_.clear(); }
RandomRecipe RandomizeController::recipe() const { const juce::ScopedLock lock(dataLock_); return recipe_; }

// ---- locks + persistence -----------------------------------------------------------------------------------------------
LockSet RandomizeController::locks() const { const juce::ScopedLock lock(dataLock_); return locks_; }
void RandomizeController::setLocks(const LockSet& l) { const juce::ScopedLock lock(dataLock_); locks_ = l; }

void RandomizeController::writeLocksTo(juce::ValueTree& stateCopy) const {
    LockSet l;
    RandomRecipe r;
    { const juce::ScopedLock lock(dataLock_); l = locks_; r = recipe_; }
    const bool haveLocks = !lockSetEmpty(l);
    if (!haveLocks && !r.valid) return;  // nothing to say: the saved state stays byte-identical to a build without this feature
    juce::ValueTree node("random");
    node.setProperty("version", 1, nullptr);
    if (haveLocks) node.setProperty("locks", juce::String(serializeLocks(l)), nullptr);
    if (r.valid) writeRecipe(node, r);
    stateCopy.appendChild(node, nullptr);
}

void RandomizeController::readLocksFrom(const juce::ValueTree& parsedState) {
    LockSet l;
    RandomRecipe r;
    const auto node = parsedState.getChildWithName("random");
    if (node.isValid()) {
        LockSet tmp;
        if (deserializeLocks(node["locks"].toString().toStdString(), tmp)) l = tmp;
        RandomRecipe rr;
        if (readRecipe(node, rr)) r = rr;
    }
    const juce::ScopedLock lock(dataLock_);
    locks_ = l;
    recipe_ = r;
    ring_.clear();
}

}  // namespace aether::rand
