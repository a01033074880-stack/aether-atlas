#pragma once
#include "PluginProcessor.h"
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
private:
    struct Impl;
    std::unique_ptr<Impl> ui;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherAudioProcessorEditor)
};
