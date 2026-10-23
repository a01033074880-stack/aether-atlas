#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <aether/Modular.h>
#include <aether/FmSynth.h>

namespace aether {
struct ParameterSpec {
    juce::String id,name;
    float minimum, maximum, defaultValue, skew;
    juce::String unit,group;
    juce::StringArray choices;
    bool boolean = false;
};
enum ParamIndex {
    input, output, mix, bypass, mode, order,
    spectralOn, spectralAmount, spectralThreshold, spectralFocus, transientProtect,
    modalOn, modalMix, modalDecay, modalTune, modalBrightness, modalMaterial,
    colourOn, driveDb, driveTone, driveBias, driveModel,
    spaceOn, spaceMix, spaceSize, spaceDecay, spaceDamping, dispersion, freeze,
    masterOn, masterWidth, masterCeiling, masterRelease, dither,
    modRate, modDepth, modTarget, modShape, modSync, parameterCount
};
inline constexpr int graphOffset=parameterCount;
inline constexpr int nodeOffset=graphOffset+4;
inline constexpr int routeOffset=nodeOffset+modular::slotCount*11;
inline constexpr int lfoOffset=routeOffset+modular::slotCount*modular::slotCount;
inline constexpr int modulationOffset=lfoOffset+16;
inline constexpr int fmOffset=modulationOffset+modular::modulationCount*3;
inline constexpr int operatorOffset=fmOffset+12;
inline constexpr int fmEdgeOffset=operatorOffset+fm::operatorCount*6;
inline constexpr int totalParameterCount=fmEdgeOffset+fm::operatorCount*fm::operatorCount;
const std::vector<ParameterSpec>& parameterSpecs();
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
juce::StringArray orderNames();
juce::StringArray routingNames();
juce::StringArray moduleNames();
juce::StringArray modulationTargetNames();
modular::Parameters defaultGraph();
}
