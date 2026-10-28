#pragma once
#include "PluginProcessor.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// Randomize popover for the Atlas editor (stage B of 2.3.0).
//
// The UI depends only on RandomizeBackend. makeEngineBackend() is the real implementation, a thin adapter over
// aether::rand::RandomizeController (Source/RandomizeController.h, reached through AetherAudioProcessor::randomizer()):
//   - the controller owns the 16-step Undo Random ring, the LockSet, the recipe, the A/B faces and the "(Random)" preset name
//   - Live face: one atomic APVTS::replaceState per random (message thread only; one notification per changed parameter, no gestures)
//   - Undo records the PHYSICAL A/B face, so it restores into the face the roll came from, also after an A/B toggle
//   - Randomize into B writes only the B snapshot (no host notification, the user stays on A)
// The editor installs this backend by default; setRandomizeBackendFactory still replaces it (tests).
namespace aether::ui {

inline constexpr int randomUndoSteps=16;
// Seeds are 40 bit and shown as 8 Crockford base32 characters in two groups of four ("A7F3-92C1").
inline constexpr std::uint64_t randomSeedMask=(std::uint64_t(1)<<40)-1;
juce::String formatRandomSeed(std::uint64_t seed);
bool parseRandomSeed(const juce::String& text,std::uint64_t& seed);
// The preset name shown after a roll.
juce::String randomPresetLabel(std::uint64_t seed);

// Maps to aether::rand::Mode { Mutate, Full } (§P.1). UI keeps lower-case enumerators.
enum class RandomizeMode { mutate, full };

// What a roll may write. all = every unlocked group; motion = only the Motion group (03 Motion page: LFO values, modulation depths,
// and, once the engine has it, the source / destination of every modulation row). Maps to aether::rand::Config::scopeMask.
enum class RandomizeScope { all, motion };

// UI-facing subset of aether::rand::Config + LockSet (§P.1 / §P.3). Real engine also has Safety / scopeMask / attempt.
struct RandomizeRequest {
    std::uint64_t seed=0;           // 40-bit, same as Config::seed
    RandomizeMode mode=RandomizeMode::mutate;
    float amount=.35f;              // 0..1; Mutate + amount==0 => empty patch (§6.2)
    juce::StringArray lockedGroups; // section names from RandomizeBackend::lockGroups(); projection of LockSet::sections
    RandomizeScope scope=RandomizeScope::all;
};

// UI-facing subset of aether::rand::RandomRecipe (§P.4). Metadata only — not part of the .aether preset format.
struct RandomizeRecipe {
    std::uint64_t seed=0;
    RandomizeMode mode=RandomizeMode::mutate;
    float amount=0;
    juce::StringArray lockedGroups;
    juce::String sourcePreset;      // RandomRecipe::sourcePreset (name before the first roll of a chain)
    bool intoB=false;               // RandomRecipe::intoB — result written into the B / AlternateSnapshot slot
    bool onLiveFace=true;           // the roll was written to the face that is live now (false: it sits in B while A is live, or vice versa)
    bool modified=false;            // values of the rolled face were edited since the roll (hashValues(current) != recipe.resultHash)
    int changedValues=0;            // parameters whose value really changed (Outcome::changedParams); -1 when unknown (recipe restored from a saved state)
    juce::StringArray touchedGroups;
    int algoVersion=0;              // aether::rand::kRandAlgoVersion of the engine that made the roll
    int attempt=0;                  // accepted re-roll index (Config::attempt, 0..5)
    bool rejected=false;            // no patch passed validate(): nothing was written, no undo step, the recipe is unchanged
    juce::String note;              // why nothing changed: "all locked", "amount 0", "no change", "no safe patch", "wrong thread"
};

// Thin UI facade over aether::rand::RandomizeController (generate + SnapshotRing + LockSet + RandomRecipe, §P).
class RandomizeBackend {
public:
    virtual ~RandomizeBackend()=default;
    // Lockable sections in display order (UI chips) and the ones that start locked (none since 2.3.1: everything is open by default).
    // Real LockSet is finer (§P.3). MixGlobal / System (bus volume, master, global mode) are Never in the engine and have no chip.
    virtual juce::StringArray lockGroups() const=0;
    virtual juce::StringArray defaultLocked() const=0;
    // Live slot (= SnapshotRing Target::Live) / into B (= Target::AlternateSnapshot). Both push one SnapshotRing step.
    virtual RandomizeRecipe randomize(const RandomizeRequest&)=0;
    virtual RandomizeRecipe randomizeIntoB(const RandomizeRequest&)=0;
    // Lock chips <-> the controller's LockSet (saved with the plugin state). The default applies until a lock was ever written.
    virtual juce::StringArray lockedGroups() const { return defaultLocked(); }
    virtual void setLockedGroups(const juce::StringArray&) {}
    // Undo Random; false when the ring is empty. undoDepth() is 0..randomUndoSteps (== SnapshotRing capacity 16).
    virtual bool undo()=0;
    virtual int undoDepth() const=0;
    // Recipe of the roll now on screen (empty when the state is not a random roll), and the way to drop it.
    virtual std::optional<RandomizeRecipe> recipe() const=0;
    virtual void forgetRecipe()=0;
};
// RandSpec §1.2 groups in spec order, and the group of a parameter id (Chain, ModuleParams, FM, Source, Motion, MixGlobal, System).
juce::StringArray randomSpecGroups();
juce::String randomGroupOf(const juce::String& parameterId);
// Real backend on aether::rand. Create and use it on the message thread; generate() is not realtime safe (Patch holds a vector),
// so a call from any other thread is refused (RandomizeRecipe::rejected, nothing written).
std::unique_ptr<RandomizeBackend> makeEngineBackend(AetherAudioProcessor&);

// Colours and text hooks of the host editor, so the popover looks like the rest of Atlas and follows its text floor.
struct RandomizeStyle {
    juce::Colour paper,light,ink,muted,earthText,line,blue;
    std::function<void(juce::Graphics&,const juce::String&,juce::Rectangle<int>,float size,juce::Colour,int justification)> drawText;
    std::function<juce::Font(float)> font;                      // mono font at an effective px size
    std::function<float(const juce::Component&)> textFloor;     // smallest legible logical text size for a component
};

// A painted text of the popover (area in popover coordinates, nominal size; the drawn size is max(floor,size)). For layout tests.
struct RandomizeTextItem {
    juce::String id,text; juce::Rectangle<int> area; float size; juce::Colour colour,background;
    int justification=juce::Justification::centredLeft;
    bool ellipsis=false; // may be cut with an ellipsis (names of presets); every other text must fit its area
    bool widget=false;   // drawn by a button / editor of the popover, not by the popover itself
};

// Small icon button (caret / close) drawn from paths, in the Atlas button style.
class GlyphButton final:public juce::Button {
public:
    enum class Kind { caret, close };
    GlyphButton(const RandomizeStyle&,Kind);
    void paintButton(juce::Graphics&,bool hover,bool down) override;
    void setOpenState(bool open) { opened=open; repaint(); }
private:
    RandomizeStyle style; Kind kind; bool opened=false;
};

class RandomizePanel final:public juce::Component {
public:
    static constexpr int panelWidth=336;
    RandomizePanel(RandomizeBackend&,RandomizeStyle,std::function<void()> changed);
    ~RandomizePanel() override;
    int panelHeight() const noexcept { return getHeight(); }
    void setOpen(bool);
    bool isOpen() const noexcept { return open; }
    void addAnchor(juce::Component& c) { anchors.push_back(&c); }
    // Settings
    RandomizeMode mode() const noexcept { return requestMode; }
    void setMode(RandomizeMode);
    float amount() const noexcept { return requestAmount; }
    void setAmount(float);
    bool pinned() const noexcept { return seedPinned; }
    void setPinned(bool);
    juce::String seedText() const;
    bool setSeedText(const juce::String&);   // false (and the old seed is kept) when the text is not a seed
    void newSeed();
    void setSeedSource(std::function<std::uint64_t()>);   // tests; the default draws from the system random
    juce::StringArray lockedGroups() const;
    bool isLocked(const juce::String& group) const;
    void setLocked(const juce::String& group,bool locked);
    void resetLocks();
    // Reserved "re-rolling" state (audition stage, loudness compensation is the sound engineer's P2): disables the roll and undo buttons.
    void setBusy(bool);
    bool isBusy() const noexcept { return busy; }
    // Actions (the same as the buttons)
    RandomizeRecipe roll(bool intoB,RandomizeScope scope=RandomizeScope::all);
    RandomizeRecipe rollMotion() { return roll(false,RandomizeScope::motion); }   // "Randomize Motion": Motion group only
    bool undo();
    int undoDepth() const { return backend.undoDepth(); }
    // Naming: "(Random) <seed>" while the roll on screen sits on the live face; empty otherwise. notePreset (called by the editor's timer) drops
    // the recipe when the processor's preset name changed to something other than the roll's own name, and follows lock / "modified" changes.
    juce::String presetLabel() const;
    juce::String sourcePreset() const;
    void notePreset(const juce::String& processorPresetName,bool onSlotB);
    void clearRecipe();   // the user loaded a preset: the shown state is no longer a roll
    void setHint(const juce::String&);
    std::vector<RandomizeTextItem> textItems() const;
    // Component
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    class PanelButton; class LockChip; struct OutsideClicks;
    RandomizeBackend& backend; RandomizeStyle style; std::function<void()> changed;
    bool open=false,seedPinned=false,busy=false,havePresetName=false,shownModified=false;
    RandomizeMode requestMode=RandomizeMode::full; float requestAmount=1.f; std::uint64_t seed=0;
    juce::String hint,presetName,noChangeNote;
    juce::StringArray locked;
    std::function<std::uint64_t()> seedSource;
    std::vector<juce::Component*> anchors;
    std::unique_ptr<OutsideClicks> outside;
    std::unique_ptr<GlyphButton> closeButton;
    std::unique_ptr<PanelButton> mutateButton,fullButton,pinButton,newButton,rollButton,rollMotionButton,rollBButton,undoButton;
    std::vector<std::unique_ptr<LockChip>> chips;
    juce::Slider amountSlider; juce::TextEditor seedEdit;
    struct Rows { juce::Rectangle<int> title,amountLabel,amountValue,seedLabel,lockHeading,lockCount,presetLabel,presetValue,presetSource,hintLine,lastLine; } rows;
    void refreshWidgets();
    void commitSeedText();
    float floorSize() const;
    juce::String undoText() const;
    juce::String sourceText() const;
    juce::String lastRollText() const;
    juce::String lockCountText() const;
    static int heightFor(int groups);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RandomizePanel)
};
}
