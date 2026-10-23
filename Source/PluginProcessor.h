#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <aether/Engine.h>
#include "Parameters.h"
#include <array>
#include <atomic>

class AetherAudioProcessor final : public juce::AudioProcessor {
public:
    AetherAudioProcessor();
    ~AetherAudioProcessor() override = default;
    void prepareToPlay(double,int) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int,const juce::String&) override {}
    juce::AudioProcessorParameter* getBypassParameter() const override;
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;

    juce::AudioProcessorValueTreeState state;
    std::atomic<float> inputMeterDb{-100},outputMeterDb{-100},gainReductionDb{0},cpuPercent{-1},detectedPitchHz{0};
    std::atomic<bool> analysisEnabled{true};
    int readAnalysis(float*,int) noexcept;
    static juce::StringArray factoryPresetNames();
    void loadFactoryPreset(int);
    juce::String currentPresetName() const;
    juce::Result savePreset(const juce::File&);
    juce::Result loadPreset(const juce::File&);
    void toggleAB();
    bool isSnapshotB() const noexcept { return snapshotB.load(); }
    void requestPanic() noexcept { panic.store(true); }

private:
    aether::Engine engine;
    std::array<std::atomic<float>*,aether::parameterCount> raw{};
    static constexpr int scratchSize=512,analysisSize=16384;
    std::array<float,scratchSize> left{},right{},side{};
    std::array<float,analysisSize> analysis{};
    juce::AbstractFifo analysisFifo{analysisSize};
    std::atomic<bool> panic{false},snapshotB{false};
    std::atomic<int> currentProgram{0};
    juce::ValueTree snapshot;
    mutable juce::CriticalSection metadataLock;
    juce::String presetName="Gentle Orbit";
    double currentRate=48000;
    bool prepared=false,wasBypassed=false;
    aether::Parameters parameters() const noexcept;
    void handleMidi(const juce::MidiMessage&) noexcept;
    void renderSegment(juce::AudioBuffer<float>&,int,int,const aether::Parameters&);
    void publishAnalysis(const float*,const float*,int) noexcept;
    void applyValues(const juce::var&);
    juce::var presetObject(const juce::String&) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherAudioProcessor)
};
