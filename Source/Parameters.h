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
    // Added with the source layer (all trailing, so existing aggregate initialisers are unchanged):
    int version = 1;          // ParameterID version hint; the source layer parameters are version 2
    float step = 0;           // 0 = continuous, otherwise the value grid (integer parameters)
    bool automatable = true;  // false = stored with the state but not offered for host automation
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
// Mode (global.mode) restriction per plugin type, applied only where the processor reads the parameter; the stored
// value, the option list and the parameter ID never change.  0 = FX, 1 = Resynth, 2 = Synth.
//   AETHER Micro Synth (isSynth): always Synth (2), whatever is stored (old saves with 0/1 play as Synth).
//   AETHER Micro (effect):        FX (0) or Resynth (1); a stored 2 (Synth) falls back to FX (0).
// The plugin type is an explicit argument so both types can be tested in one executable.
inline constexpr int clampModeForPluginType(int mode,bool isSynth) noexcept {
    if(isSynth) return 2;
    return mode==1 ? 1 : 0;
}
inline constexpr int graphOffset=parameterCount;
inline constexpr int nodeOffset=graphOffset+4;
inline constexpr int routeOffset=nodeOffset+modular::slotCount*11;
inline constexpr int lfoOffset=routeOffset+modular::slotCount*modular::slotCount;
inline constexpr int modulationOffset=lfoOffset+16;
inline constexpr int fmOffset=modulationOffset+modular::modulationCount*3;
inline constexpr int operatorOffset=fmOffset+12;
inline constexpr int fmEdgeOffset=operatorOffset+fm::operatorCount*6;
// Parameters of the project before the source layer; their order and indices never change.
inline constexpr int legacyParameterCount=fmEdgeOffset+fm::operatorCount*fm::operatorCount;
// Source layer (Phase Geometry / Chaos voices). Appended after every legacy parameter, ParameterID version 2.
inline constexpr int sourceOffset=legacyParameterCount;
enum SourceParamIndex {
    sourceEngine, sourceEnabled, sourceAntiAlias, sourceSeed, sourceAttack, sourceDecay, sourceSustain, sourceRelease,
    sourceGain, sourceSpread, sourceTranspose,
    pgSequency, pgWalshMix, pgCzShape, pgCzDepth, pgCzEnvelope, pgCzVelocity, pgVpsD, pgVpsV, pgVpsRatio, pgPhaseRandom,
    chaosSegments, chaosDistribution, chaosAmpStep, chaosDurStep, chaosDurRange, chaosMomentum, chaosAmpBarrier,
    sourceParameterCount
};
inline constexpr int totalParameterCount=sourceOffset+sourceParameterCount;
// State / preset schema: 1 = v1 FX only, 2 = Atlas graph + FM, 3 = adds the source layer.
inline constexpr int stateSchema=3;
const std::vector<ParameterSpec>& parameterSpecs();
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
juce::StringArray orderNames();
juce::StringArray routingNames();
juce::StringArray moduleNames();
juce::StringArray modulationTargetNames();
juce::StringArray sourceEngineNames();
juce::StringArray czShapeNames();
juce::StringArray chaosDistributionNames();
modular::Parameters defaultGraph();
}
