#include "PluginProcessor.h"
#include <cmath>

namespace {
using Setting=std::pair<const char*,float>;
struct Factory { const char* name; std::initializer_list<Setting> values; };
const std::array<Factory,12> factories {{
    {"Gentle Orbit",{{"spectral.amount",0.35f},{"space.mix",0.12f}}},
    {"Clean Canvas",{{"spectral.amount",0},{"space.mix",0},{"master.ceiling",0}}},
    {"Vocal Silhouette",{{"spectral.amount",0.78f},{"spectral.threshold",-32},{"spectral.focus",5800},{"colour.drive",3},{"space.mix",0.08f}}},
    {"Warm Console",{{"spectral.amount",0.12f},{"colour.drive",9},{"colour.tone",0.35f},{"colour.model",0},{"space.mix",0.03f}}},
    {"Ember Guitar",{{"colour.drive",22},{"colour.model",1},{"colour.bias",0.12f},{"colour.tone",0.38f},{"space.mix",0.16f},{"space.decay",0.9f}}},
    {"Iron Drum Room",{{"colour.drive",13},{"colour.model",2},{"spectral.transient",0.95f},{"space.mix",0.24f},{"space.decay",0.6f},{"space.size",0.25f}}},
    {"Glass Resonator",{{"modal.mix",0.55f},{"modal.material",1},{"modal.decay",4.5f},{"modal.brightness",0.8f},{"space.mix",0.3f}}},
    {"Wooden Echoes",{{"modal.mix",0.42f},{"modal.material",2},{"modal.decay",1.4f},{"modal.brightness",0.28f},{"space.mix",0.22f}}},
    {"Nebula Drift",{{"space.mix",0.72f},{"space.size",0.9f},{"space.decay",14},{"space.dispersion",0.8f},{"mod.depth",0.2f},{"mod.target",3},{"mod.rate",0.11f}}},
    {"Resynth Halo",{{"global.mode",1},{"modal.mix",0.7f},{"modal.decay",2.8f},{"space.mix",0.35f},{"space.decay",6}}},
    {"Prism Keys",{{"global.mode",2},{"modal.mix",0.32f},{"modal.material",1},{"modal.decay",2},{"space.mix",0.22f},{"colour.drive",3}}},
    {"Copper Strings",{{"global.mode",2},{"modal.mix",0.2f},{"modal.material",0},{"modal.brightness",0.35f},{"modal.decay",3.8f},{"space.mix",0.26f},{"colour.drive",6}}}
}};
}
juce::StringArray AetherAudioProcessor::factoryPresetNames() {
    juce::StringArray names; for(const auto& f:factories) names.add(f.name); return names;
}
juce::String AetherAudioProcessor::currentPresetName() const { const juce::ScopedLock lock(metadataLock); return presetName; }
void AetherAudioProcessor::loadFactoryPreset(int index) {
    index=juce::jlimit(0,static_cast<int>(factories.size())-1,index);
    for(const auto& spec:aether::parameterSpecs()) {
        if(juce::String(spec.id)=="global.bypass") continue;
        float value=spec.defaultValue;
#if JucePlugin_IsSynth
        if(juce::String(spec.id)=="global.mode") value=2;
#endif
        for(const auto& item:factories[static_cast<size_t>(index)].values) if(juce::String(spec.id)==item.first) value=item.second;
        // The instrument variant always remains an instrument when selecting an FX factory sound.
#if JucePlugin_IsSynth
        if(juce::String(spec.id)=="global.mode" && value==0) value=2;
#endif
        if(auto* param=state.getParameter(spec.id)) { param->beginChangeGesture(); param->setValueNotifyingHost(param->convertTo0to1(value)); param->endChangeGesture(); }
    }
    { const juce::ScopedLock lock(metadataLock); presetName=factories[static_cast<size_t>(index)].name; }
    currentProgram.store(index); requestPanic();
}
juce::var AetherAudioProcessor::presetObject(const juce::String& name) const {
    auto* root=new juce::DynamicObject(); root->setProperty("product","AetherMicro"); root->setProperty("version","1.0.0"); root->setProperty("schema",1); root->setProperty("name",name);
    auto* values=new juce::DynamicObject();
    for(const auto& spec:aether::parameterSpecs()) values->setProperty(spec.id,state.getRawParameterValue(spec.id)->load());
    root->setProperty("parameters",juce::var(values)); return juce::var(root);
}
juce::Result AetherAudioProcessor::savePreset(const juce::File& file) {
    const auto name=file.getFileNameWithoutExtension();
    const auto text=juce::JSON::toString(presetObject(name),false);
    if(!file.replaceWithText(text)) return juce::Result::fail("Cannot write preset: "+file.getFullPathName());
    { const juce::ScopedLock lock(metadataLock); presetName=name; }
    return juce::Result::ok();
}
void AetherAudioProcessor::applyValues(const juce::var& values) {
    auto* object=values.getDynamicObject(); if(object==nullptr) return;
    for(const auto& spec:aether::parameterSpecs()) if(object->hasProperty(spec.id)) {
        const float value=static_cast<float>(object->getProperty(spec.id));
        if(auto* param=state.getParameter(spec.id)) { param->beginChangeGesture(); param->setValueNotifyingHost(param->convertTo0to1(juce::jlimit(spec.minimum,spec.maximum,value))); param->endChangeGesture(); }
    }
}
juce::Result AetherAudioProcessor::loadPreset(const juce::File& file) {
    if(!file.existsAsFile() || file.getSize()>1024*1024) return juce::Result::fail("Preset missing or larger than 1 MB.");
    juce::var root; auto result=juce::JSON::parse(file.loadFileAsString(),root);
    if(result.failed()) return juce::Result::fail("Invalid preset JSON.");
    if(root["product"].toString()!="AetherMicro" || static_cast<int>(root["schema"])!=1 || !root["parameters"].isObject()) return juce::Result::fail("Unsupported preset product or schema.");
    auto values=root["parameters"];
    for(const auto& spec:aether::parameterSpecs()) if(values.hasProperty(spec.id)) {
        auto value=values[spec.id];
        if(!(value.isDouble()||value.isInt()||value.isInt64()||value.isBool()) || !std::isfinite(static_cast<double>(value))) return juce::Result::fail("Preset contains invalid parameter values.");
    }
    applyValues(values);
    { const juce::ScopedLock lock(metadataLock); presetName=root["name"].toString().substring(0,128); }
    requestPanic(); return juce::Result::ok();
}
void AetherAudioProcessor::toggleAB() {
    auto current=state.copyState();
    if(snapshot.isValid()) state.replaceState(snapshot.createCopy());
    snapshot=current; snapshotB.store(!snapshotB.load()); requestPanic();
}
