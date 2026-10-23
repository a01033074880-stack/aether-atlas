#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

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
    allRaw.reserve(specs.size());
    for(const auto& spec:specs) allRaw.push_back(state.getRawParameterValue(spec.id));
    for(size_t i=0;i<raw.size();++i) raw[i]=allRaw[i];
    state.state.setProperty("schema",2,nullptr);
    state.state.setProperty("version","2.0.0",nullptr);
#if JucePlugin_IsSynth
    state.getParameter("global.mode")->setValueNotifyingHost(1.f);
    presetName="Atlas / Init FM";
#endif
    snapshot=state.copyState();
}
void AetherAudioProcessor::prepareToPlay(double rate,int block) {
    currentRate=(std::isfinite(rate)&&rate>=8000) ? rate:48000;
    engine.prepare(currentRate,juce::jmax(block,scratchSize));
    outputStage.prepare(currentRate,juce::jmax(block,scratchSize));
    graph.prepare(currentRate,juce::jmax(block,scratchSize));
    fmEngine.prepare(currentRate,juce::jmax(block,scratchSize));
    engine.setParameters(parameters());
    graph.setParameters(graphParameters()); fmEngine.setParameters(fmParameters());
    fmInputGain=1; inputGainSmoothing=static_cast<float>(1-std::exp(-1/(currentRate*0.005)));
    excitationAttack=static_cast<float>(1-std::exp(-1/(currentRate*0.004)));
    excitationRelease=static_cast<float>(1-std::exp(-1/(currentRate*0.08)));
    excitationLowpass=static_cast<float>(1-std::exp(-2*juce::MathConstants<double>::pi*1800/currentRate));
    analysisFifo.reset(); prepared=true; wasBypassed=false;
    setLatencySamples(0);
}
void AetherAudioProcessor::releaseResources() { reset(); prepared=false; }
void AetherAudioProcessor::reset() {
    engine.reset(); outputStage.reset(); graph.reset(); fmEngine.reset();
    lastVelocity=lastNoteHz=0; fmVoiceCount.store(0);
    excitationEnvelope=excitationLow=excitationPrevious=excitationPitch=0;excitationCrossing=-1;excitationClock=0;
    for(auto& value:nodeMeters) value.store(0); for(auto& value:modulationMeters) value.store(0);
}
double AetherAudioProcessor::getTailLengthSeconds() const {
    if(!allRaw.empty() && allRaw[aether::graphOffset]->load(std::memory_order_relaxed)>0.5f) {
        const auto config=graphParameters();
        if(config.feedback>0) for(int destination=0;destination<aether::modular::slotCount;++destination)
            for(int source=destination;source<aether::modular::slotCount;++source)
                if(std::abs(config.connection[static_cast<size_t>(destination)][static_cast<size_t>(source)])>0.0001f)
                    return std::numeric_limits<double>::infinity();
        for(int n=0;n<aether::modular::slotCount;++n) {
            const int base=aether::nodeOffset+n*11;
            if(allRaw[static_cast<size_t>(base+1)]->load(std::memory_order_relaxed)<0.5f) continue;
            const int type=juce::jlimit(0,static_cast<int>(aether::modular::ModuleType::Count)-1,static_cast<int>(allRaw[static_cast<size_t>(base)]->load(std::memory_order_relaxed)));
            const auto& info=aether::modular::moduleInfo(static_cast<aether::modular::ModuleType>(type));
            if(info.freezeMacro>=0) {
                float possible=allRaw[static_cast<size_t>(base+5+info.freezeMacro)]->load(std::memory_order_relaxed);
                for(const auto& motion:config.modulation) if(motion.source!=0 && motion.target==n*7+info.freezeMacro+1) possible+=std::abs(motion.depth);
                if(possible>=info.freezeThreshold) return std::numeric_limits<double>::infinity();
            }
        }
        return 30.0;
    }
    if(raw[aether::spaceOn]->load(std::memory_order_relaxed)>0.5f && raw[aether::freeze]->load(std::memory_order_relaxed)>0.5f)
        return std::numeric_limits<double>::infinity();
    return 30.0;
}
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
    if(msg.isNoteOn()) {
        engine.noteOn(msg.getNoteNumber(),msg.getFloatVelocity(),channel); fmEngine.noteOn(msg.getNoteNumber(),msg.getFloatVelocity(),channel);
        lastVelocity=msg.getFloatVelocity(); lastNoteHz=static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(msg.getNoteNumber()));
    }
    else if(msg.isNoteOff()) { engine.noteOff(msg.getNoteNumber(),channel); fmEngine.noteOff(msg.getNoteNumber(),channel); }
    else if(msg.isAllNotesOff()||msg.isAllSoundOff()) { engine.allNotesOff();fmEngine.allNotesOff();lastVelocity=lastNoteHz=0; }
    else if(msg.isPitchWheel()) {
        const float semitones=(static_cast<float>(msg.getPitchWheelValue())-8192.f)/8192.f*2.f;
        engine.pitchBend(semitones,channel); fmEngine.pitchBend(semitones,channel);
    }
    else if(msg.isController() && msg.getControllerNumber()==64) { engine.sustain(msg.getControllerValue()>=64,channel);fmEngine.sustain(msg.getControllerValue()>=64,channel); }
}
void AetherAudioProcessor::analyseExcitation(float sample) noexcept {
    const float magnitude=std::abs(sample);
    excitationEnvelope+=(magnitude-excitationEnvelope)*(magnitude>excitationEnvelope?excitationAttack:excitationRelease);
    excitationLow+=excitationLowpass*(sample-excitationLow);
    if(excitationPrevious<=0 && excitationLow>0 && excitationEnvelope>0.002f) {
        const double crossing=static_cast<double>(excitationClock)-excitationLow/(excitationLow-excitationPrevious);
        if(excitationCrossing>=0) {
            const float hz=static_cast<float>(currentRate/(crossing-excitationCrossing));
            if(hz>=35 && hz<=2000) excitationPitch=excitationPitch<=0?hz:excitationPitch+0.15f*(hz-excitationPitch);
        }
        excitationCrossing=crossing;
    }
    excitationPrevious=excitationLow;
    if(excitationEnvelope<1.e-5f) {excitationPitch=0;excitationCrossing=-1;}
    ++excitationClock;
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
void AetherAudioProcessor::renderSegment(juce::AudioBuffer<float>& buffer,int start,int count,const aether::Parameters& p) {
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
            dryLeft[static_cast<size_t>(i)]=left[static_cast<size_t>(i)]; dryRight[static_cast<size_t>(i)]=right[static_cast<size_t>(i)];
        }
        engine.process(left.data(),right.data(),
#if JucePlugin_IsSynth
            nullptr,
#else
            external.getNumChannels()>0?side.data():nullptr,
#endif
            n);
        if(atlasSourceActive) {
            bool graphRendered=false;
            if(fmActive && p.mode!=0) {
                if(p.mode==1) for(int i=0;i<n;++i) {
#if JucePlugin_IsSynth
                    const float sample=0.5f*(dryLeft[static_cast<size_t>(i)]+dryRight[static_cast<size_t>(i)]);
#else
                    const float sample=external.getNumChannels()>0?side[static_cast<size_t>(i)]:0.5f*(dryLeft[static_cast<size_t>(i)]+dryRight[static_cast<size_t>(i)]);
#endif
                    analyseExcitation(sample);
                    if((excitationClock%16)==1) {
                        fmEngine.setAudioExcitation(excitationPitch,juce::jlimit(0.f,1.f,excitationEnvelope*2.f));
                        if(graphActive) graph.setPerformance(fmEngine.activeVoices()>0?lastVelocity:0.f,excitationPitch>0?excitationPitch:lastNoteHz);
                    }
                    fmEngine.process(left.data()+i,right.data()+i,1);
                    const float target=juce::Decibels::decibelsToGain(p.inputDb);
                    fmInputGain+=inputGainSmoothing*(target-fmInputGain);left[static_cast<size_t>(i)]*=fmInputGain;right[static_cast<size_t>(i)]*=fmInputGain;
                    if(graphActive) {
                        graph.process(left.data()+i,right.data()+i,
#if JucePlugin_IsSynth
                            nullptr,
#else
                            external.getNumChannels()>0?side.data()+i:nullptr,
#endif
                            1);
                        graphRendered=true;
                    }
                } else {
                    fmEngine.setAudioExcitation(0,0);
                    fmEngine.process(left.data(),right.data(),n);
                }
                if(p.mode==2) {
                    const float target=juce::Decibels::decibelsToGain(p.inputDb);
                    for(int i=0;i<n;++i) { fmInputGain+=inputGainSmoothing*(target-fmInputGain);left[static_cast<size_t>(i)]*=fmInputGain;right[static_cast<size_t>(i)]*=fmInputGain; }
                }
            }
            if(graphActive&&!graphRendered) {
              if(p.mode==2) {
                graph.setPerformance(fmEngine.activeVoices()>0?lastVelocity:0.f,lastNoteHz);
                graph.process(left.data(),right.data(),
#if JucePlugin_IsSynth
                nullptr,
#else
                external.getNumChannels()>0?side.data():nullptr,
#endif
                  n);
              } else for(int i=0;i<n;++i) {
#if JucePlugin_IsSynth
                analyseExcitation(0.5f*(dryLeft[static_cast<size_t>(i)]+dryRight[static_cast<size_t>(i)]));
#else
                analyseExcitation(external.getNumChannels()>0?side[static_cast<size_t>(i)]:0.5f*(dryLeft[static_cast<size_t>(i)]+dryRight[static_cast<size_t>(i)]));
#endif
                if((excitationClock%16)==1) graph.setPerformance(fmEngine.activeVoices()>0?lastVelocity:0.f,excitationPitch>0?excitationPitch:lastNoteHz);
                graph.process(left.data()+i,right.data()+i,
#if JucePlugin_IsSynth
                    nullptr,
#else
                    external.getNumChannels()>0?side.data()+i:nullptr,
#endif
                    1);
              }
            }
            for(int i=0;i<n;++i) {
                left[static_cast<size_t>(i)]=(1-p.mix)*dryLeft[static_cast<size_t>(i)]+p.mix*left[static_cast<size_t>(i)];
                right[static_cast<size_t>(i)]=(1-p.mix)*dryRight[static_cast<size_t>(i)]+p.mix*right[static_cast<size_t>(i)];
            }
            outputStage.process(left.data(),right.data(),nullptr,n);
        }
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
    if(panic.exchange(false,std::memory_order_relaxed)) reset();
    if(graphReset.exchange(false,std::memory_order_relaxed)) graph.reset();
    if(raw[aether::bypass]->load(std::memory_order_relaxed)>0.5f) { processBlockBypassed(buffer,midi); return; }
    if(wasBypassed) { reset(); wasBypassed=false; }
    auto p=parameters();
    if(auto* playhead=getPlayHead()) if(auto pos=playhead->getPosition()) if(auto bpm=pos->getBpm()) p.tempo=*bpm;
    graphActive=allRaw[aether::graphOffset]->load(std::memory_order_relaxed)>0.5f;
    fmActive=allRaw[aether::fmOffset]->load(std::memory_order_relaxed)>0.5f;
    atlasSourceActive=graphActive||(fmActive&&p.mode!=0);
    if(atlasSourceActive) {
        auto source=p; for(auto& enabled:source.enabled) enabled=false;
        source.outputDb=0;source.mix=1;engine.setParameters(source);
        auto output=p;for(auto& enabled:output.enabled) enabled=false;output.enabled[4]=p.enabled[4];
        output.inputDb=0;output.mode=0;output.mix=1;outputStage.setParameters(output);
        auto configuration=graphParameters();configuration.tempo=p.tempo;graph.setParameters(configuration);
        fmEngine.setParameters(fmParameters());
    } else engine.setParameters(p);
    int offset=0; const int total=buffer.getNumSamples();
    for(const auto metadata:midi) {
        const int position=juce::jlimit(offset,total,metadata.samplePosition);
        if(position>offset) renderSegment(buffer,offset,position-offset,p);
        // SysEx may allocate in MidiMessage; this instrument consumes only short channel messages.
        if(metadata.numBytes>0 && metadata.numBytes<=3) handleMidi(juce::MidiMessage(metadata.data,metadata.numBytes,0));
        offset=position;
    }
    if(offset<total) renderSegment(buffer,offset,total-offset,p);
    gainReductionDb.store(atlasSourceActive?outputStage.gainReductionDb():engine.gainReductionDb(),std::memory_order_relaxed);
    detectedPitchHz.store(engine.pitchHz(),std::memory_order_relaxed);
    fmVoiceCount.store(fmEngine.activeVoices(),std::memory_order_relaxed);
    if(graphActive) {
        const auto nodes=graph.nodeLevels(); const auto sources=graph.sourceLevels();
        int latency=0;
        for(size_t i=0;i<nodes.size();++i) {
            nodeMeters[i].store(nodes[i],std::memory_order_relaxed);
            const int nodeBase=aether::nodeOffset+static_cast<int>(i)*11;
            if(allRaw[static_cast<size_t>(nodeBase+1)]->load(std::memory_order_relaxed)>0.5f)
                latency=juce::jmax(latency,graph.intrinsicLatencySamples(static_cast<int>(i)));
        }
        for(size_t i=0;i<sources.size();++i) modulationMeters[i].store(sources[i],std::memory_order_relaxed);
        textureLatencySamples.store(latency,std::memory_order_relaxed);
    } else textureLatencySamples.store(0,std::memory_order_relaxed);
    midi.clear();
}
void AetherAudioProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi) {
    if(!wasBypassed) { reset(); wasBypassed=true; }
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
    { const juce::ScopedLock lock(metadataLock);
      tree.setProperty("preset",presetName,nullptr);
      tree.setProperty("snapshotB",snapshotB.load(),nullptr);
      if(snapshot.isValid()) tree.setProperty("alternateSnapshot",snapshot.toXmlString(),nullptr);
    }
    tree.setProperty("program",currentProgram.load(),nullptr);
    tree.setProperty("schema",2,nullptr); tree.setProperty("version","2.0.0",nullptr);
    auto xml=tree.createXml(); copyXmlToBinary(*xml,data);
}
void AetherAudioProcessor::setStateInformation(const void* data,int size) {
    if(data==nullptr || size<=0 || size>1024*1024) return;
    auto xml=getXmlFromBinary(data,size);
    if(!xml || !xml->hasTagName("AetherState")) return;
    auto tree=juce::ValueTree::fromXml(*xml);
    const int schema=static_cast<int>(tree.getProperty("schema",1));
    if(schema<1||schema>2) return;
    // Merge only recognized finite values into the current canonical tree.
    auto canonical=state.copyState();
    auto merge=[&](const juce::ValueTree& sourceTree) {
        auto result=canonical.createCopy();
        for(const auto& spec:aether::parameterSpecs()) {
            const auto source=sourceTree.getChildWithProperty("id",spec.id);
            auto dest=result.getChildWithProperty("id",spec.id);
            if(source.isValid() && dest.isValid()) {
                const auto value=source.getProperty("value",spec.defaultValue).toString().trim().toStdString();
                float number=0;
                const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
                if(parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size() && std::isfinite(number))
                    dest.setProperty("value",juce::jlimit(spec.minimum,spec.maximum,number),nullptr);
            }
        }
        if(schema==1) {
            result.getChildWithProperty("id","graph.enabled").setProperty("value",0,nullptr);
            result.getChildWithProperty("id","fm.enabled").setProperty("value",0,nullptr);
        }
        result.setProperty("schema",2,nullptr);result.setProperty("version","2.0.0",nullptr);
        return result;
    };
    auto clean=merge(tree);
    state.replaceState(clean);
    { const juce::ScopedLock lock(metadataLock);
      presetName=tree.getProperty("preset","Recalled state").toString().substring(0,128);
      const auto alternate=juce::ValueTree::fromXml(tree.getProperty("alternateSnapshot").toString());
      snapshot=alternate.hasType("AetherState") ? merge(alternate) : clean.createCopy();
      snapshotB.store(static_cast<bool>(tree.getProperty("snapshotB",false)));
    }
    currentProgram.store(juce::jlimit(0,getNumPrograms()-1,static_cast<int>(tree.getProperty("program",0))));
    panic.store(true,std::memory_order_relaxed);
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AetherAudioProcessor(); }
