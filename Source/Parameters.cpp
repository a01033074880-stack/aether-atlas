#include "Parameters.h"
#include <algorithm>
#include <array>

namespace aether {
juce::StringArray orderNames() {
    juce::StringArray result;
    const std::array<juce::String, 4> names { "Spectral", "Modal", "Colour", "Space" };
    std::array<int,4> p {0,1,2,3};
    do { result.add(names[p[0]] + " > " + names[p[1]] + " > " + names[p[2]] + " > " + names[p[3]]); }
    while (std::next_permutation(p.begin(),p.end()));
    return result;
}
const std::vector<ParameterSpec>& parameterSpecs() {
    static const auto specs = [] {
        std::vector<ParameterSpec> p;
        auto f = [&](const char* id, const char* name, float lo, float hi, float value, float skew, const char* unit, const char* group) {
            p.push_back({id,name,lo,hi,value,skew,unit,group,{},false});
        };
        auto b = [&](const char* id,const char* name,bool value,const char* group) {
            p.push_back({id,name,0,1,value ? 1.f:0.f,1,"",group,{},true});
        };
        auto c = [&](const char* id,const char* name,juce::StringArray choices,int value,const char* group) {
            const auto hi = static_cast<float>(choices.size()-1);
            p.push_back({id,name,0,hi,static_cast<float>(value),1,"",group,std::move(choices),false});
        };
        f("global.input","Input",-24,24,0,1,"dB","Global");
        f("global.output","Output",-24,12,0,1,"dB","Global");
        f("global.mix","Dry / Wet",0,1,1,1,"%","Global");
        b("global.bypass","Bypass",false,"Global");
        c("global.mode","Mode",{"FX","Resynth","Synth"},0,"Global");
        c("global.order","Signal order",orderNames(),0,"Global");
        b("spectral.enabled","Spectral on",true,"Spectral");
        f("spectral.amount","Tame",0,1,0.35f,1,"%","Spectral");
        f("spectral.threshold","Threshold",-60,0,-24,1,"dB","Spectral");
        f("spectral.focus","Focus",120,16000,3500,0.35f,"Hz","Spectral");
        f("spectral.transient","Preserve",0,1,0.65f,1,"%","Spectral");
        b("modal.enabled","Modal on",true,"Modal");
        f("modal.mix","Resonance",0,1,0,1,"%","Modal");
        f("modal.decay","Decay",0.08f,12,1.2f,0.4f,"s","Modal");
        f("modal.tune","Tune",-24,24,0,1,"st","Modal");
        f("modal.brightness","Brightness",0,1,0.5f,1,"%","Modal");
        c("modal.material","Material",{"String","Plate","Beam","Membrane"},0,"Modal");
        b("colour.enabled","Colour on",true,"Colour");
        f("colour.drive","Drive",0,30,0,1,"dB","Colour");
        f("colour.tone","Tone",0,1,0.5f,1,"%","Colour");
        f("colour.bias","Bias",-0.5f,0.5f,0,1,"","Colour");
        c("colour.model","Character",{"Silk","Ember","Iron"},0,"Colour");
        b("space.enabled","Space on",true,"Space");
        f("space.mix","Space",0,1,0.12f,1,"%","Space");
        f("space.size","Size",0,1,0.5f,1,"%","Space");
        f("space.decay","Time",0.1f,20,2.5f,0.4f,"s","Space");
        f("space.damping","Damping",0,1,0.5f,1,"%","Space");
        f("space.dispersion","Disperse",0,1,0.2f,1,"%","Space");
        b("space.freeze","Freeze",false,"Space");
        b("master.enabled","Master on",true,"Master");
        f("master.width","Width",0,2,1,1,"%","Master");
        f("master.ceiling","Ceiling",-12,0,-0.3f,1,"dB","Master");
        f("master.release","Release",10,500,100,0.5f,"ms","Master");
        c("master.dither","Dither",{"Off","16 bit","24 bit"},0,"Master");
        f("mod.rate","Rate",0.05f,20,0.5f,0.35f,"Hz","Modulation");
        f("mod.depth","Depth",0,1,0,1,"%","Modulation");
        c("mod.target","Destination",{"None","Drive","Resonance","Space","Focus"},0,"Modulation");
        c("mod.shape","Shape",{"Sine","Triangle","Random hold"},0,"Modulation");
        b("mod.sync","Beat sync",false,"Modulation");
        jassert(p.size() == parameterCount);
        return p;
    }();
    return specs;
}
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& p : parameterSpecs()) {
        const juce::ParameterID id {p.id,1};
        if (p.boolean) layout.add(std::make_unique<juce::AudioParameterBool>(id,p.name,p.defaultValue>0.5f));
        else if (!p.choices.isEmpty()) layout.add(std::make_unique<juce::AudioParameterChoice>(id,p.name,p.choices,static_cast<int>(p.defaultValue)));
        else {
            juce::NormalisableRange<float> range {p.minimum,p.maximum,0.0f,p.skew};
            const juce::String unit {p.unit};
            auto attrs = juce::AudioParameterFloatAttributes().withLabel(unit)
                .withStringFromValueFunction([unit](float value,int) {
                    if (unit=="%") return juce::String(value*100.f,0)+" %";
                    if (unit=="Hz" && value>=1000) return juce::String(value/1000.f,2)+" kHz";
                    return juce::String(value,(unit=="Hz"||unit=="ms") ? 0:2)+(unit.isEmpty()?"":" "+unit);
                });
            layout.add(std::make_unique<juce::AudioParameterFloat>(id,p.name,range,p.defaultValue,attrs));
        }
    }
    return layout;
}
}
