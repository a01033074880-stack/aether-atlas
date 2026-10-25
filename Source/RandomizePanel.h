#pragma once
#include "PluginProcessor.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// Randomize popover for the Atlas editor (stage B of 2.3.0).
//
// The engine side is hidden behind RandomizeBackend, a small pure-virtual interface: the UI only knows "randomize with seed /
// amount / locked groups", an undo ring of at most randomUndoSteps steps, the recipe of the roll now on screen, and "write the
// result into the B slot". docs/randomization-spec.md is not frozen yet, so only MockBackend implements it today (tests and
// snapshots); the plugin editor shows no Randomize controls until a backend factory is installed.
namespace aether::ui {

inline constexpr int randomUndoSteps=16;
// Seeds are 40 bit and shown as 8 Crockford base32 characters in two groups of four ("A7F3-92C1").
inline constexpr std::uint64_t randomSeedMask=(std::uint64_t(1)<<40)-1;
juce::String formatRandomSeed(std::uint64_t seed);
bool parseRandomSeed(const juce::String& text,std::uint64_t& seed);
// The preset name shown after a roll.
juce::String randomPresetLabel(std::uint64_t seed);

enum class RandomizeMode { mutate, full };

struct RandomizeRequest {
    std::uint64_t seed=0;
    RandomizeMode mode=RandomizeMode::mutate;
    float amount=.35f;              // 0..1
    juce::StringArray lockedGroups; // names from RandomizeBackend::lockGroups(); their parameters are left untouched
};

// What produced the state now on screen. Metadata only: it is not part of the preset format.
struct RandomizeRecipe {
    std::uint64_t seed=0;
    RandomizeMode mode=RandomizeMode::mutate;
    float amount=0;
    juce::StringArray lockedGroups;
    juce::String sourcePreset;      // preset name before the first roll of a chain ("from <name>")
    bool intoB=false;               // the roll was written into the B slot
    bool onSlotB=false;             // the slot (A=false, B=true) that carries the result
    int changedValues=0;
    juce::StringArray touchedGroups;
};

class RandomizeBackend {
public:
    virtual ~RandomizeBackend()=default;
    // Lockable parameter groups in display order, and the ones that start locked.
    virtual juce::StringArray lockGroups() const=0;
    virtual juce::StringArray defaultLocked() const=0;
    // Randomize the live slot / write the result into the B slot (A stays as it was). Both push one undo step.
    virtual RandomizeRecipe randomize(const RandomizeRequest&)=0;
    virtual RandomizeRecipe randomizeIntoB(const RandomizeRequest&)=0;
    // Undo the last roll; false when the ring is empty. undoDepth() is the number of steps left (0..randomUndoSteps).
    virtual bool undo()=0;
    virtual int undoDepth() const=0;
    // Recipe of the roll that is on screen now (empty when the state is not a random roll), and the way to drop it.
    virtual std::optional<RandomizeRecipe> recipe() const=0;
    virtual void forgetRecipe()=0;
};
// Deterministic stand-in that really moves the parameters of the processor (continuous ones only), honours locks and keeps a
// 16 step undo ring. For tests and snapshots; never part of a shipped build path.
std::unique_ptr<RandomizeBackend> makeMockBackend(AetherAudioProcessor&);

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
    // Actions (the same as the buttons)
    RandomizeRecipe roll(bool intoB);
    bool undo();
    int undoDepth() const { return backend.undoDepth(); }
    // Naming: "(Random) <seed>" while the roll on screen sits in the slot now shown; empty otherwise. notePreset (called by the editor's timer) drops the recipe when the processor's preset changed.
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
    bool open=false,seedPinned=false,currentSlotB=false;
    RandomizeMode requestMode=RandomizeMode::mutate; float requestAmount=.35f; std::uint64_t seed=0;
    juce::String hint,presetName;
    juce::StringArray locked;
    std::function<std::uint64_t()> seedSource;
    std::vector<juce::Component*> anchors;
    std::unique_ptr<OutsideClicks> outside;
    std::unique_ptr<GlyphButton> closeButton;
    std::unique_ptr<PanelButton> mutateButton,fullButton,pinButton,newButton,rollButton,rollBButton,undoButton;
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
