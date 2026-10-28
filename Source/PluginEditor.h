#pragma once
#include "PluginProcessor.h"
#include "RandomizePanel.h"
#include <functional>
#include <memory>
#include <vector>

class AetherAudioProcessorEditor final : public juce::AudioProcessorEditor {
public:
    explicit AetherAudioProcessorEditor(AetherAudioProcessor&);
    ~AetherAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void setMinimalMode(bool);
    void setWorkspace(int);
    void selectSlot(int);
    bool performUiAction(const juce::String&);
    // Geometry (in Atlas content-component coordinates) of the FM workspace hint texts and the horizontal rule above the operator columns; used by layout tests.
    struct FmPaintGeometry { juce::Rectangle<int> matrixHint, liveVoices; int dividerY=0, dividerX0=0, dividerX1=0; };
    FmPaintGeometry fmPaintGeometry() const;
    // Source workspace: every painted (non-component) text with its area and final text colour, and the status line shown for the current Mode / engine / On state. Used by layout tests.
    struct SourcePaintText { juce::Rectangle<int> area; juce::String text; float size; bool display; juce::Colour colour; };
    std::vector<SourcePaintText> sourcePaintTexts() const;
    juce::String sourceStatus();
    // Header notice shown right of the tabs on the Patch / FM / Motion pages (empty when none), refreshed from the current parameters. Used by layout tests.
    juce::String pageNotice();
    // Modulation destination grouping: group label of a destination name ("Node 3 / Mix" -> "Node 3"; empty for None), and the filler that adds the items (ids = index + 1) with a heading per group.
    static juce::String modulationTargetGroup(const juce::String& item);
    static void fillModulationTargets(juce::ComboBox& combo,const juce::StringArray& items);
    // Test / snapshot hook: 0 = effect edition (FX + Resynth), 1 = Synth edition, -1 = compile-time edition (JucePlugin_IsSynth). Call before creating an editor.
    static void setEditionForTesting(int edition);
    // Randomize (2.3.0). On by default: without a factory the editor builds aether::ui::makeEngineBackend(processor), which drives the processor's
    // RandomizeController. Tests and snapshot tools may install their own factory before creating an editor; an empty function restores the default.
    using RandomizeBackendFactory=std::function<std::unique_ptr<aether::ui::RandomizeBackend>(AetherAudioProcessor&)>;
    static void setRandomizeBackendFactory(RandomizeBackendFactory);
    // The popover (nullptr only when the backend could not be created), and every text it paints or hosts with the size actually drawn (nominal size raised to the legible floor). Used by layout tests.
    aether::ui::RandomizePanel* randomizePanel();
    std::vector<aether::ui::RandomizeTextItem> randomizeTexts();
private:
    struct Impl;
    std::unique_ptr<Impl> ui;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherAudioProcessorEditor)
};
