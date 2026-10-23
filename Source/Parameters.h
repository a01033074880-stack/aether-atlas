#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace aether {
struct ParameterSpec {
    const char* id; const char* name;
    float minimum, maximum, defaultValue, skew;
    const char* unit; const char* group;
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
const std::vector<ParameterSpec>& parameterSpecs();
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
juce::StringArray orderNames();
}
