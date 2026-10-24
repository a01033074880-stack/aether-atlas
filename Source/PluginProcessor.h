#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <aether/Engine.h>
#include <aether/Chaos.h>
#include <aether/PhaseGeometry.h>
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
    double getTailLengthSeconds() const override;
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
    float parameterValue(const juce::String&) const;
    void setParameterValue(const juce::String&,float);
    void applyRoutingAlgorithm(int);
    void setGraphEdge(int source,int destination,float gain);
    void prepareGraphEditing();
    void assignModule(int slot,int moduleType);
    void applyFmAlgorithm(int);
    void setFmEdge(int source,int destination,float depth);
    void prepareFmEditing();
    std::array<std::atomic<float>,aether::modular::slotCount> nodeMeters{};
    std::array<std::atomic<float>,aether::modular::sourceCount> modulationMeters{};
    std::atomic<int> fmVoiceCount{0},textureLatencySamples{0};

private:
    aether::Engine engine;
    aether::Engine outputStage;
    aether::modular::GraphEngine graph;
    aether::fm::Synth fmEngine;
    // Source layer: two voice engines, at most one of them is ever active (parameter "source.engine").
    aether::source::PhaseGeometrySource phaseGeometry;
    aether::source::ChaosSource chaosSource;
    int activeSourceEngine=0; // 0 off, 1 Phase Geometry, 2 Chaos; changes only at block starts
    std::array<std::atomic<float>*,aether::parameterCount> raw{};
    std::vector<std::atomic<float>*> allRaw;
    static constexpr int scratchSize=512,analysisSize=16384;
    std::array<float,scratchSize> left{},right{},side{};
    std::array<float,scratchSize> dryLeft{},dryRight{};
    std::array<float,scratchSize> sourceLeft{},sourceRight{};
    std::array<float,analysisSize> analysis{};
    juce::AbstractFifo analysisFifo{analysisSize};
    std::atomic<bool> panic{false},graphReset{false},snapshotB{false};
    std::atomic<int> currentProgram{0};
    juce::ValueTree snapshot;
    mutable juce::CriticalSection metadataLock;
    juce::String presetName="Atlas / Init";
    double currentRate=48000;
    bool prepared=false,wasBypassed=false;
    bool graphActive=true,fmActive=true,atlasSourceActive=true;
    float lastVelocity=0,lastNoteHz=0,fmInputGain=1,sourceInputGain=1,inputGainSmoothing=0.004f;
    float excitationEnvelope=0,excitationLow=0,excitationPrevious=0,excitationPitch=0;
    float excitationAttack=0.005f,excitationRelease=0.0003f,excitationLowpass=0.2f;
    double excitationCrossing=-1;
    std::uint64_t excitationClock=0;
    aether::Parameters parameters() const noexcept;
    aether::modular::Parameters graphParameters() const noexcept;
    aether::fm::Parameters fmParameters() const noexcept;
    aether::source::Source* activeSource() noexcept;
    void sourceCommon(aether::source::CommonParameters&) const noexcept;
    aether::source::PhaseGeometryParameters phaseGeometryParameters() const noexcept;
    aether::source::ChaosParameters chaosParameters() const noexcept;
    void analyseExcitation(float) noexcept;
    void handleMidi(const juce::MidiMessage&) noexcept;
    void renderSegment(juce::AudioBuffer<float>&,int,int,const aether::Parameters&);
    void publishAnalysis(const float*,const float*,int) noexcept;
    void applyValues(const juce::var&);
    juce::var presetObject(const juce::String&) const;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherAudioProcessor)
};
