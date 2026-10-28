// AETHER 2.3.0 randomization: processor-level wiring tests (RandomizeController), built once per plugin edition
// (AetherRandWiring = effect/resynth form, AetherRandWiringSynth = instrument form). Run with `--bench` for timing output.
#include "PluginProcessor.h"
#include "RandomizeController.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>
#include <thread>

using namespace aether::rand;

namespace {
int passed = 0, failed = 0;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
void require(bool ok, const juce::String& why) { if (!ok) throw std::runtime_error(why.toStdString()); }
template <class F> void test(const char* name, F f) {
    try { f(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
}
bool bitsEqual(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

using Raw = std::array<float, kParamCount>;
Raw rawOf(AetherAudioProcessor& p) { return p.randomizer().captureSnapshot(p.randomizer().liveFace()).value; }
std::vector<float> normOf(AetherAudioProcessor& p) {
    std::vector<float> v;
    for (const auto& spec : aether::parameterSpecs()) v.push_back(p.state.getParameter(spec.id)->getValue());
    return v;
}
// distance in representable floats (0 = bit-identical); both finite
long long ulpDist(float a, float b) {
    auto key = [](float f) { std::int32_t i; std::memcpy(&i, &f, sizeof i); return i < 0 ? static_cast<long long>(INT32_MIN) - i : static_cast<long long>(i); };
    return std::llabs(key(a) - key(b));
}
long long maxUlp(const Raw& a, const Raw& b) { long long m = 0; for (int i = 0; i < kParamCount; ++i) m = std::max(m, ulpDist(a[static_cast<std::size_t>(i)], b[static_cast<std::size_t>(i)])); return m; }
long long maxUlp(const std::vector<float>& a, const std::vector<float>& b) { long long m = 0; for (std::size_t i = 0; i < a.size(); ++i) m = std::max(m, ulpDist(a[i], b[i])); return m; }
int rawDiffCount(const Raw& a, const Raw& b) { int n = 0; for (int i = 0; i < kParamCount; ++i) if (!bitsEqual(a[static_cast<std::size_t>(i)], b[static_cast<std::size_t>(i)])) ++n; return n; }
int normDiffCount(const std::vector<float>& a, const std::vector<float>& b) { int n = 0; for (std::size_t i = 0; i < a.size(); ++i) if (!bitsEqual(a[i], b[i])) ++n; return n; }
double maxRelDiff(const Raw& a, const Raw& b) {
    double m = 0;
    for (int i = 0; i < kParamCount; ++i) {
        const auto& s = aether::parameterSpecs()[static_cast<std::size_t>(i)];
        const double range = std::max(1e-9, static_cast<double>(s.maximum) - static_cast<double>(s.minimum));
        m = std::max(m, std::abs(static_cast<double>(a[static_cast<std::size_t>(i)]) - b[static_cast<std::size_t>(i)]) / range);
    }
    return m;
}
juce::String nameOf(AetherAudioProcessor& p) { return p.currentPresetName(); }
void setParam(AetherAudioProcessor& p, const char* id, float v) {
    auto* param = p.state.getParameter(id); require(param != nullptr, "parameter missing");
    param->setValueNotifyingHost(param->convertTo0to1(v));
}
void consumePanic(AetherAudioProcessor& p) {  // what the audio thread does at the start of a block
    juce::AudioBuffer<float> b(2, 64); b.clear(); juce::MidiBuffer m; p.processBlock(b, m);
    require(!p.randomizer().panicPending(), "panic flag not consumed");
}
Config fullCfg(std::uint64_t seed, Safety sf = Safety::Safe, bool intoB = false) { Config c; c.seed = seed; c.mode = Mode::Full; c.safety = sf; c.intoB = intoB; return c; }
Config mutCfg(std::uint64_t seed, float amount, bool intoB = false) { Config c; c.seed = seed; c.mode = Mode::Mutate; c.amount = amount; c.intoB = intoB; return c; }
int idx(const char* id) { const int i = paramIndex(id); if (i < 0) throw std::runtime_error(std::string("unknown parameter id in test: ") + id); return i; }

// Counts what a host (or any AudioProcessorListener) sees.
struct Counter final : juce::AudioProcessorListener, juce::AudioProcessorParameter::Listener {
    int changed = 0, gestureBegin = 0, gestureEnd = 0, processorChanged = 0, paramLevel = 0, paramGestures = 0;
    std::set<int> changedIndices;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int i, float) override { ++changed; changedIndices.insert(i); }
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override { ++processorChanged; }
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override { ++gestureBegin; }
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int) override { ++gestureEnd; }
    void parameterValueChanged(int, float) override { ++paramLevel; }
    void parameterGestureChanged(int, bool) override { ++paramGestures; }
    void reset() { changed = gestureBegin = gestureEnd = processorChanged = paramLevel = paramGestures = 0; changedIndices.clear(); }
};
struct Attach {
    AetherAudioProcessor& p; Counter& c;
    Attach(AetherAudioProcessor& proc, Counter& counter) : p(proc), c(counter) {
        p.addListener(&c);
        for (auto* param : p.getParameters()) param->addListener(&c);
    }
    ~Attach() { for (auto* param : p.getParameters()) param->removeListener(&c); p.removeListener(&c); }
};

LockSet someLocks() {
    LockSet l;
    for (const char* id : {"graph.enabled", "routing.algorithm", "node.1.type", "node.1.a", "master.width", "global.output"}) l.params.set(static_cast<std::size_t>(idx(id)));
    l.sections = (kLockSlot0 << 2) | kLockFm;
    l.modRows = 0x0003;
    return l;
}
std::unique_ptr<juce::XmlElement> xmlOf(const juce::MemoryBlock& b) { return juce::AudioProcessor::getXmlFromBinary(b.getData(), static_cast<int>(b.getSize())); }
bool sameBlock(const juce::MemoryBlock& a, const juce::MemoryBlock& b) { return a.getSize() == b.getSize() && std::memcmp(a.getData(), b.getData(), a.getSize()) == 0; }
juce::MemoryBlock saveOf(AetherAudioProcessor& p) { juce::MemoryBlock b; p.getStateInformation(b); return b; }
bool hasRandomNode(const juce::MemoryBlock& b) { auto x = xmlOf(b); require(x != nullptr, "state xml"); return x->getChildByName("random") != nullptr; }
juce::MemoryBlock toBlock(const juce::XmlElement& x) { juce::MemoryBlock b; juce::AudioProcessor::copyXmlToBinary(x, b); return b; }

}  // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    bool bench = false;
    for (int i = 1; i < argc; ++i) if (juce::String(argv[i]) == "--bench") bench = true;
#if JucePlugin_IsSynth
    const char* edition = "Synth";
#else
    const char* edition = "FX/Resynth";
#endif
    std::cout << "edition " << edition << '\n';

    test("random + undo restores the live state, 16 steps (17 commits, the oldest falls off)", [] {
        AetherAudioProcessor p;
        auto& rc = p.randomizer();
        struct Before { Raw raw; std::vector<float> norm; juce::String name; };
        std::vector<Before> before;
        const juce::String original = nameOf(p);
        int rawMismatch = 0, normMismatch = 0; double worst = 0; long long rawUlp = 0, normUlp = 0;
        for (int k = 1; k <= 17; ++k) {
            before.push_back({rawOf(p), normOf(p), nameOf(p)});
            const auto o = rc.randomize(fullCfg(1000 + static_cast<std::uint64_t>(k)), {});
            require(o.ok && o.changedParams > 0 && o.wroteLive && o.face == rc.liveFace(), juce::String("randomize failed at ") + juce::String(k) + ": " + o.reason);
            require(nameOf(p).contains("(Random) "), "preset name not marked random");
        }
        require(rc.undoDepth() == 16 && rc.canUndoRandom(), "ring depth after 17 commits");
        for (int k = 17; k >= 2; --k) {
            const auto o = rc.undoRandom();
            require(o.ok, juce::String("undo failed at ") + juce::String(k) + ": " + o.reason);
            const auto& b = before[static_cast<std::size_t>(k - 1)];
            rawMismatch += rawDiffCount(rawOf(p), b.raw);
            normMismatch += normDiffCount(normOf(p), b.norm);
            worst = std::max(worst, maxRelDiff(rawOf(p), b.raw));
            rawUlp = std::max(rawUlp, maxUlp(rawOf(p), b.raw));
            normUlp = std::max(normUlp, maxUlp(normOf(p), b.norm));
            require(nameOf(p) == b.name, juce::String("name not restored at step ") + juce::String(k) + ": " + nameOf(p) + " vs " + b.name);
        }
        std::cout << "UNDO16 raw-bit mismatches=" << rawMismatch << " normalised-bit mismatches=" << normMismatch << " worst deviation/range=" << worst << " max ulp raw=" << rawUlp << " max ulp normalised=" << normUlp << '\n';
        require(!rc.canUndoRandom() && rc.undoDepth() == 0, "ring not empty after 16 undos");
        require(!rc.undoRandom().ok && std::strcmp(rc.undoRandom().reason, "no-undo") == 0, "undo on empty ring");
        // APVTS cannot give bit identity: its raw atomic only follows a parameter change that is not approximatelyEqual() to the old
        // value, and v -> convertFrom0to1(convertTo0to1(v)) is not idempotent for skewed/snapped floats (the same holds for the plugin's own
        // A/B toggle, see the TOGGLE line). So: every parameter must come back within 4 float ulps (never visible, below JUCE's own
        // "equal" threshold), the counts above are the facts, and everything that is NOT a parameter float (names, ring, recipe) is exact.
        require(rawUlp <= 4, "undo: a raw value is more than 4 ulps away from the recorded one");
        require(normUlp <= 4, "undo: a normalised value is more than 4 ulps away from the recorded one");
        (void)original;
    });

    test("locked parameters do not change; unlocked ones do", [] {
        AetherAudioProcessor p;
        auto& rc = p.randomizer();
        const LockSet lk = someLocks();
        std::set<int> locked;
        for (int i = 0; i < kParamCount; ++i) if (lk.isLocked(i)) locked.insert(i);
        require(locked.size() > 30, "lock set unexpectedly small");
        for (std::uint64_t seed = 1; seed <= 12; ++seed) {
            const auto before = rawOf(p);
            const auto o = rc.randomize(fullCfg(seed, seed % 3 == 0 ? Safety::Wild : Safety::Safe), lk);
            require(o.ok, juce::String("randomize failed: ") + o.reason);
            const auto after = rawOf(p);
            int changedUnlocked = 0;
            for (int i = 0; i < kParamCount; ++i) {
                const bool same = bitsEqual(before[static_cast<std::size_t>(i)], after[static_cast<std::size_t>(i)]);
                if (locked.count(i)) require(same, juce::String("locked parameter changed: ") + aether::parameterSpecs()[static_cast<std::size_t>(i)].id);
                else if (!same) ++changedUnlocked;
            }
            require(changedUnlocked > 5, "almost nothing changed");
        }
    });

    test("randomize into B leaves the live face untouched and tells nobody", [] {
        AetherAudioProcessor p; p.prepareToPlay(48000, 64);
        auto& rc = p.randomizer();
        require(rc.liveFace() == Face::A, "start face");
        consumePanic(p);
        Counter c; Attach attach(p, c);
        const auto liveRaw = rawOf(p); const auto liveNorm = normOf(p); const auto name = nameOf(p);
        const auto bBefore = rc.captureSnapshot(Face::B);
        bool sawStructural = false;
        for (std::uint64_t seed = 1; seed <= 10; ++seed) {
            const auto o = rc.randomize(fullCfg(seed, Safety::Wild, true), {});
            require(o.ok && !o.wroteLive && o.face == Face::B && !o.panicRequested, juce::String("into-B outcome: ") + o.reason);
            sawStructural = sawStructural || o.structural;
            require(rawDiffCount(liveRaw, rawOf(p)) == 0 && normDiffCount(liveNorm, normOf(p)) == 0, "live parameters changed by into-B");
            require(nameOf(p) == name, "preset name changed by into-B");
            require(!rc.panicPending(), "into-B raised panic");
        }
        require(sawStructural, "none of the into-B patches was structural: the no-panic check proves nothing");
        require(c.changed == 0 && c.paramLevel == 0 && c.gestureBegin == 0 && c.gestureEnd == 0 && c.processorChanged == 0, "host-visible events during into-B");
        std::cout << "NOTIFY into-B x10 (structural seen): parameterChanged=" << c.changed << " gestureBegin=" << c.gestureBegin << " processorChanged=" << c.processorChanged << '\n';
        const auto bAfter = rc.captureSnapshot(Face::B);
        require(rawDiffCount(bBefore.value, bAfter.value) > 20, "B face did not change");
        // switching to B now shows exactly what was randomized into B
        p.toggleAB();
        require(rc.liveFace() == Face::B, "toggle");
        const auto shown = rawOf(p);
        std::cout << "TOGGLE B->live: raw-bit diffs vs the B snapshot=" << rawDiffCount(shown, bAfter.value) << " max relative=" << maxRelDiff(shown, bAfter.value) << '\n';
        require(maxRelDiff(shown, bAfter.value) < 1e-5, "B face not shown after toggle");
    });

    test("undo follows the physical A/B face, also across an A/B toggle", [] {
        AetherAudioProcessor p;
        auto& rc = p.randomizer();
        const auto a0 = rc.captureSnapshot(Face::A); const auto b0 = rc.captureSnapshot(Face::B);
        require(rc.randomize(fullCfg(5), {}).ok, "random on A");          // entry: face A
        require(rc.randomize(fullCfg(6, Safety::Safe, true), {}).ok, "random into B");  // entry: face B
        require(rc.undoDepth() == 2, "depth");
        p.toggleAB();                                                      // B live now
        require(rc.undoRandom().ok, "undo B entry");                       // B entry: B is live -> live write
        require(maxRelDiff(rc.captureSnapshot(Face::B).value, b0.value) < 1e-5, "B not restored");
        const auto o = rc.undoRandom();                                    // A entry: A is NOT live -> alternate tree
        require(o.ok && !o.wroteLive && o.face == Face::A, "A entry must land on the non-live face A");
        require(rawDiffCount(rc.captureSnapshot(Face::A).value, a0.value) == 0, "A not restored bit-exactly");
    });

    test("only structural live commits request panic", [] {
        AetherAudioProcessor p; p.prepareToPlay(48000, 64);
        auto& rc = p.randomizer();
        consumePanic(p);
        const int width = idx("master.width"), algo = idx("routing.algorithm");
        const float w0 = rawOf(p)[static_cast<std::size_t>(width)];
        Patch soft; soft.diffs.push_back({width, w0 > 1.f ? 0.5f : 1.5f});
        auto o = rc.commitRandomPatch(soft, fullCfg(1));
        require(o.ok && o.changedParams == 1 && !o.structural && !o.panicRequested && !rc.panicPending(), "continuous change raised panic");
        const float a0 = rawOf(p)[static_cast<std::size_t>(algo)];
        Patch hard; hard.diffs.push_back({algo, a0 == 3.f ? 7.f : 3.f});
        o = rc.commitRandomPatch(hard, fullCfg(2));
        require(o.ok && o.structural && o.panicRequested && rc.panicPending(), "structural change did not raise panic");
        consumePanic(p);
        o = rc.commitRandomPatch(hard, fullCfg(3));   // same values again: nothing to write
        require(o.ok && o.changedParams == 0 && !rc.panicPending(), "no-op commit raised panic");
        Patch hardB; hardB.diffs.push_back({algo, 11.f});
        o = rc.commitRandomPatch(hardB, fullCfg(4, Safety::Safe, true));
        require(o.ok && o.structural && !o.panicRequested && !rc.panicPending(), "into-B structural raised panic");
        o = rc.undoRandom();                          // undoes the into-B commit: B face, no panic
        require(o.ok && !o.wroteLive && !rc.panicPending(), "undo of an into-B commit raised panic");
        o = rc.undoRandom();                          // undoes the structural live commit: algorithm changes back -> panic
        require(o.ok && o.wroteLive && o.structural && rc.panicPending(), "structural undo did not raise panic");
        consumePanic(p);
        o = rc.undoRandom();                          // undoes the continuous one: no panic
        require(o.ok && !rc.panicPending() && rawOf(p)[static_cast<std::size_t>(width)] == w0, "continuous undo");
    });

    test("32 changed parameters: getParameter values equal the patch", [] {
        AetherAudioProcessor p;
        auto& rc = p.randomizer();
        const auto view = rc.captureSnapshot(rc.liveFace());
        Patch src = generate(view, fullCfg(77, Safety::Wild), {});
        require(src.report.ok && src.diffs.size() >= 32, "need at least 32 diffs");
        Patch p32; p32.diffs.assign(src.diffs.begin(), src.diffs.begin() + 32);
        const auto before = rawOf(p);
        const auto o = rc.commitRandomPatch(p32, fullCfg(77, Safety::Wild));
        require(o.ok && o.changedParams == 32, juce::String("changedParams=") + juce::String(o.changedParams));
        std::set<int> touched; double worstNorm = 0, worstRaw = 0;
        for (const auto& d : p32.diffs) {
            touched.insert(d.specIndex);
            const auto& spec = aether::parameterSpecs()[static_cast<std::size_t>(d.specIndex)];
            auto* param = p.state.getParameter(spec.id);
            worstNorm = std::max(worstNorm, static_cast<double>(std::abs(param->getValue() - param->convertTo0to1(d.value))));
            const double range = std::max(1e-9, static_cast<double>(spec.maximum) - spec.minimum);
            worstRaw = std::max(worstRaw, std::abs(static_cast<double>(rawOf(p)[static_cast<std::size_t>(d.specIndex)]) - d.value) / range);
        }
        std::cout << "PATCH32 worst |getValue - convertTo0to1(patch)|=" << worstNorm << " worst relative raw deviation=" << worstRaw << '\n';
        require(worstNorm < 1e-6 && worstRaw < 1e-5, "getParameter value differs from the patch");
        const auto after = rawOf(p);
        for (int i = 0; i < kParamCount; ++i) if (!touched.count(i)) require(bitsEqual(before[static_cast<std::size_t>(i)], after[static_cast<std::size_t>(i)]), "an untouched parameter moved");
    });

    test("in-process listener: what one commit / undo / no-op tells the host (facts, prepare is not re-run)", [] {
        AetherAudioProcessor p; p.prepareToPlay(48000, 64);
        auto& rc = p.randomizer();
        consumePanic(p);
        const int latency = p.getLatencySamples();
        Counter c; Attach attach(p, c);
        // a) continuous commit of N changed parameters
        const auto view = rc.captureSnapshot(rc.liveFace());
        Patch src = generate(view, mutCfg(9, 0.5f), {});
        require(src.report.ok && !src.diffs.empty(), "mutate patch");
        const auto o = rc.commitRandomPatch(src, mutCfg(9, 0.5f));
        std::cout << "NOTIFY live commit: changedParams=" << o.changedParams << " AudioProcessorListener.parameterChanged=" << c.changed << " (distinct indices " << c.changedIndices.size()
                  << ") gestureBegin=" << c.gestureBegin << " gestureEnd=" << c.gestureEnd << " processorChanged(updateHostDisplay)=" << c.processorChanged
                  << " Parameter::Listener.valueChanged=" << c.paramLevel << " gestureChanged=" << c.paramGestures << " structural=" << o.structural << " panic=" << o.panicRequested << '\n';
        require(c.changedIndices.size() <= static_cast<std::size_t>(o.changedParams) + 8, "far more indices notified than changed");
        require(c.processorChanged == 0 && p.getLatencySamples() == latency, "commit triggered a prepare-level change (updateHostDisplay / latency)");
        // b) undo
        c.reset();
        const auto u = rc.undoRandom();
        std::cout << "NOTIFY live undo: changedParams=" << u.changedParams << " parameterChanged=" << c.changed << " gestureBegin=" << c.gestureBegin << " gestureEnd=" << c.gestureEnd
                  << " processorChanged=" << c.processorChanged << " Parameter::Listener.valueChanged=" << c.paramLevel << '\n';
        require(c.processorChanged == 0, "undo triggered updateHostDisplay");
        // c) replaceState with an identical tree
        c.reset();
        p.state.replaceState(p.state.copyState());
        std::cout << "NOTIFY replaceState(identical tree): parameterChanged=" << c.changed << " gestureBegin=" << c.gestureBegin << " processorChanged=" << c.processorChanged << '\n';
        // d) the same N changes through the per-parameter gesture loop (what loadPreset does) for comparison
        const auto base = rawOf(p);
        c.reset();
        int n = 0;
        for (const auto& d : src.diffs) {
            const auto& spec = aether::parameterSpecs()[static_cast<std::size_t>(d.specIndex)];
            auto* param = p.state.getParameter(spec.id);
            param->beginChangeGesture(); param->setValueNotifyingHost(param->convertTo0to1(d.value)); param->endChangeGesture(); ++n;
        }
        std::cout << "NOTIFY gesture loop (reference, " << n << " params): parameterChanged=" << c.changed << " gestureBegin=" << c.gestureBegin << " gestureEnd=" << c.gestureEnd << '\n';
        (void)base;
        require(c.gestureBegin == n, "reference loop count");
    });

    test("amount-0 Mutate and equal-value commits push nothing on the ring", [] {
        AetherAudioProcessor p;
        auto& rc = p.randomizer();
        const auto o = rc.randomize(mutCfg(3, 0.f), {});
        require(o.ok && o.changedParams == 0 && rc.undoDepth() == 0 && !rc.recipe().valid, "Mutate amount 0 must be a no-op");
        require(rc.randomize(fullCfg(3), {}).ok && rc.undoDepth() == 1, "first real commit");
        require(rc.randomize(mutCfg(4, 0.f), {}).changedParams == 0 && rc.undoDepth() == 1, "no-op after a real commit");
    });

    test("recipe, source preset and name across re-rolls and undo", [] {
        AetherAudioProcessor p;
        auto& rc = p.randomizer();
        const juce::String original = nameOf(p);
        require(rc.randomize(fullCfg(0x123456789ULL, Safety::Normal), {}).ok, "first");
        const auto r1 = rc.recipe();
        const juce::String name1 = nameOf(p);
        require(r1.valid && r1.seed == 0x123456789ULL && r1.sourcePreset == original.toStdString() && r1.safety == Safety::Normal && r1.baselineHash != r1.resultHash, "recipe 1");
        require(name1 == juce::String(randomPresetName(original.toStdString(), r1.seed)), "name 1");
        require(hashValues(rawOf(p)) == r1.resultHash, "resultHash must equal the hash of the committed state");
        require(rc.randomize(fullCfg(0x2ULL), {}).ok, "second");
        const auto r2 = rc.recipe();
        require(r2.sourcePreset == original.toStdString() && nameOf(p) == juce::String(randomPresetName(original.toStdString(), r2.seed)), "source preset must survive a re-roll");
        require(rc.undoRandom().ok && nameOf(p) == name1 && rc.recipe().seed == r1.seed, "undo restores the first random name and recipe");
        require(rc.undoRandom().ok && nameOf(p) == original && !rc.recipe().valid, "second undo restores the original name, no recipe");
    });

    test("LockSet + recipe survive a save/load round trip; old saves read as empty locks", [] {
        AetherAudioProcessor p; auto& rc = p.randomizer();
        rc.setLocks(someLocks());
        require(rc.randomize(fullCfg(31337), rc.locks()).ok, "random");
        const auto bytes = saveOf(p);
        require(hasRandomNode(bytes), "saved state lacks the random node");
        AetherAudioProcessor q; q.setStateInformation(bytes.getData(), static_cast<int>(bytes.getSize()));
        auto& qc = q.randomizer();
        const auto l1 = rc.locks(), l2 = qc.locks();
        require(l1.params == l2.params && l1.sections == l2.sections && l1.modRows == l2.modRows, "locks lost");
        const auto a = rc.recipe(), b = qc.recipe();
        require(b.valid && a.seed == b.seed && a.algoVersion == b.algoVersion && a.mode == b.mode && bitsEqual(a.amount, b.amount) && a.safety == b.safety && a.scopeMask == b.scopeMask
                && a.attempt == b.attempt && bitsEqual(a.trimDb, b.trimDb) && a.intoB == b.intoB && a.edition == b.edition && a.baselineHash == b.baselineHash && a.resultHash == b.resultHash
                && a.sourcePreset == b.sourcePreset && a.sourceProgram == b.sourceProgram && a.timeUtcMs == b.timeUtcMs && a.version == b.version, "recipe differs after round trip");
        require(nameOf(q) == nameOf(p), "preset name lost");
        require(qc.undoDepth() == 0, "a loaded state starts with an empty undo ring");
        const auto bytes2 = saveOf(q);
        std::cout << "SAVE round trip save->load->save identical bytes: " << (sameBlock(bytes, bytes2) ? "yes" : "no") << " (" << bytes.getSize() << " vs " << bytes2.getSize() << ")\n";
        require(sameBlock(bytes, bytes2), "save -> load -> save is not stable");
        // old save: the same state without the random node
        auto xml = xmlOf(bytes);
        auto* node = xml->getChildByName("random"); require(node != nullptr, "node"); xml->removeChildElement(node, true);
        const auto old = toBlock(*xml);
        AetherAudioProcessor r; r.randomizer().setLocks(someLocks());
        r.setStateInformation(old.getData(), static_cast<int>(old.getSize()));
        const auto l3 = r.randomizer().locks();
        require(l3.params.none() && l3.sections == 0 && l3.modRows == 0 && !r.randomizer().recipe().valid, "old save must read as empty locks");
        require(rawDiffCount(rawOf(r), rawOf(p)) <= 0 || maxRelDiff(rawOf(r), rawOf(p)) < 1e-5, "old save did not restore parameters");
        // garbage in the node: ignored, parameters still load
        auto xml2 = xmlOf(bytes);
        xml2->getChildByName("random")->setAttribute("locks", "not-a-lock-set");
        xml2->getChildByName("random")->setAttribute("seed", "zz");
        const auto garbage = toBlock(*xml2);
        AetherAudioProcessor s; s.setStateInformation(garbage.getData(), static_cast<int>(garbage.getSize()));
        require(s.randomizer().locks().params.none() && !s.randomizer().recipe().valid, "malformed node must read as empty");
        require(maxRelDiff(rawOf(s), rawOf(p)) < 1e-5, "malformed random node blocked the parameters");
    });

    test("no locks and no recipe: the saved state has no random node and is byte-identical across lock edits", [] {
        AetherAudioProcessor p; auto& rc = p.randomizer();
        const auto plain = saveOf(p);
        require(!hasRandomNode(plain), "empty locks must not write a node");
        rc.setLocks(someLocks());
        const auto withLocks = saveOf(p);
        require(hasRandomNode(withLocks), "locks must be written");
        rc.setLocks({});
        const auto again = saveOf(p);
        require(sameBlock(plain, again), "bytes changed after setting and clearing locks");
        // the live APVTS state never carries the node
        require(!p.state.copyState().getChildWithName("random").isValid(), "random node leaked into the live state");
        // schema rule unchanged: only modulation.N.target decides 3 vs 4, locks do not
        auto schemaOf = [](const juce::MemoryBlock& b) { auto x = xmlOf(b); return x->getIntAttribute("schema"); };
        require(schemaOf(plain) == aether::stateSchema && schemaOf(withLocks) == aether::stateSchema, "schema with/without locks");
        setParam(p, "modulation.1.source", 1); setParam(p, "modulation.1.target", 90);
        rc.setLocks(someLocks());
        require(schemaOf(saveOf(p)) == aether::stateSchemaDestinations, "destination 90 must still stamp schema 4");
        rc.setLocks({});
        require(schemaOf(saveOf(p)) == aether::stateSchemaDestinations, "schema 4 without locks");
    });

    test("a truncated save with locks is rejected as a whole", [] {
        AetherAudioProcessor p; auto& rc = p.randomizer();
        rc.setLocks(someLocks());
        require(rc.randomize(fullCfg(8), rc.locks()).ok, "random");
        const auto good = saveOf(p);
        AetherAudioProcessor q; q.randomizer().setLocks([] { LockSet l; l.params.set(7); return l; }());
        const auto raw = rawOf(q); const auto name = nameOf(q);
        for (const std::size_t cut : {std::size_t(4), std::size_t(8), good.getSize() / 2, good.getSize() - 3}) {
            q.setStateInformation(good.getData(), static_cast<int>(cut));
            require(q.randomizer().locks().params.test(7) && q.randomizer().locks().params.count() == 1, "locks changed by a truncated save");
            require(rawDiffCount(rawOf(q), raw) == 0 && nameOf(q) == name, "state changed by a truncated save");
        }
    });

    test("random entry points refuse to run off the message thread", [] {
        AetherAudioProcessor p; auto& rc = p.randomizer();
        const auto before = rawOf(p);
        Outcome o; std::thread t([&] { o = rc.randomize(fullCfg(1), {}); });
        t.join();
        require(!o.ok && std::strcmp(o.reason, "not-message-thread") == 0, "wrong-thread call accepted");
        require(rawDiffCount(before, rawOf(p)) == 0 && rc.undoDepth() == 0, "wrong-thread call changed state");
    });

    if (bench) {
        // Release timing of one randomization including the bulk write. Wall time, one process, nothing else running.
        using clock = std::chrono::steady_clock;
        for (const bool mutate : {false, true}) {
            AetherAudioProcessor p; auto& rc = p.randomizer();
            const int n = 2000;
            double sum = 0, worst = 0, sumCap = 0, sumGen = 0, sumCommit = 0, worstCommit = 0;
            for (int i = 0; i < n; ++i) {
                Config c = mutate ? mutCfg(5000 + static_cast<std::uint64_t>(i), 0.5f) : fullCfg(5000 + static_cast<std::uint64_t>(i), i % 3 == 0 ? Safety::Wild : i % 3 == 1 ? Safety::Normal : Safety::Safe);
                const auto t0 = clock::now();
                const auto view = rc.captureSnapshot(rc.liveFace());
                const auto t1 = clock::now();
                const Patch patch = generate(view, c, {});
                const auto t2 = clock::now();
                const auto o = rc.commitRandomPatch(patch, c);
                const auto t3 = clock::now();
                if (!patch.report.ok || !o.ok) throw std::runtime_error("bench: randomize failed");
                const double cap = std::chrono::duration<double, std::micro>(t1 - t0).count(), gen = std::chrono::duration<double, std::micro>(t2 - t1).count(), com = std::chrono::duration<double, std::micro>(t3 - t2).count();
                const double total = cap + gen + com;
                sum += total; sumCap += cap; sumGen += gen; sumCommit += com; worst = std::max(worst, total); worstCommit = std::max(worstCommit, com);
            }
            std::cout << "BENCH " << edition << (mutate ? " Mutate(0.5)" : " Full(mixed safety)") << " n=" << n << ": total avg " << sum / n << " us, worst " << worst << " us | capture avg " << sumCap / n
                      << " us | generate avg " << sumGen / n << " us | commit(replaceState+ring+recipe) avg " << sumCommit / n << " us, worst " << worstCommit << " us\n";
            // undo cost (16 steps)
            double undoSum = 0, undoWorst = 0; int undos = 0;
            while (rc.canUndoRandom()) { const auto t0 = clock::now(); rc.undoRandom(); const double us = std::chrono::duration<double, std::micro>(clock::now() - t0).count(); undoSum += us; undoWorst = std::max(undoWorst, us); ++undos; }
            std::cout << "BENCH " << edition << " undo x" << undos << ": avg " << undoSum / undos << " us, worst " << undoWorst << " us\n";
        }
    }

    std::cout << "aether_rand_wiring(" << edition << "): " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
