// AETHER 2.3.0 - randomization: processor-side entry points (header-only API declaration).
//
// STATUS: STEP 1 OF "processor wiring" = DECLARATIONS ONLY, no implementation, nothing calls it yet (the stub TU
// Source/RandomizeControllerStub.cpp only includes this header so it is compiled). Do not link against these members
// before step 2 adds Source/RandomizeController.cpp. Spec: docs/randomization-spec.md section P and 9.1.1.
// Not touched by this header: PluginEditor.*, RandomizePanel.*, version number, ParameterIDs, presets.
//
// HOW IT HOOKS INTO THE PROCESSOR (step 2, minimal edit of PluginProcessor.h):
//   - `friend class aether::rand::RandomizeController;` (needs private snapshot / snapshotB / metadataLock / presetName /
//     currentProgram / panic / effectiveMode()),
//   - `aether::rand::RandomizeController& randomizer() noexcept;` (owns one controller, constructed with *this),
//   - getStateInformation / setStateInformation each get ONE call: writeLocksTo(copy) / readLocksFrom(parsedTree).
//
// HOW IT CONNECTS TO aether::rand (libs/aether-rand, JUCE-free):
//   StateView  <- captureSnapshot(Face)           (495 unit values in parameterSpecs() order + EditionContext)
//   generate(StateView, Config, LockSet) -> Patch  (pure, NON-realtime: allocates a std::vector)
//   Patch      -> commitRandomPatch(...)           (diffs are unit values, ascending spec index)
//   LockSet    <-> locks() / setLocks() / writeLocksTo() / readLocksFrom()
//   randomize(cfg, locks) = captureSnapshot + generate + commitRandomPatch in one call.
//
// THREADING. Every member is MESSAGE-THREAD ONLY (JUCE MessageManager thread, i.e. the thread that runs the editor).
// None of them may be called from processBlock. The audio thread is never touched directly; it only sees the result
// through the APVTS atomic parameter values (and the `panic` flag). An audition worker thread (spec P.6, later step) may
// call the pure generate() on a copy of StateView, but must hand its Patch back via MessageManager::callAsync.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <aether/Rand.h>
#include <aether/RandState.h>
#include <array>
#include <cstdint>
#include <string>

class AetherAudioProcessor;

namespace aether::rand {

// Result of one entry-point call.
struct Outcome {
    bool ok = false;
    const char* reason = "";      // "" when ok; else a generate()/validate() reason, or "no-undo" / "bad-locks"
    int changedParams = 0;        // values that really differed and were written
    bool structural = false;      // patchIsStructural() was true -> requestPanic() was called (live face only)
    Face face = Face::A;          // face that received the write
    bool wroteLive = false;       // true if the write went to the currently live face (host + audio affected)
};

class RandomizeController {
public:
    explicit RandomizeController(AetherAudioProcessor& owner) noexcept;

    // ---- face / snapshot interface ("Randomize into B"; replaces the editor's toggleAB -> randomize -> toggleAB detour) --
    // Which physical face is live right now: isSnapshotB() ? Face::B : Face::A.
    Face liveFace() const noexcept;

    // Read-only copy of one face as a StateView (unit values + EditionContext{effectiveMode, isSynth}).
    //  - the live face reads the APVTS raw atomics (cheap, no tree copy);
    //  - the other face is parsed from the processor's `snapshot` ValueTree under metadataLock (finite values only,
    //    out-of-range values clamped to the spec range; a missing entry falls back to the spec default).
    // Does not touch the audio thread, the host, or `panic`.
    //  EditionContext MUST be filled consistently: effectiveMode = proc.effectiveMode(stored global.mode) and
    //  isSynth = (effectiveMode == 2). Reason (Rand.cpp:140): mode_ = isSynth ? 2 : (effectiveMode == 1 ? 1 : 0), i.e. the
    //  engine looks at isSynth first. The processor really yields effectiveMode 2 for a Synth edition, while the existing
    //  engine tests build a Synth state as (isSynth=true, effectiveMode=0). The real pair (true, 2) gives the same result
    //  today but is NOT covered by a test yet -> step 2 adds an engine-level test for it (no behaviour change). The pair
    //  (isSynth=false, effectiveMode=2) would be treated as FX by the engine; the controller never produces it.
    StateView captureSnapshot(Face face) const;

    // Whole-face replacement from unit values (clamped, NaN -> keep the current value).
    //  - Face != liveFace(): only the `snapshot` ValueTree is rewritten under metadataLock. NO host notification, NO
    //    panic, NO effect on Live or on automation. This is the "Randomize into B" case while A is live.
    //  - Face == liveFace(): goes through the live path (replaceState, see below) - same as writing the live state.
    // The ring/recipe/name are NOT touched by this primitive (commitRandomPatch / undoRandom manage them).
    Outcome applySnapshot(Face face, const std::array<float, kParamCount>& values);
    // Convenience used by the editor: applySnapshot(Face::B, values).
    Outcome applySnapshotToB(const std::array<float, kParamCount>& values);

    // ---- random entry points ----------------------------------------------------------------------------------------
    // Target face = cfg.intoB ? Face::B : liveFace(). Steps: captureSnapshot(target) -> generate(view, cfg, locks) ->
    // commitRandomPatch. Locks are passed explicitly; the editor usually passes locks().
    Outcome randomize(const Config& cfg, const LockSet& locks);

    // Commit an already generated Patch (e.g. after the audition stage appended a graph.trim diff; trimDb is stored in the
    // recipe only). Order: (1) fill RandomSnapshot of the target face and push it on the ring, (2) write the diffs,
    // (3) set preset name randomPresetName(source, cfg.seed) and recipe_, (4) requestPanic() iff structural AND the target is
    // the live face. An empty patch (Mutate amount 0) pushes nothing and returns ok with changedParams = 0.
    Outcome commitRandomPatch(const Patch& patch, const Config& cfg, float trimDb = 0.f);

    // Undo Random: restore the newest ring entry (whole state of the face it was taken from + name + program + recipe),
    // including any manual edits made after the random commit (whole-state semantics). Restores into
    // `entry.face` even if the user toggled A/B since. requestPanic() only if the restored values are structurally
    // different from the current ones (compared with patchIsStructural on the difference).
    Outcome undoRandom();
    bool canUndoRandom() const noexcept;
    int undoDepth() const noexcept;                    // 0..SnapshotRing::kCapacity
    void clearUndoRandom() noexcept;

    // Recipe of the last random commit, valid=false if none. The "modified" badge = hashValues(current) != resultHash.
    RandomRecipe recipe() const;

    // ---- LockSet ----------------------------------------------------------------------------------------------------
    const LockSet& locks() const noexcept;
    void setLocks(const LockSet& locks);

    // Persistence of the LockSet (and, when valid, the recipe) in the PROCESSOR STATE.
    //   Where: ONE optional child node of the saved AetherState tree: <random version="1" locks="1:..." [recipe...]/>.
    //   writeLocksTo() appends it to the COPY that getStateInformation() already makes (`auto tree=state.copyState()`),
    //   never to the live APVTS state, and writes nothing when the LockSet is empty and no recipe is valid - so a state
    //   saved without locks is byte-identical to today's.
    //   readLocksFrom() reads the node from the parsed tree OUTSIDE merge() (merge only copies known parameter ids, so it
    //   would drop it); a missing or malformed node means "no locks" and is not an error.
    //   Schema: UNCHANGED. `schema` / `version` writing rules stay exactly as they are (stateSchema 3 or
    //   stateSchemaDestinations 4 decided by treeUsesExtendedDestination, which only looks at modulation.N.target);
    //   `random` does not influence it and no schema 5 is introduced (9.1.1 #5).
    //   Old saves: a state without the node loads with an empty LockSet. Old BUILDS (2.2.0) read parameters by id via
    //   getChildWithProperty and so should ignore an extra child - UNVERIFIED (needs a SchemaProbe run against 991e493).
    //   The undo ring is never persisted. Locks are not part of the .aether preset file.
    void writeLocksTo(juce::ValueTree& stateCopy) const;
    void readLocksFrom(const juce::ValueTree& parsedState);

private:
    // HOW A COMMIT TOUCHES THE HOST.
    //  Read in the JUCE sources at C:/SDKs/JUCE (juce_AudioProcessorValueTreeState.cpp) this turn, NOT run, NOT seen in a DAW:
    //  * Live face: build `auto tree = proc.state.copyState()`, set "value" of the changed parameter children, then
    //    proc.state.replaceState(tree). replaceState does `state = newState` -> valueTreeRedirected ->
    //    updateParameterConnectionsToChildTrees -> setNewState(child) -> ParameterAdapter::setDenormalisedValue, which calls
    //    parameter.setValueNotifyingHost() ONLY for parameters whose value differs from the current one. So by code
    //    reading the host gets one value notification per CHANGED parameter and NO beginChangeGesture/endChangeGesture
    //    (unlike loadPreset/applyValues). Automation recording behaviour for gesture-less notifications (latch/touch/
    //    write, per DAW) is UNVERIFIED; the JUCE-level claim above is also unverified until the in-process
    //    AudioProcessorParameter::Listener test (spec 8.3 aether_rand_notify) exists.
    //    NOTE spec P.5 recommends the opposite for the live face (per-parameter gesture loop, no replaceState); this header
    //    follows the step-1 instruction (replaceState) - open question #1.
    //  * Other face: only the `snapshot` ValueTree changes; the host is never notified (same as the A/B side today).
    //  * requestPanic() (audio thread reset() at the next block, tails are cut) ONLY for live-face structural patches.
    AetherAudioProcessor& proc_;
    LockSet locks_{};
    SnapshotRing ring_{};
    RandomRecipe recipe_{};
    JUCE_DECLARE_NON_COPYABLE(RandomizeController)
};

}  // namespace aether::rand
