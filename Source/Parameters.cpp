#include "Parameters.h"
#include <algorithm>
#include <array>

namespace aether {
juce::StringArray routingNames() {
    juce::StringArray names;
    for(int i=0;i<modular::algorithmCount;++i) names.add(modular::algorithmName(i));
    names.add("Custom constellation"); return names;
}
juce::StringArray moduleNames() {
    juce::StringArray names;
    for(int i=0;i<static_cast<int>(modular::ModuleType::Count);++i) names.add(modular::moduleInfo(static_cast<modular::ModuleType>(i)).name);
    return names;
}
juce::StringArray modulationTargetNames() {
    juce::StringArray names{"None"};
    for(int n=1;n<=modular::slotCount;++n) for(const auto* target:{"Macro A","Macro B","Macro C","Macro D","Mix","Level","Pan"})
        names.add("Node "+juce::String(n)+" / "+target);
    return names;
}
juce::StringArray sourceEngineNames() { return {"Off","Phase Geometry","Chaos"}; }
juce::StringArray czShapeNames() { return {"Saw","Square","Pulse","Resonant saw","Resonant triangle","Resonant trapezoid"}; }
juce::StringArray chaosDistributionNames() { return {"Cauchy","Logistic","Hyperbolic cosine","Arcsine","Exponential","Uniform"}; }
modular::Parameters defaultGraph() {
    modular::Parameters graph;
    const modular::ModuleType types[]={modular::ModuleType::Spectral,modular::ModuleType::WdfDiode,modular::ModuleType::Resonator,
        modular::ModuleType::Tape,modular::ModuleType::PrismPitch,modular::ModuleType::GrainCloud,modular::ModuleType::FrequencyShift,
        modular::ModuleType::Chorus,modular::ModuleType::Diffuser,modular::ModuleType::Space,modular::ModuleType::LoFi,modular::ModuleType::Thru};
    const float wet[]={0.35f,0.22f,0.18f,0.12f,0.f,0.f,0.f,0.12f,0.14f,0.12f,0.f,1.f};
    for(int i=0;i<modular::slotCount;++i) {
        modular::assignModule(graph,i,types[i]); graph.slots[static_cast<size_t>(i)].mix=wet[i];
        graph.slots[static_cast<size_t>(i)].enabled=wet[i]>0;
    }
    modular::applyAlgorithm(0,graph); return graph;
}
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
        auto f = [&](juce::String id, juce::String name, float lo, float hi, float value, float skew, juce::String unit, juce::String group) {
            p.push_back({id,name,lo,hi,value,skew,unit,group,{},false});
        };
        auto b = [&](juce::String id,juce::String name,bool value,juce::String group) {
            p.push_back({id,name,0,1,value ? 1.f:0.f,1,"",group,{},true});
        };
        auto c = [&](juce::String id,juce::String name,juce::StringArray choices,int value,juce::String group) {
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
        b("graph.enabled","Atlas graph",true,"Atlas");
        c("routing.algorithm","Routing algorithm",routingNames(),0,"Atlas");
        f("graph.feedback","Feedback return",0,0.95f,0.65f,1,"%","Atlas");
        f("graph.trim","Graph trim",-24,12,0,1,"dB","Atlas");
        const auto defaults=defaultGraph();
        const auto modules=moduleNames();
        for(int n=0;n<modular::slotCount;++n) {
            const auto id="node."+juce::String(n+1)+".";
            const auto group="Node "+juce::String(n+1); const auto& slot=defaults.slots[static_cast<size_t>(n)];
            c(id+"type","Processor",modules,static_cast<int>(slot.type),group);
            b(id+"on","Enabled",slot.enabled,group);
            f(id+"mix","Mix",0,1,slot.mix,1,"%",group);
            f(id+"level","Level",-24,12,0,1,"dB",group);
            f(id+"pan","Pan",-1,1,0,1,"",group);
            for(int m=0;m<4;++m) f(id+juce::String::charToString(static_cast<juce::juce_wchar>('a'+m)),
                "Macro "+juce::String::charToString(static_cast<juce::juce_wchar>('A'+m)),0,1,slot.macro[static_cast<size_t>(m)],1,"",group);
            f(id+"input","Input send",-1,1,defaults.input[static_cast<size_t>(n)],1,"",group);
            f(id+"output","Output send",-1,1,defaults.output[static_cast<size_t>(n)],1,"",group);
        }
        for(int source=0;source<modular::slotCount;++source) for(int destination=0;destination<modular::slotCount;++destination)
            f("route."+juce::String(source+1)+"."+juce::String(destination+1),"N"+juce::String(source+1)+" to N"+juce::String(destination+1),
                -1,1,defaults.connection[static_cast<size_t>(destination)][static_cast<size_t>(source)],1,"","Cables");
        for(int n=1;n<=4;++n) {
            const auto id="lfo."+juce::String(n)+"."; const auto group="LFO "+juce::String(n);
            f(id+"rate","Rate",0.02f,30,0.25f*static_cast<float>(n),0.35f,"Hz",group);
            f(id+"phase","Phase",0,1,0,1,"%",group);
            c(id+"shape","Shape",{"Sine","Triangle","Saw","Pulse","Random"},0,group);
            b(id+"sync","Beat sync",false,group);
        }
        juce::StringArray sources; for(int i=0;i<modular::sourceCount;++i) sources.add(modular::sourceName(i));
        const auto targets=modulationTargetNames();
        for(int n=1;n<=modular::modulationCount;++n) {
            const auto id="modulation."+juce::String(n)+".";const auto group="Motion "+juce::String(n);
            c(id+"source","Source",sources,0,group); c(id+"target","Destination",targets,0,group);
            f(id+"depth","Depth",-1,1,0,1,"%",group);
        }
        b("fm.enabled","Six operator engine",true,"FM");
        juce::StringArray fmNames; for(int i=0;i<fm::algorithmCount;++i) fmNames.add(fm::algorithm(i).name);
        c("fm.algorithm","FM algorithm",fmNames,0,"FM");
        b("fm.custom","Custom FM matrix",false,"FM");
        f("fm.index","Modulation index",0,12,2,1,"","FM");
        f("fm.morph","Algorithm morph",0,1,0,1,"%","FM");
        f("fm.feedback","Feedback",0,0.95f,0.1f,1,"%","FM");
        f("fm.attack","Attack",0.001f,5,0.008f,0.3f,"s","FM");
        f("fm.decay","Decay",0.01f,5,0.4f,0.4f,"s","FM");
        f("fm.sustain","Sustain",0,1,0.7f,1,"%","FM");
        f("fm.release","Release",0.01f,12,0.5f,0.35f,"s","FM");
        f("fm.gain","Voice level",0,1,0.65f,1,"%","FM");
        f("fm.spread","Stereo spread",0,1,0.25f,1,"%","FM");
        for(int n=1;n<=fm::operatorCount;++n) {
            const auto id="fm.op."+juce::String(n)+".";const auto group="Operator "+juce::String(n);
            f(id+"ratio","Frequency ratio",0.125f,16,1,0.5f,"x",group);
            f(id+"level","Level",0,1,0.7f,1,"%",group);
            f(id+"carrier","To output",0,1,1,1,"%",group);
            f(id+"detune","Fine tune",-100,100,0,1,"ct",group);
            f(id+"attack","Attack scale",0.1f,4,1,0.5f,"x",group);
            f(id+"release","Release scale",0.1f,4,1,0.5f,"x",group);
        }
        for(int source=0;source<fm::operatorCount;++source) for(int destination=0;destination<fm::operatorCount;++destination)
            f("fm.edge."+juce::String(source+1)+"."+juce::String(destination+1),"Op"+juce::String(source+1)+" to Op"+juce::String(destination+1),
                0,1,fm::algorithm(0).matrix[static_cast<size_t>(destination)][static_cast<size_t>(source)],1,"%","FM matrix");
        jassert(p.size() == legacyParameterCount);
        // ---- source layer: ParameterID version 2, appended after every legacy parameter ----
        auto sf = [&](juce::String id, juce::String name, float lo, float hi, float value, float skew, juce::String unit, juce::String group,
                      float step = 0, bool automatable = true) {
            ParameterSpec s{id,name,lo,hi,value,skew,unit,group,{},false}; s.version=2; s.step=step; s.automatable=automatable; p.push_back(s);
        };
        auto sb = [&](juce::String id, juce::String name, bool value, juce::String group, bool automatable = true) {
            ParameterSpec s{id,name,0,1,value ? 1.f:0.f,1,"",group,{},true}; s.version=2; s.automatable=automatable; p.push_back(s);
        };
        auto sc = [&](juce::String id, juce::String name, juce::StringArray choices, int value, juce::String group) {
            const auto hi = static_cast<float>(choices.size()-1);
            ParameterSpec s{id,name,0,hi,static_cast<float>(value),1,"",group,std::move(choices),false}; s.version=2; p.push_back(s);
        };
        sc("source.engine","Engine",sourceEngineNames(),0,"Source");
        sb("source.enabled","On",true,"Source");
        sb("source.antialias","Anti-alias 4x",true,"Source",false);
        sf("source.seed","Seed",0,65535,0,1,"","Source",1,false);
        sf("source.attack","Attack",0.001f,5,0.005f,0.3f,"s","Source");
        sf("source.decay","Decay",0.01f,5,0.3f,0.4f,"s","Source");
        sf("source.sustain","Sustain",0,1,0.7f,1,"%","Source");
        sf("source.release","Release",0.01f,12,0.3f,0.35f,"s","Source");
        sf("source.gain","Level",0,1,0.65f,1,"%","Source");
        sf("source.spread","Stereo spread",0,1,0.25f,1,"%","Source");
        sf("source.transpose","Transpose",-24,24,0,1,"st","Source");
        sf("source.pg.sequency","Walsh sequency",0,63,0,1,"","Phase Geometry");
        sf("source.pg.walshmix","Walsh mix",0,1,0,1,"%","Phase Geometry");
        sc("source.pg.czshape","CZ shape",czShapeNames(),0,"Phase Geometry");
        sf("source.pg.czdepth","CZ depth",0,1,0.5f,1,"%","Phase Geometry");
        sf("source.pg.czenvelope","CZ envelope",0,1,0,1,"%","Phase Geometry");
        sf("source.pg.czvelocity","CZ velocity",0,1,0,1,"%","Phase Geometry");
        sf("source.pg.vpsd","VPS breakpoint D",0.001f,0.999f,0.5f,1,"%","Phase Geometry");
        sf("source.pg.vpsv","VPS breakpoint V",0,1,0.5f,1,"%","Phase Geometry");
        sf("source.pg.vpsratio","VPS ratio",1,16,1,0.5f,"x","Phase Geometry");
        sf("source.pg.phaserandom","Phase random",0,1,0,1,"%","Phase Geometry");
        sf("source.chaos.segments","Segments",2,32,12,1,"","Chaos",1);
        sc("source.chaos.distribution","Distribution",chaosDistributionNames(),0,"Chaos");
        sf("source.chaos.ampstep","Amplitude step",0,1,0.15f,1,"%","Chaos");
        sf("source.chaos.durstep","Duration step",0,1,0.1f,1,"%","Chaos");
        sf("source.chaos.durrange","Duration range",0,1,0.5f,1,"%","Chaos");
        sf("source.chaos.momentum","Momentum",0,0.95f,0,1,"%","Chaos");
        sf("source.chaos.ampbarrier","Amplitude barrier",0.05f,1,1,1,"%","Chaos");
        jassert(p.size() == totalParameterCount);
        return p;
    }();
    return specs;
}
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto& p : parameterSpecs()) {
        const juce::ParameterID id {p.id,p.version};
        if (p.boolean) layout.add(std::make_unique<juce::AudioParameterBool>(id,p.name,p.defaultValue>0.5f,juce::AudioParameterBoolAttributes().withAutomatable(p.automatable)));
        else if (!p.choices.isEmpty()) layout.add(std::make_unique<juce::AudioParameterChoice>(id,p.name,p.choices,static_cast<int>(p.defaultValue),juce::AudioParameterChoiceAttributes().withAutomatable(p.automatable)));
        else {
            juce::NormalisableRange<float> range {p.minimum,p.maximum,p.step,p.skew};
            const juce::String unit {p.unit};
            auto attrs = juce::AudioParameterFloatAttributes().withLabel(unit).withAutomatable(p.automatable)
                .withStringFromValueFunction([unit](float value,int) {
                    if (unit=="%") return juce::String(value*100.f,0)+" %";
                    if (unit=="Hz" && value>=1000) return juce::String(value/1000.f,2)+" kHz";
                    return juce::String(value,(unit=="Hz"||unit=="ms") ? 0:2)+(unit.isEmpty()?"":" "+unit);
                })
                .withValueFromStringFunction([unit](const juce::String& text) {
                    float value=text.getFloatValue();
                    if(unit=="%") value*=0.01f;
                    else if(unit=="Hz" && text.containsIgnoreCase("k")) value*=1000.f;
                    return value;
                });
            layout.add(std::make_unique<juce::AudioParameterFloat>(id,p.name,range,p.defaultValue,attrs));
        }
    }
    return layout;
}
}
