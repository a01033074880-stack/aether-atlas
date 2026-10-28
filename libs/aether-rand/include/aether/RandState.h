// AETHER 2.3.0 - randomization: state-side data types (JUCE-free, header-only API declaration).
//
// STATUS: STEP 1 OF "processor wiring" = DECLARATIONS ONLY. Nothing in this file is defined or called yet; the
// out-of-line functions below are implemented in step 2 (libs/aether-rand/src/RandState.cpp, to be added then).
// Spec: docs/randomization-spec.md section P (P.2 snapshot ring, P.3 LockSet, P.4 recipe, P.7 schema) and 9.1.1.
//
// Everything here is plain data + pure functions so it can be tested by CTest without a processor. The JUCE-facing
// glue (replaceState, metadataLock, ValueTree) lives in Source/RandomizeController.h and uses these types.
#pragma once
#include "Rand.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace aether::rand {

// ---- Undo Random: whole-state snapshot ring (spec P.2) -------------------------------------------------------------
inline constexpr int kUndoDepth = 16;     // frozen: "Undo Random only", 16 steps
inline constexpr int kRecipeVersion = 1;
inline constexpr int kPresetNameMax = 128;  // PresetManager.cpp substring(0,128)

// Physical A/B face, NOT "live/alternate". Face B is the processor's `snapshotB==true` side. Which face is currently
// live depends on AetherAudioProcessor::isSnapshotB(). Recording the face (instead of spec P.2's Live/AlternateSnapshot)
// keeps an undo entry valid after the user toggles A/B: it is written back to the face it was taken from.
enum class Face : std::uint8_t { A, B };

// Spec P.4. Persisted (optionally) in the same `random` node as the locks (see RandomizeController.h); the .aether
// preset file does NOT carry it (9.1.1 #6).
struct RandomRecipe {
    std::uint8_t version = kRecipeVersion;
    std::uint64_t seed = 0;            // 40 significant bits
    int algoVersion = kRandAlgoVersion;
    Mode mode = Mode::Full;
    float amount = 1.f;
    Safety safety = Safety::Safe;      // 9.1.1 #2 (spec P.1 draft said Normal)
    std::uint32_t scopeMask = ScopeAll;
    int attempt = 0;                   // accepted re-roll index
    float trimDb = 0.f;                // graph.trim compensation written by the audition stage (9.1.1 #4)
    bool intoB = false;
    int edition = 0;                   // effective mode 0 FX / 1 Resynth / 2 Synth
    std::uint64_t baselineHash = 0;    // hashValues() of the 495 values before the commit
    std::uint64_t resultHash = 0;      // hashValues() right after the commit (dirty flag = current hash != resultHash)
    std::string sourcePreset;          // currentPresetName() before the commit
    int sourceProgram = -1;            // factory program before the commit, -1 if none
    std::int64_t timeUtcMs = 0;
    bool valid = false;
};

// One undo step: the complete state of one A/B face before a random commit. Non-persistent (never written to the
// plugin state; cleared when the session ends).
struct RandomSnapshot {
    Face face = Face::A;
    std::array<float, kParamCount> values{};  // unit values, parameterSpecs() order (same convention as StateView)
    std::string presetName;                   // name before the commit
    RandomRecipe recipe;                      // recipe in effect before the commit (valid=false if none)
    int program = 0;                          // currentProgram before the commit
};

// Fixed-capacity (kUndoDepth) ring. Message thread ONLY; the audio thread never sees it, so no lock is needed.
// push() is done by the controller immediately BEFORE a random commit; the 17th push drops the oldest entry.
class SnapshotRing {
public:
    static constexpr int kCapacity = kUndoDepth;
    static constexpr int capacity() noexcept { return kCapacity; }

    void push(RandomSnapshot snapshot);               // newest on top; overwrites the oldest when full
    bool pop(RandomSnapshot& out);                    // undo: removes and returns the newest; false when empty
    const RandomSnapshot* peek() const noexcept;      // newest, or nullptr when empty
    bool canUndo() const noexcept;
    int size() const noexcept;                        // 0..kCapacity
    void clear() noexcept;

private:
    std::array<RandomSnapshot, kCapacity> slots_{};
    int head_ = 0;   // index of the next write
    int count_ = 0;
};

// ---- pure helpers (declared; defined in step 2) -------------------------------------------------------------------
// FNV-1a-64 over the IEEE-754 bit patterns of the 495 values, each as 4 little-endian bytes (baselineHash/resultHash).
std::uint64_t hashValues(const std::array<float, kParamCount>& values) noexcept;

// 40-bit seed -> 8 Crockford Base32 characters (no I L O U), e.g. "7K3M9Q2X".
std::string seedToBase32(std::uint64_t seed40);

// "<sourcePreset> (Random) <seed>", at most kPresetNameMax bytes; an over-long SOURCE name is truncated, never the seed.
std::string randomPresetName(std::string_view sourcePreset, std::uint64_t seed40);

// True when the patch touches anything that needs the audio thread to reset() (the processor's requestPanic):
// node type (nodeParam(s,0)), routing algorithm, fm.algorithm, source.engine, and the enable switches
// graph.enabled / fm.enabled / source.enabled. Everything else (continuous values) is false, so a Mutate that only moves
// knobs never cuts a tail. The exact set is an open question (see the report): route edge gains are NOT in it.
bool patchIsStructural(const Patch& patch) noexcept;

// LockSet <-> text, for storing in the processor state (where: see RandomizeController.h).
// Format v1, one ASCII line: "1:<sections 8 hex>:<modRows 4 hex>:<params 124 hex>" (495 bits, bit i = spec index i,
// least significant bit of the first hex digit = index 0). deserializeLocks returns false and leaves `out` untouched on
// any malformed input (wrong version, length, non-hex).
std::string serializeLocks(const LockSet& locks);
bool deserializeLocks(std::string_view text, LockSet& out) noexcept;

}  // namespace aether::rand
