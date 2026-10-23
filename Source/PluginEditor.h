#pragma once
#include "PluginProcessor.h"
#include <memory>

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
private:
    struct Impl;
    std::unique_ptr<Impl> ui;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherAudioProcessorEditor)
};
