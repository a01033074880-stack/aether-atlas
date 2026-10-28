// AETHER 2.3.0 - randomization: processor-side entry points (implemented in RandomizeController.cpp).
// Spec: docs/randomization-spec.md section P and 9.1.1. Not touched by this feature: PluginEditor.*, RandomizePanel.*,
// version number, ParameterIDs, presets, the schema-writing rules.
//
// HOW IT HOOKS INTO THE PROCESSOR (PluginProcessor.h / .cpp, one line each):
//   - `friend class aether::rand::RandomizeController;` (private snapshot / snapshotB / metadataLock / presetName /
//     currentProgram / panic / effectiveMode()),
//   - `randomizer()` returns the one controller the processor owns,
//   - getStateInformation calls writeLocksTo(copy), setStateInformation calls readLocksFrom(parsedTree).
//
// HOW IT CONNECTS TO aether::rand (libs/aether-rand, JUCE-free):
//   StateView  <- captureSnapshot(Face)           (495 unit values in parameterSpecs() order + EditionContext)
//   generate(StateView, Config, LockSet) -> Patch  (pure, NON-realtime: allocates a std::vector)
//   Patch      -> commitRandomPatch(...)           (diffs are unit values, ascending spec index)
//   LockSet    <-> locks() / setLocks() / writeLocksTo() / readLocksFrom()
//   randomize(cfg, locks) = captureSnapshot + generate + commitRandomPatch in one call.
//
// THREADING. The random entry points (randomize, commitRandomPatch, undoRandom, applySnapshot*) are MESSAGE-THREAD ONLY (the
// thread that runs the editor); called from another thread they return Outcome{ok=false, "not-message-thread"} and change
// nothing. They must never be called from processBlock. The audio thread is never touched directly; it only sees the result
// through the APVTS atomic parameter values and the `panic` flag. Read-only accessors (liveFace, captureSnapshot, locks,
// recipe, undoDepth, panicPending) and the two persistence functions may run on any thread: the controller's own data
// (locks, recipe, ring) is guarded by a private CriticalSection, and the processor-side `snapshot` tree by metadataLock,
// because hosts may call get/setStateInformation off the message thread. An audition worker (spec P.6, later step) may call
// the pure generate() on a copy of StateView, but must hand its Patch back via MessageManager::callAsync.
#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <aether/Rand.h>
#include <aether/RandState.h>
#include <array>
#include <cstdint>
#include <span>
#include <string>

class AetherAudioProcessor;

namespace aether::rand {

// Result of one entry-point call.
struct Outcome {
    bool ok = false;
    const char* reason = "";      // "" when ok; else a generate()/validate() reason, or "no-undo" / "not-message-thread" / "no-alternate"
    int changedParams = 0;        // values that really differed and were written
    bool structural = false;      // the written change was structural (patchIsStructural)
    Face face = Face::A;          // face that received the write
    bool wroteLive = false;       // true if the write went to the currently live face (host + audio affected)
    bool panicRequested = false;  // requestPanic() was called (structural AND live face)
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
    //    out-of-range values clamped to the spec range; a missing or unparsable entry falls back to the spec default).
    // Does not touch the audio thread, the host, or `panic`.
    //  EditionContext is filled consistently: effectiveMode = proc.effectiveMode(that face's global.mode) and
    //  isSynth = (effectiveMode == 2). Reason (Rand.cpp:140): mode_ = isSynth ? 2 : (effectiveMode == 1 ? 1 : 0), i.e. the
    //  engine looks at isSynth first. The processor really yields effectiveMode 2 for a Synth edition, while the older engine
    //  tests build a Synth state as (isSynth=true, effectiveMode=0); tests/RandStateTests.cpp pins that the real pair
    //  (true, 2) gives bit-identical patches. The pair (isSynth=false, effectiveMode=2) would be treated as FX by the engine;
    //  the controller never produces it.
    StateView captureSnapshot(Face face) const;

    // Whole-face replacement from unit values (clamped to the spec range; non-finite entries keep the current value).
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
    // recipe only). Order: (1) keep only diffs that really change the target face, (2) push the whole target face on the
    // ring, (3) write the diffs, (4) set the recipe and - for a live-face commit only - the preset name
    // randomPresetName(source, seed), (5) requestPanic() iff structural AND the target is the live face.
    // A commit that changes nothing (empty patch, Mutate amount 0, or values already equal) pushes nothing and returns ok
    // with changedParams = 0. Into-B commits keep the preset name (the name belongs to the live face), but do record the recipe.
    Outcome commitRandomPatch(const Patch& patch, const Config& cfg, float trimDb = 0.f);

    // Undo Random: restore the newest ring entry - the whole state of the face it was taken from (+ name + program, when
    // that commit had renamed the preset, + recipe) - including any manual edits made after the random commit
    // (whole-state semantics). requestPanic() only if the live face is written AND the restored values are structurally
    // different from the current ones (patchIsStructural on the difference).
    //
    // WHY THE RING RECORDS THE PHYSICAL A/B FACE (spec P.2 drafted Live/AlternateSnapshot instead): "live" changes meaning
    // every time the user presses A/B (toggleAB swaps `snapshot` and the live state). An entry taken from face A must
    // go back to face A even if B is live by the time of the undo; otherwise Undo Random would silently overwrite the face
    // the user is working on with the other face's old sound. Recording the face makes every entry valid across A/B
    // toggles, so the spec's workaround (clear the Live entries on every toggle) is not needed. The ring stays in memory
    // only and is never persisted.
    Outcome undoRandom();
    bool canUndoRandom() const noexcept;
    int undoDepth() const noexcept;                    // 0..SnapshotRing::kCapacity
    void clearUndoRandom() noexcept;

    // Recipe of the last random commit, valid=false if none. The "modified" badge = hashValues(current) != resultHash.
    RandomRecipe recipe() const;

    // True while a requestPanic() is waiting for the audio thread (the processor's `panic` flag). The audio thread
    // clears it at the start of the next processBlock. Probe for tests and diagnostics.
    bool panicPending() const noexcept;

    // ---- LockSet ----------------------------------------------------------------------------------------------------
    LockSet locks() const;
    void setLocks(const LockSet& locks);

    // Persistence of the LockSet (and, when valid, the recipe) in the PROCESSOR STATE.
    //   Where: ONE optional child node of the saved AetherState tree: <random version="1" locks="1:..." seed=... />.
    //   writeLocksTo() appends it to the COPY that getStateInformation() already makes (`auto tree=state.copyState()`),
    //   never to the live APVTS state, and writes nothing when the LockSet is empty and no recipe is valid - so a state
    //   saved without locks is byte-identical to today's.
    //   readLocksFrom() reads the node from the parsed tree OUTSIDE merge() (merge only copies known parameter ids, so it
    //   would drop it); a missing or malformed node means "no locks, no recipe" and is not an error. It also clears the undo
    //   ring (a loaded state starts a new undo history).
    //   Schema: UNCHANGED. `schema` / `version` writing rules stay exactly as they are (stateSchema 3 or
    //   stateSchemaDestinations 4 decided by treeUsesExtendedDestination, which only looks at modulation.N.target);
    //   `random` does not influence it and no schema 5 is introduced (9.1.1 #5).
    //   Old saves: a state without the node loads with an empty LockSet. Old BUILDS (2.2.0) read parameters by id via
    //   getChildWithProperty and so should ignore an extra child - UNVERIFIED (needs a SchemaProbe run against 991e493).
    //   Locks are not part of the .aether preset file.
    void writeLocksTo(juce::ValueTree& stateCopy) const;
    void readLocksFrom(const juce::ValueTree& parsedState);

private:
    // HOW A COMMIT TOUCHES THE HOST.
    //  Read in the JUCE sources at C:/SDKs/JUCE (juce_AudioProcessorValueTreeState.cpp), then measured with an in-process
    //  AudioProcessorListener (tests/RandWiringTests.cpp prints the counts as "NOTIFY ..." lines; NOT seen in a DAW):
    //  * Live face: build `auto tree = proc.state.copyState()`, set "value" of the changed parameter children, then
    //    proc.state.replaceState(tree). replaceState does `state = newState` -> valueTreeRedirected ->
    //    updateParameterConnectionsToChildTrees -> setNewState(child) -> ParameterAdapter::setDenormalisedValue, which calls
    //    parameter.setValueNotifyingHost() ONLY for parameters whose value differs from the current one. Hence one value
    //    notification per CHANGED parameter and NO beginChangeGesture/endChangeGesture (unlike loadPreset/applyValues).
    //    Automation recording behaviour for gesture-less notifications (latch/touch/write, per DAW) is UNVERIFIED.
    //    NOTE spec P.5 recommends the opposite for the live face (per-parameter gesture loop, no replaceState); the
    //    project decision (parent / user) is replaceState, which is also what toggleAB and setStateInformation already do.
    //    replaceState is only ever called from the message thread here (the entry points check), and nothing in it calls
    //    prepareToPlay; no prepare-level rebuild is triggered by a commit (tests count audioProcessorChanged() = 0).
    //  * Other face: only the `snapshot` ValueTree changes; the host is never notified (same as the A/B side today).
    //  * requestPanic() (audio thread reset() at the next block, tails are cut) ONLY for live-face structural changes.
    struct Diff { int index; float value; };
    Outcome writeFace(Face face, std::span<const Diff> diffs, bool allowPanic);
    std::vector<Diff> effectiveDiffs(const std::array<float, kParamCount>& before, std::span<const Patch::Diff> diffs) const;
    bool onMessageThread() const noexcept;

    AetherAudioProcessor& proc_;
    mutable juce::CriticalSection dataLock_;   // guards locks_, ring_, recipe_ (persistence may run on a host thread)
    LockSet locks_{};
    SnapshotRing ring_{};
    RandomRecipe recipe_{};
    JUCE_DECLARE_NON_COPYABLE(RandomizeController)
};

}  // namespace aether::rand
