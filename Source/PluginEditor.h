#pragma once
#include "PluginProcessor.h"
#include <juce_dsp/juce_dsp.h>
#include <memory>

class AetherAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer {
public:
    explicit AetherAudioProcessorEditor(AetherAudioProcessor&);
    ~AetherAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void setMinimalMode(bool);

private:
    class Theme;
    class Control;
    AetherAudioProcessor& processor;
    std::unique_ptr<Theme> theme;
    std::vector<std::unique_ptr<Control>> controls;
    juce::ComboBox presets;
    juce::TextButton loadButton{"Load"}, saveButton{"Save"}, abButton{"A / B"},
                     minimalButton{"Minimal"}, panicButton{"Panic"};
    juce::TooltipWindow tooltips{this, 650};
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> spectrumBounds, footerBounds, orderBounds;
    std::array<juce::Rectangle<int>, 5> cards{};
    std::array<float, 2048> fftInput{};
    std::array<float, 4096> fftData{};
    std::array<float, 256> spectrum{};
    juce::dsp::FFT fft{11};
    juce::dsp::WindowingFunction<float> window{2048, juce::dsp::WindowingFunction<float>::hann};
    int fftWrite = 0;
    bool minimal = false, receivedAudio = false;
    juce::String displayedPreset;
    void timerCallback() override;
    void updateAnalysis();
    void choosePresetFile(bool save);
    void paintSpectrum(juce::Graphics&);
    void paintMeters(juce::Graphics&, juce::Rectangle<int>);
    Control& control(int index);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherAudioProcessorEditor)
};
