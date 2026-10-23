#include "PluginProcessor.h"
#include <cmath>

float AetherAudioProcessor::parameterValue(const juce::String& id) const {
    if(auto* value=state.getRawParameterValue(id)) return value->load(std::memory_order_relaxed);
    return 0;
}
void AetherAudioProcessor::setParameterValue(const juce::String& id,float value) {
    if(!std::isfinite(value)) return;
    if(auto* parameter=state.getParameter(id)) {
        parameter->beginChangeGesture(); parameter->setValueNotifyingHost(parameter->convertTo0to1(value)); parameter->endChangeGesture();
    }
}
void AetherAudioProcessor::applyRoutingAlgorithm(int index) {
    using namespace aether::modular;
    if(index>=algorithmCount) { setParameterValue("routing.algorithm",static_cast<float>(algorithmCount)); return; }
    Parameters config; applyAlgorithm(index,config);
    for(int n=0;n<slotCount;++n) {
        const auto id="node."+juce::String(n+1)+".";
        setParameterValue(id+"input",config.input[static_cast<size_t>(n)]); setParameterValue(id+"output",config.output[static_cast<size_t>(n)]);
        for(int d=0;d<slotCount;++d) setParameterValue("route."+juce::String(n+1)+"."+juce::String(d+1),config.connection[static_cast<size_t>(d)][static_cast<size_t>(n)]);
    }
    setParameterValue("routing.algorithm",static_cast<float>(juce::jlimit(0,algorithmCount-1,index))); graphReset.store(true);
}
void AetherAudioProcessor::setGraphEdge(int source,int destination,float gain) {
    if(source<0||source>=aether::modular::slotCount||destination<0||destination>=aether::modular::slotCount) return;
    if(parameterValue("routing.algorithm")<aether::modular::algorithmCount) applyRoutingAlgorithm(static_cast<int>(parameterValue("routing.algorithm")));
    setParameterValue("routing.algorithm",static_cast<float>(aether::modular::algorithmCount));
    setParameterValue("route."+juce::String(source+1)+"."+juce::String(destination+1),juce::jlimit(-1.f,1.f,gain));
}
void AetherAudioProcessor::assignModule(int slot,int moduleType) {
    using namespace aether::modular;
    if(slot<0||slot>=slotCount) return;
    moduleType=juce::jlimit(0,static_cast<int>(ModuleType::Count)-1,moduleType);
    Parameters defaults; aether::modular::assignModule(defaults,slot,static_cast<ModuleType>(moduleType));
    const auto id="node."+juce::String(slot+1)+".";
    setParameterValue(id+"type",static_cast<float>(moduleType));
    for(int i=0;i<4;++i) setParameterValue(id+juce::String::charToString(static_cast<juce::juce_wchar>('a'+i)),defaults.slots[static_cast<size_t>(slot)].macro[static_cast<size_t>(i)]);
    setParameterValue(id+"on",1); setParameterValue(id+"mix",moduleType==0?1.f:0.5f); graphReset.store(true);
}
void AetherAudioProcessor::applyFmAlgorithm(int index) {
    using namespace aether::fm;
    index=juce::jlimit(0,algorithmCount-1,index); const auto& config=algorithm(index);
    for(int source=0;source<operatorCount;++source) {
        setParameterValue("fm.op."+juce::String(source+1)+".carrier",1);
        for(int dest=0;dest<operatorCount;++dest) setParameterValue("fm.edge."+juce::String(source+1)+"."+juce::String(dest+1),config.matrix[static_cast<size_t>(dest)][static_cast<size_t>(source)]);
    }
    setParameterValue("fm.algorithm",static_cast<float>(index)); setParameterValue("fm.custom",0);
}
void AetherAudioProcessor::setFmEdge(int source,int destination,float depth) {
    using namespace aether::fm;
    if(source<0||source>=operatorCount||destination<0||destination>=operatorCount) return;
    if(parameterValue("fm.custom")<0.5f) {
        const int algorithmIndex=static_cast<int>(parameterValue("fm.algorithm"));
        const auto& config=algorithm(algorithmIndex);const auto& next=algorithm((algorithmIndex+1)%algorithmCount);
        const float morph=parameterValue("fm.morph");
        for(int s=0;s<operatorCount;++s) {
            const float carrier=config.carriers[static_cast<size_t>(s)]+morph*(next.carriers[static_cast<size_t>(s)]-config.carriers[static_cast<size_t>(s)]);
            setParameterValue("fm.op."+juce::String(s+1)+".carrier",carrier*parameterValue("fm.op."+juce::String(s+1)+".carrier"));
            for(int d=0;d<operatorCount;++d) {
                const float edge=config.matrix[static_cast<size_t>(d)][static_cast<size_t>(s)]+morph*(next.matrix[static_cast<size_t>(d)][static_cast<size_t>(s)]-config.matrix[static_cast<size_t>(d)][static_cast<size_t>(s)]);
                setParameterValue("fm.edge."+juce::String(s+1)+"."+juce::String(d+1),edge);
            }
        }
        setParameterValue("fm.morph",0);
    }
    setParameterValue("fm.custom",1);
    setParameterValue("fm.edge."+juce::String(source+1)+"."+juce::String(destination+1),juce::jlimit(0.f,1.f,depth));
}
aether::modular::Parameters AetherAudioProcessor::graphParameters() const noexcept {
    using namespace aether;
    auto value=[&](int index) { const float v=allRaw[static_cast<size_t>(index)]->load(std::memory_order_relaxed); return std::isfinite(v)?v:parameterSpecs()[static_cast<size_t>(index)].defaultValue; };
    modular::Parameters p;
    p.feedback=value(graphOffset+2); p.trimDb=value(graphOffset+3);
    for(int n=0;n<modular::slotCount;++n) {
        const int base=nodeOffset+n*11;auto& slot=p.slots[static_cast<size_t>(n)];
        slot.type=static_cast<modular::ModuleType>(static_cast<int>(value(base))); slot.enabled=value(base+1)>0.5f;
        slot.mix=value(base+2); slot.levelDb=value(base+3); slot.pan=value(base+4);
        for(int m=0;m<4;++m) slot.macro[static_cast<size_t>(m)]=value(base+5+m);
        p.input[static_cast<size_t>(n)]=value(base+9); p.output[static_cast<size_t>(n)]=value(base+10);
        for(int d=0;d<modular::slotCount;++d) p.connection[static_cast<size_t>(d)][static_cast<size_t>(n)]=value(routeOffset+n*modular::slotCount+d);
    }
    const int algorithmIndex=static_cast<int>(value(graphOffset+1));
    if(algorithmIndex<modular::algorithmCount) modular::applyAlgorithm(algorithmIndex,p);
    for(int n=0;n<4;++n) {
        auto& lfo=p.lfo[static_cast<size_t>(n)];const int base=lfoOffset+n*4;
        lfo.rate=value(base); lfo.phase=value(base+1); lfo.shape=static_cast<int>(value(base+2)); lfo.sync=value(base+3)>0.5f;
    }
    for(int n=0;n<modular::modulationCount;++n) {
        auto& route=p.modulation[static_cast<size_t>(n)];const int base=modulationOffset+n*3;
        route.source=static_cast<int>(value(base)); route.target=static_cast<int>(value(base+1)); route.depth=value(base+2);
    }
    return p;
}
aether::fm::Parameters AetherAudioProcessor::fmParameters() const noexcept {
    using namespace aether;
    auto value=[&](int index) { const float v=allRaw[static_cast<size_t>(index)]->load(std::memory_order_relaxed); return std::isfinite(v)?v:parameterSpecs()[static_cast<size_t>(index)].defaultValue; };
    fm::Parameters p;
    p.enabled=value(fmOffset)>0.5f; p.algorithm=static_cast<int>(value(fmOffset+1)); p.custom=value(fmOffset+2)>0.5f;
    p.index=value(fmOffset+3);p.morph=value(fmOffset+4);p.feedback=value(fmOffset+5);
    p.attack=value(fmOffset+6);p.decay=value(fmOffset+7);p.sustain=value(fmOffset+8);p.release=value(fmOffset+9);
    p.gain=value(fmOffset+10);p.spread=value(fmOffset+11);p.transpose=value(modalTune);
    for(int n=0;n<fm::operatorCount;++n) {
        auto& op=p.operators[static_cast<size_t>(n)];const int base=operatorOffset+n*6;
        op.ratio=value(base);op.level=value(base+1);op.carrier=value(base+2);op.detune=value(base+3);op.attack=value(base+4);op.release=value(base+5);
        for(int d=0;d<fm::operatorCount;++d) p.matrix[static_cast<size_t>(d)][static_cast<size_t>(n)]=value(fmEdgeOffset+n*fm::operatorCount+d);
    }
    return p;
}
