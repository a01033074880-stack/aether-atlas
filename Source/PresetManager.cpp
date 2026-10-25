#include "PluginProcessor.h"
#include <cmath>

namespace {
using Setting=std::pair<const char*,float>;
struct Factory { const char* name; std::initializer_list<Setting> values; };
const std::array<Factory,36> factories {{
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
    {"Copper Strings",{{"global.mode",2},{"modal.mix",0.2f},{"modal.material",0},{"modal.brightness",0.35f},{"modal.decay",3.8f},{"space.mix",0.26f},{"colour.drive",6}}},
    {"Atlas / Suspended Paper",{{"routing.algorithm",3},{"graph.trim",-3}}},
    {"Atlas / Ceramic Current",{{"routing.algorithm",1},{"graph.trim",-4}}},
    {"Atlas / Shattered Prism",{{"routing.algorithm",4},{"graph.trim",-4}}},
    {"Atlas / Grain Cathedral",{{"routing.algorithm",2},{"graph.trim",-4}}},
    {"Atlas / Impossible String",{{"routing.algorithm",5},{"graph.trim",-3}}},
    {"Atlas / Folded Sun",{{"routing.algorithm",0},{"graph.trim",-6}}},
    {"Atlas / Clockwork Dust",{{"routing.algorithm",6},{"graph.trim",-4}}},
    {"Atlas / Diode Garden",{{"routing.algorithm",7},{"graph.trim",-4}}},
    {"Atlas / Frozen Architecture",{{"routing.algorithm",3},{"graph.trim",-4}}},
    {"Atlas / Sideband Mirage",{{"routing.algorithm",1},{"graph.trim",-4}}},
    {"Atlas / Cobalt Tape",{{"routing.algorithm",8},{"graph.trim",-3}}},
    {"Atlas / Chaotic Orbit",{{"routing.algorithm",9},{"graph.feedback",0.25f},{"graph.trim",-6}}},
    {"FM / Glass Staircase",{{"global.mode",2},{"fm.algorithm",4},{"fm.index",3},{"fm.attack",0.003f},{"fm.release",1.5f}}},
    {"FM / Brass Compass",{{"global.mode",2},{"fm.algorithm",9},{"fm.index",2.8f},{"fm.decay",0.7f},{"fm.release",0.4f}}},
    {"FM / Porcelain Keys",{{"global.mode",2},{"fm.algorithm",12},{"fm.index",1.8f},{"fm.sustain",0.3f},{"fm.release",1.2f}}},
    {"FM / Inverted Bells",{{"global.mode",2},{"fm.algorithm",18},{"fm.index",5},{"fm.sustain",0.1f},{"fm.release",3}}},
    {"FM / Mercury Thread",{{"global.mode",2},{"fm.algorithm",23},{"fm.index",2.2f},{"fm.morph",0.35f},{"fm.attack",0.2f},{"fm.release",2}}},
    {"FM / Velvet Satellite",{{"global.mode",2},{"fm.algorithm",27},{"fm.index",1.2f},{"fm.attack",0.6f},{"fm.release",3}}},
    {"FM / Broken Geometry",{{"global.mode",2},{"fm.algorithm",30},{"fm.index",6},{"fm.feedback",0.35f}}},
    {"FM / Singing Glass",{{"global.mode",1},{"fm.algorithm",6},{"fm.index",2.5f},{"graph.trim",-3}}},
    {"Motion / Breathing Stone",{{"routing.algorithm",2},{"graph.trim",-3}}},
    {"Motion / Slow Collisions",{{"routing.algorithm",4},{"graph.trim",-4}}},
    {"Motion / Lunar Loom",{{"routing.algorithm",10},{"graph.feedback",0.2f},{"graph.trim",-5}}},
    {"Motion / Stochastic Room",{{"routing.algorithm",11},{"graph.feedback",0.15f},{"graph.trim",-5}}}
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
    if(index<12) {
        setParameterValue("graph.enabled",0);setParameterValue("fm.enabled",0);
    } else {
        setParameterValue("graph.enabled",1);setParameterValue("fm.enabled",1);
        using T=aether::modular::ModuleType;
        std::array<T,6> chain{};
        switch(index-12) {
            case 0:chain={T::Spectral,T::WdfDiode,T::Chorus,T::Diffuser,T::Space,T::Tape};break;
            case 1:chain={T::Wavefolder,T::Resonator,T::FrequencyShift,T::Diffuser,T::Space,T::Spectral};break;
            case 2:chain={T::PrismPitch,T::FrequencyShift,T::RingMod,T::Comb,T::Space,T::Chorus};break;
            case 3:chain={T::GrainCloud,T::Ladder,T::Diffuser,T::Space,T::Tape,T::PrismPitch};break;
            case 4:chain={T::String,T::Modal,T::Colour,T::Chorus,T::Space,T::Diffuser};break;
            case 5:chain={T::Wavefolder,T::WdfDiode,T::Ladder,T::Tape,T::Space,T::Spectral};break;
            case 6:chain={T::LoFi,T::RingMod,T::Comb,T::GrainCloud,T::Space,T::Spectral};break;
            case 7:chain={T::WdfDiode,T::Resonator,T::Ladder,T::Tape,T::Space,T::Chorus};break;
            case 8:chain={T::SpectralFreeze,T::PrismPitch,T::Diffuser,T::Space,T::Tape,T::Chorus};break;
            case 9:chain={T::FrequencyShift,T::RingMod,T::Chorus,T::Comb,T::Space,T::Diffuser};break;
            case 10:chain={T::Tape,T::Colour,T::Ladder,T::Diffuser,T::Space,T::Chorus};break;
            case 11:chain={T::Chaos,T::Comb,T::FrequencyShift,T::Wavefolder,T::Space,T::Spectral};break;
            case 12:chain={T::Resonator,T::PrismPitch,T::Diffuser,T::Space,T::Chorus,T::Spectral};break;
            case 13:chain={T::Ladder,T::Colour,T::Tape,T::Chorus,T::Space,T::Spectral};break;
            case 14:chain={T::Modal,T::WdfDiode,T::Diffuser,T::Space,T::Chorus,T::Spectral};break;
            case 15:chain={T::PrismPitch,T::FrequencyShift,T::Comb,T::Space,T::Diffuser,T::Spectral};break;
            case 16:chain={T::Wavefolder,T::Chorus,T::GrainCloud,T::Space,T::Diffuser,T::Spectral};break;
            case 17:chain={T::Ladder,T::Tape,T::Chorus,T::Space,T::Diffuser,T::Spectral};break;
            case 18:chain={T::WdfDiode,T::LoFi,T::RingMod,T::Comb,T::Space,T::Spectral};break;
            case 19:chain={T::Resonator,T::GrainCloud,T::Diffuser,T::Space,T::Chorus,T::Spectral};break;
            case 20:chain={T::Ladder,T::Modal,T::Chorus,T::Space,T::Diffuser,T::Spectral};break;
            case 21:chain={T::Wavefolder,T::FrequencyShift,T::GrainCloud,T::Comb,T::Space,T::Spectral};break;
            case 22:chain={T::PrismPitch,T::Tape,T::Resonator,T::Diffuser,T::Space,T::Spectral};break;
            default:chain={T::Chaos,T::LoFi,T::Diffuser,T::Space,T::Chorus,T::Spectral};break;
        }
        for(int slot=0;slot<aether::modular::slotCount;++slot) {
            assignModule(slot,slot<6?static_cast<int>(chain[static_cast<size_t>(slot)]):0);
            const auto id="node."+juce::String(slot+1)+".";
            setParameterValue(id+"mix",slot<6?(chain[static_cast<size_t>(slot)]==T::Space?0.3f:0.45f):1.f);
            if(slot>=6) setParameterValue(id+"on",0);
        }
        if(index>=24 && index<=31) {
            const float ratios[]={1,2,3,1.5f,4,0.5f};
            for(int op=1;op<=6;++op) setParameterValue("fm.op."+juce::String(op)+".ratio",ratios[(op+index)%6]);
        }
        if(index>=32) {
            setParameterValue("modulation.1.source",1);setParameterValue("modulation.1.target",1);setParameterValue("modulation.1.depth",0.3f);
            setParameterValue("modulation.2.source",2);setParameterValue("modulation.2.target",17);setParameterValue("modulation.2.depth",0.2f);
            setParameterValue("lfo.1.rate",0.07f);setParameterValue("lfo.2.rate",0.13f);
        }
        applyRoutingAlgorithm(static_cast<int>(parameterValue("routing.algorithm")));
    }
    { const juce::ScopedLock lock(metadataLock); presetName=factories[static_cast<size_t>(index)].name; }
    currentProgram.store(index); requestPanic();
}
juce::var AetherAudioProcessor::presetObject(const juce::String& name) const {
    // Schema 4 only when a motion row uses destination 85..112 (see aether::stateSchemaDestinations); every other preset stays schema 3.
    int presetSchema=aether::stateSchema;
    for(int n=1;n<=aether::modular::modulationCount;++n) {
        const float target=state.getRawParameterValue("modulation."+juce::String(n)+".target")->load();
        if(std::isfinite(target) && aether::modulationTargetNeedsSchema4(target)) presetSchema=aether::stateSchemaDestinations;
    }
    auto* root=new juce::DynamicObject(); root->setProperty("product","AetherMicro"); root->setProperty("version","2.0.0"); root->setProperty("schema",presetSchema); root->setProperty("name",name);
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
    for(const auto& spec:aether::parameterSpecs()) {
        // Presets from before the source layer do not mention its parameters: they return to their defaults (layer off).
        const bool present=object->hasProperty(spec.id);
        if(!present && spec.version<2) continue;
        const float value=present ? static_cast<float>(object->getProperty(spec.id)) : spec.defaultValue;
        if(auto* param=state.getParameter(spec.id)) { param->beginChangeGesture(); param->setValueNotifyingHost(param->convertTo0to1(juce::jlimit(spec.minimum,spec.maximum,value))); param->endChangeGesture(); }
    }
}
juce::Result AetherAudioProcessor::loadPreset(const juce::File& file) {
    if(!file.existsAsFile() || file.getSize()>1024*1024) return juce::Result::fail("Preset missing or larger than 1 MB.");
    juce::var root; auto result=juce::JSON::parse(file.loadFileAsString(),root);
    if(result.failed()) return juce::Result::fail("Invalid preset JSON.");
    const int schema=static_cast<int>(root["schema"]);
    if(root["product"].toString()!="AetherMicro" || schema<1 || schema>aether::stateSchemaNewest || !root["parameters"].isObject()) return juce::Result::fail("Unsupported preset product or schema.");
    auto values=root["parameters"];
    for(const auto& spec:aether::parameterSpecs()) if(values.hasProperty(spec.id)) {
        auto value=values.getProperty(juce::Identifier(spec.id),{});
        if(!(value.isDouble()||value.isInt()||value.isInt64()||value.isBool()) || !std::isfinite(static_cast<double>(value))) return juce::Result::fail("Preset contains invalid parameter values.");
    }
    applyValues(values);
    if(schema==1) { setParameterValue("graph.enabled",0);setParameterValue("fm.enabled",0); }
    { const juce::ScopedLock lock(metadataLock); presetName=root["name"].toString().substring(0,128); }
    requestPanic(); return juce::Result::ok();
}
void AetherAudioProcessor::toggleAB() {
    auto current=state.copyState();
    juce::ValueTree target;
    { const juce::ScopedLock lock(metadataLock);
      target=snapshot.createCopy(); snapshot=current; snapshotB.store(!snapshotB.load());
    }
    if(target.isValid()) state.replaceState(target);
    requestPanic();
}
