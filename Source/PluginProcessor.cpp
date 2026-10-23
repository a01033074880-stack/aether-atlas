#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace {
float finiteSample(float value) noexcept { return std::isfinite(value) ? juce::jlimit(-32.f,32.f,value) : 0.f; }
float db(float value) noexcept { return juce::Decibels::gainToDecibels(value,-100.f); }
}
AetherAudioProcessor::AetherAudioProcessor()
    : AudioProcessor(BusesProperties()
#if !JucePlugin_IsSynth
        .withInput("Input",juce::AudioChannelSet::stereo(),true)
#endif
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)
#if !JucePlugin_IsSynth
        .withInput("Sidechain",juce::AudioChannelSet::stereo(),false)
#endif
        ), state(*this,nullptr,"AetherState",aether::createParameterLayout()) {
    const auto& specs=aether::parameterSpecs();
    for(size_t i=0;i<raw.size();++i) raw[i]=state.getRawParameterValue(specs[i].id);
    state.state.setProperty("schema",1,nullptr);
    state.state.setProperty("version","1.0.0",nullptr);
#if JucePlugin_IsSynth
    state.getParameter("global.mode")->setValueNotifyingHost(1.f);
    presetName="Init Synth";
#endif
    snapshot=state.copyState();
}
void AetherAudioProcessor::prepareToPlay(double rate,int block) {
    currentRate=(std::isfinite(rate)&&rate>=8000) ? rate:48000;
    engine.prepare(currentRate,juce::jmax(block,scratchSize));
    engine.setParameters(parameters());
    analysisFifo.reset(); prepared=true; wasBypassed=false;
    setLatencySamples(0);
}
void AetherAudioProcessor::releaseResources() { engine.reset(); prepared=false; }
void AetherAudioProcessor::reset() { engine.reset(); }
bool AetherAudioProcessor::isBusesLayoutSupported(const BusesLayout& layout) const {
    const auto out=layout.getMainOutputChannelSet();
    if(out!=juce::AudioChannelSet::mono() && out!=juce::AudioChannelSet::stereo()) return false;
#if !JucePlugin_IsSynth
    if(layout.getMainInputChannelSet()!=out) return false;
    if(layout.inputBuses.size()>1) {
        const auto sideSet=layout.inputBuses[1];
        if(!sideSet.isDisabled() && sideSet!=juce::AudioChannelSet::mono() && sideSet!=juce::AudioChannelSet::stereo()) return false;
    }
#endif
    return true;
}
aether::Parameters AetherAudioProcessor::parameters() const noexcept {
    using namespace aether;
    auto v=[&](ParamIndex index) { const float value=raw[index]->load(std::memory_order_relaxed); return std::isfinite(value) ? value:parameterSpecs()[index].defaultValue; };
    Parameters p;
    p.inputDb=v(input); p.outputDb=v(output); p.mix=v(mix); p.mode=static_cast<int>(v(mode)); p.order=static_cast<int>(v(order));
    p.enabled[0]=v(spectralOn)>0.5f; p.enabled[1]=v(modalOn)>0.5f; p.enabled[2]=v(colourOn)>0.5f; p.enabled[3]=v(spaceOn)>0.5f; p.enabled[4]=v(masterOn)>0.5f;
    p.spectralAmount=v(spectralAmount); p.spectralThreshold=v(spectralThreshold); p.spectralFocus=v(spectralFocus); p.transientProtect=v(transientProtect);
    p.modalMix=v(modalMix); p.modalDecay=v(modalDecay); p.modalTune=v(modalTune); p.modalBrightness=v(modalBrightness); p.modalMaterial=static_cast<int>(v(modalMaterial));
    p.driveDb=v(driveDb); p.driveTone=v(driveTone); p.driveBias=v(driveBias); p.driveModel=static_cast<int>(v(driveModel));
    p.spaceMix=v(spaceMix); p.spaceSize=v(spaceSize); p.spaceDecay=v(spaceDecay); p.spaceDamping=v(spaceDamping); p.dispersion=v(dispersion); p.freeze=v(freeze)>0.5f;
    p.masterWidth=v(masterWidth); p.masterCeiling=v(masterCeiling); p.masterRelease=v(masterRelease); p.dither=static_cast<int>(v(dither));
    p.modRate=v(modRate); p.modDepth=v(modDepth); p.modTarget=static_cast<int>(v(modTarget)); p.modShape=static_cast<int>(v(modShape)); p.modSync=v(modSync)>0.5f;
    return p;
}
void AetherAudioProcessor::handleMidi(const juce::MidiMessage& msg) noexcept {
    const int channel=msg.getChannel();
    if(msg.isNoteOn()) engine.noteOn(msg.getNoteNumber(),msg.getFloatVelocity(),channel);
    else if(msg.isNoteOff()) engine.noteOff(msg.getNoteNumber(),channel);
    else if(msg.isAllNotesOff()||msg.isAllSoundOff()) engine.allNotesOff();
    else if(msg.isPitchWheel()) engine.pitchBend((static_cast<float>(msg.getPitchWheelValue())-8192.f)/8192.f*2.f,channel);
    else if(msg.isController() && msg.getControllerNumber()==64) engine.sustain(msg.getControllerValue()>=64,channel);
}
void AetherAudioProcessor::publishAnalysis(const float* l,const float* r,int count) noexcept {
    if(!analysisEnabled.load(std::memory_order_relaxed)) return;
    int s1,n1,s2,n2; analysisFifo.prepareToWrite(count,s1,n1,s2,n2);
    for(int i=0;i<n1;++i) analysis[static_cast<size_t>(s1+i)]=0.5f*(l[i]+r[i]);
    for(int i=0;i<n2;++i) analysis[static_cast<size_t>(s2+i)]=0.5f*(l[n1+i]+r[n1+i]);
    analysisFifo.finishedWrite(n1+n2);
}
int AetherAudioProcessor::readAnalysis(float* dest,int maximum) noexcept {
    int s1,n1,s2,n2; analysisFifo.prepareToRead(maximum,s1,n1,s2,n2);
    std::copy_n(analysis.data()+s1,n1,dest); std::copy_n(analysis.data()+s2,n2,dest+n1);
    analysisFifo.finishedRead(n1+n2); return n1+n2;
}
void AetherAudioProcessor::renderSegment(juce::AudioBuffer<float>& buffer,int start,int count,const aether::Parameters&) {
    auto main=getBusBuffer(buffer,false,0);
#if !JucePlugin_IsSynth
    auto in=getBusBuffer(buffer,true,0);
    auto external=getBusBuffer(buffer,true,1);
    const int inChannels=in.getNumChannels();
#endif
    float peakIn=0,peakOut=0;
    for(int offset=start;offset<start+count;) {
        const int n=juce::jmin(scratchSize,start+count-offset);
        for(int i=0;i<n;++i) {
#if JucePlugin_IsSynth
            left[static_cast<size_t>(i)]=right[static_cast<size_t>(i)]=0;
#else
            left[static_cast<size_t>(i)]=finiteSample(in.getSample(0,offset+i));
            right[static_cast<size_t>(i)]=finiteSample(in.getSample(inChannels>1?1:0,offset+i));
            if(external.getNumChannels()>0) {
                float x=external.getSample(0,offset+i);
                if(external.getNumChannels()>1) x=0.5f*(x+external.getSample(1,offset+i));
                side[static_cast<size_t>(i)]=finiteSample(x);
            }
#endif
            peakIn=juce::jmax(peakIn,std::abs(left[static_cast<size_t>(i)]),std::abs(right[static_cast<size_t>(i)]));
        }
        engine.process(left.data(),right.data(),
#if JucePlugin_IsSynth
            nullptr,
#else
            external.getNumChannels()>0?side.data():nullptr,
#endif
            n);
        for(int i=0;i<n;++i) {
            const float l=finiteSample(left[static_cast<size_t>(i)]),r=finiteSample(right[static_cast<size_t>(i)]);
            if(main.getNumChannels()>1) { main.setSample(0,offset+i,l); main.setSample(1,offset+i,r); }
            else main.setSample(0,offset+i,0.5f*(l+r));
            peakOut=juce::jmax(peakOut,std::abs(l),std::abs(r));
        }
        publishAnalysis(left.data(),right.data(),n); offset+=n;
    }
    const float decay=static_cast<float>(count/currentRate*24.0);
    inputMeterDb.store(juce::jmax(db(peakIn),inputMeterDb.load()-decay),std::memory_order_relaxed);
    outputMeterDb.store(juce::jmax(db(peakOut),outputMeterDb.load()-decay),std::memory_order_relaxed);
}
void AetherAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    if(!prepared) { buffer.clear(); midi.clear(); return; }
    if(panic.exchange(false,std::memory_order_relaxed)) engine.reset();
    if(raw[aether::bypass]->load(std::memory_order_relaxed)>0.5f) { processBlockBypassed(buffer,midi); return; }
    if(wasBypassed) { engine.reset(); wasBypassed=false; }
    auto p=parameters();
    if(auto* playhead=getPlayHead()) if(auto pos=playhead->getPosition()) if(auto bpm=pos->getBpm()) p.tempo=*bpm;
    engine.setParameters(p);
    int offset=0; const int total=buffer.getNumSamples();
    for(const auto metadata:midi) {
        const int position=juce::jlimit(offset,total,metadata.samplePosition);
        if(position>offset) renderSegment(buffer,offset,position-offset,p);
        // SysEx may allocate in MidiMessage; this instrument consumes only short channel messages.
        if(metadata.numBytes>0 && metadata.numBytes<=3) handleMidi(juce::MidiMessage(metadata.data,metadata.numBytes,0));
        offset=position;
    }
    if(offset<total) renderSegment(buffer,offset,total-offset,p);
    gainReductionDb.store(engine.gainReductionDb(),std::memory_order_relaxed);
    detectedPitchHz.store(engine.pitchHz(),std::memory_order_relaxed);
    midi.clear();
}
void AetherAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi) {
    if(!wasBypassed) { engine.reset(); wasBypassed=true; }
#if JucePlugin_IsSynth
    buffer.clear();
#endif
    midi.clear();
}
juce::AudioProcessorParameter* AetherAudioProcessor::getBypassParameter() const { return state.getParameter("global.bypass"); }
juce::AudioProcessorEditor* AetherAudioProcessor::createEditor() { return new AetherAudioProcessorEditor(*this); }
int AetherAudioProcessor::getNumPrograms() { return factoryPresetNames().size(); }
void AetherAudioProcessor::setCurrentProgram(int index) { loadFactoryPreset(index); }
const juce::String AetherAudioProcessor::getProgramName(int index) { return factoryPresetNames()[juce::jlimit(0,getNumPrograms()-1,index)]; }
void AetherAudioProcessor::getStateInformation(juce::MemoryBlock& data) {
    auto tree=state.copyState();
    { const juce::ScopedLock lock(metadataLock); tree.setProperty("preset",presetName,nullptr); }
    tree.setProperty("schema",1,nullptr); tree.setProperty("version","1.0.0",nullptr);
    auto xml=tree.createXml(); copyXmlToBinary(*xml,data);
}
void AetherAudioProcessor::setStateInformation(const void* data,int size) {
    if(data==nullptr || size<=0 || size>1024*1024) return;
    auto xml=getXmlFromBinary(data,size);
    if(!xml || !xml->hasTagName("AetherState")) return;
    auto tree=juce::ValueTree::fromXml(*xml);
    if(static_cast<int>(tree.getProperty("schema",1))!=1) return;
    // Merge only recognized finite values into the current canonical tree.
    auto clean=state.copyState();
    for(const auto& spec:aether::parameterSpecs()) {
        auto source=tree.getChildWithProperty("id",spec.id);
        auto dest=clean.getChildWithProperty("id",spec.id);
        if(source.isValid() && dest.isValid()) {
            const float value=static_cast<float>(source.getProperty("value",spec.defaultValue));
            if(std::isfinite(value)) dest.setProperty("value",juce::jlimit(spec.minimum,spec.maximum,value),nullptr);
        }
    }
    state.replaceState(clean);
    { const juce::ScopedLock lock(metadataLock); presetName=tree.getProperty("preset","Recalled state").toString().substring(0,128); }
    panic.store(true,std::memory_order_relaxed);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AetherAudioProcessor(); }
