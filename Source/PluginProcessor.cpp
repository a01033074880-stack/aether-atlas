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
    for(size_t k=0;k<extendedTargets.size();++k) {
        const int index=aether::modulationExtendedParameter[k];
        extendedTargets[k]={state.getParameter(specs[static_cast<size_t>(index)].id),index};
    }
    state.state.setProperty("schema",aether::stateSchema,nullptr);
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
    phaseGeometry.prepare(currentRate,juce::jmax(block,scratchSize));
    chaosSource.prepare(currentRate,juce::jmax(block,scratchSize));
    activeSourceEngine=0;
    engine.setParameters(parameters());
    graph.setParameters(graphParameters()); fmEngine.setParameters(fmParameters());
    fmInputGain=1; sourceInputGain=1; inputGainSmoothing=static_cast<float>(1-std::exp(-1/(currentRate*0.005)));
    excitationAttack=static_cast<float>(1-std::exp(-1/(currentRate*0.004)));
    excitationRelease=static_cast<float>(1-std::exp(-1/(currentRate*0.08)));
    excitationLowpass=static_cast<float>(1-std::exp(-2*juce::MathConstants<double>::pi*1800/currentRate));
    analysisFifo.reset(); prepared=true; wasBypassed=false;
    setLatencySamples(0);
}
void AetherAudioProcessor::releaseResources() { reset(); prepared=false; }
void AetherAudioProcessor::reset() {
    engine.reset(); outputStage.reset(); graph.reset(); fmEngine.reset(); phaseGeometry.reset(); chaosSource.reset();
    lastVelocity=lastNoteHz=0; fmVoiceCount.store(0);
    excitationEnvelope=excitationLow=excitationPrevious=excitationPitch=0;excitationCrossing=-1;excitationClock=0;
    for(auto& value:nodeMeters) value.store(0); for(auto& value:modulationMeters) value.store(0);
}
double AetherAudioProcessor::getTailLengthSeconds() const {
    double finiteTail=30.0;
    if(!allRaw.empty() && allRaw[aether::fmOffset]->load(std::memory_order_relaxed)>0.5f && effectiveMode(raw[aether::mode]->load(std::memory_order_relaxed))>0) {
        float releaseScale=0;
        for(int op=0;op<aether::fm::operatorCount;++op)
            releaseScale=juce::jmax(releaseScale,allRaw[static_cast<size_t>(aether::operatorOffset+op*6+5)]->load(std::memory_order_relaxed));
        finiteTail+=static_cast<double>(allRaw[aether::fmOffset+9]->load(std::memory_order_relaxed))*releaseScale;
    }
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
        return finiteTail;
    }
    if(raw[aether::spaceOn]->load(std::memory_order_relaxed)>0.5f && raw[aether::freeze]->load(std::memory_order_relaxed)>0.5f)
        return std::numeric_limits<double>::infinity();
    return finiteTail;
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
int AetherAudioProcessor::effectiveMode(float storedMode) const noexcept {
    const int stored=std::isfinite(storedMode) ? static_cast<int>(storedMode) : 0;
    switch(modeClamp.load(std::memory_order_relaxed)) {
        case ModeClamp::fxPlugin: return aether::clampModeForPluginType(stored,false);
        case ModeClamp::synthPlugin: return aether::clampModeForPluginType(stored,true);
        case ModeClamp::none: break;
    }
    return stored;
}
aether::Parameters AetherAudioProcessor::parameters() const noexcept {
    using namespace aether;
    auto v=[&](ParamIndex index) { const float value=raw[index]->load(std::memory_order_relaxed); return std::isfinite(value) ? value:parameterSpecs()[index].defaultValue; };
    Parameters p;
    p.inputDb=v(input); p.outputDb=v(output); p.mix=v(mix); p.mode=effectiveMode(v(mode)); p.order=static_cast<int>(v(order));
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
        if(auto* src=activeSource()) src->noteOn(msg.getNoteNumber(),msg.getFloatVelocity(),channel);
        lastVelocity=msg.getFloatVelocity(); lastNoteHz=static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(msg.getNoteNumber()));
    }
    else if(msg.isNoteOff()) { engine.noteOff(msg.getNoteNumber(),channel); fmEngine.noteOff(msg.getNoteNumber(),channel); if(auto* src=activeSource()) src->noteOff(msg.getNoteNumber(),channel); }
    else if(msg.isAllNotesOff()||msg.isAllSoundOff()) { engine.allNotesOff();fmEngine.allNotesOff();if(auto* src=activeSource()) src->allNotesOff();lastVelocity=lastNoteHz=0; }
    else if(msg.isPitchWheel()) {
        const float semitones=(static_cast<float>(msg.getPitchWheelValue())-8192.f)/8192.f*2.f;
        engine.pitchBend(semitones,channel); fmEngine.pitchBend(semitones,channel); if(auto* src=activeSource()) src->pitchBend(semitones,channel);
    }
    else if(msg.isController() && msg.getControllerNumber()==64) { engine.sustain(msg.getControllerValue()>=64,channel);fmEngine.sustain(msg.getControllerValue()>=64,channel); if(auto* src=activeSource()) src->sustain(msg.getControllerValue()>=64,channel); }
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
// Builds the modulated FM / Source parameter sets for the offsets the graph holds now (offset = summed source*depth per
// destination, in units of the parameter's full normalised range) and hands them to the engines. Offset 0 keeps the host
// value bit for bit. No allocation: fixed arrays and struct copies only.
void AetherAudioProcessor::applyExtendedModulation(const std::array<float,aether::modulationExtendedCount>& amount,bool force) noexcept {
    if(!force && amount==extendedApplied) return;
    extendedApplied=amount;
    auto fm=fmBase; auto pg=pgBase; auto ch=chaosBase;
    for(size_t k=0;k<amount.size();++k) {
        const float a=amount[k];
        if(!(a!=0.f) || !std::isfinite(a)) continue;
        const auto* parameter=extendedTargets[k].parameter;
        const float m=parameter->convertFrom0to1(juce::jlimit(0.f,1.f,parameter->convertTo0to1(extendedBase[k])+a));
        if(!std::isfinite(m)) continue;
        switch(k) {
            case 0: pg.gain=m; ch.gain=m; break;
            case 1: pg.spread=m; ch.spread=m; break;
            case 2: pg.transpose=m; ch.transpose=m; break;
            case 3: pg.walshSequency=m; break;
            case 4: pg.walshMix=m; break;
            case 5: pg.czDepth=m; break;
            case 6: pg.czEnvelope=m; break;
            case 7: pg.czVelocity=m; break;
            case 8: pg.vpsD=m; break;
            case 9: pg.vpsV=m; break;
            case 10: pg.vpsRatio=m; break;
            case 11: pg.phaseRandom=m; break;
            case 12: ch.ampStep=m; break;
            case 13: ch.durStep=m; break;
            case 14: ch.durRange=m; break;
            case 15: ch.momentum=m; break;
            case 16: ch.ampBarrier=m; break;
            case 17: fm.index=m; break;
            case 18: fm.morph=m; break;
            case 19: fm.feedback=m; break;
            case 20: fm.gain=m; break;
            case 21: fm.spread=m; break;
            default: fm.operators[k-22].level=m; break;
        }
    }
    fmEngine.setParameters(fm);
    if(wantedSourceEngine==1) phaseGeometry.setParameters(pg);
    else if(wantedSourceEngine==2) chaosSource.setParameters(ch);
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
            // Resynth + Source: the excitation analysis of this block runs once up front (it depends only on the input
            // samples), the FM loop and the graph loop below replay the stored per-sample state, and the same pitch and
            // envelope drive the Source voice. Without a Source nothing here runs, so plain Resynth is untouched.
            auto excitationInput=[&](int i) noexcept {
#if JucePlugin_IsSynth
                return 0.5f*(dryLeft[static_cast<size_t>(i)]+dryRight[static_cast<size_t>(i)]);
#else
                return external.getNumChannels()>0?side[static_cast<size_t>(i)]:0.5f*(dryLeft[static_cast<size_t>(i)]+dryRight[static_cast<size_t>(i)]);
#endif
            };
            const bool sourceDriven=p.mode==1 && activeSource()!=nullptr;
            if(sourceDriven) for(int i=0;i<n;++i) {
                analyseExcitation(excitationInput(i));
                const auto k=static_cast<size_t>(i);
                excitationPitchAt[k]=excitationPitch; excitationEnvelopeAt[k]=excitationEnvelope; excitationClockAt[k]=excitationClock;
                driveHz[k]=excitationPitch; driveLevel[k]=juce::jlimit(0.f,1.f,excitationEnvelope*2.f);
            }
            auto excite=[&](int i) noexcept {
                if(sourceDriven) {
                    const auto k=static_cast<size_t>(i);
                    excitationPitch=excitationPitchAt[k]; excitationEnvelope=excitationEnvelopeAt[k]; excitationClock=excitationClockAt[k];
                } else analyseExcitation(excitationInput(i));
            };
            auto renderRange=[&](const int b,const int e) {
                const int cnt=e-b; bool graphRendered=false;
            if(fmActive && p.mode!=0) {
                if(p.mode==1) for(int i=b;i<e;++i) {
                    excite(i);
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
                    fmEngine.process(left.data()+b,right.data()+b,cnt);
                }
                if(p.mode==2) {
                    const float target=juce::Decibels::decibelsToGain(p.inputDb);
                    for(int i=b;i<e;++i) { fmInputGain+=inputGainSmoothing*(target-fmInputGain);left[static_cast<size_t>(i)]*=fmInputGain;right[static_cast<size_t>(i)]*=fmInputGain; }
                }
            }
            if(auto* src=activeSource()) {
                // Source layer (Synth: MIDI voices; Resynth: additionally one voice driven by the input's pitch and envelope).
                // It replaces the buffer when FM is not rendering, otherwise it is added to FM.
                if(sourceDriven) src->processDriven(sourceLeft.data()+b,sourceRight.data()+b,cnt,driveHz.data()+b,driveLevel.data()+b);
                else src->process(sourceLeft.data()+b,sourceRight.data()+b,cnt);
                // Same Input-gain semantics as the FM path: target = Input (dB), 5 ms one-pole smoothing, applied after the
                // voices render. The source keeps its own smoothing state so FM and source ramp independently.
                const float inputTarget=juce::Decibels::decibelsToGain(p.inputDb);
                for(size_t i=static_cast<size_t>(b);i<static_cast<size_t>(e);++i) { sourceInputGain+=inputGainSmoothing*(inputTarget-sourceInputGain);sourceLeft[i]*=sourceInputGain;sourceRight[i]*=sourceInputGain; }
                const bool replace=!(fmActive&&p.mode!=0);
                for(size_t i=static_cast<size_t>(b);i<static_cast<size_t>(e);++i) {
                    left[i]=(replace?0.f:left[i])+sourceLeft[i]; right[i]=(replace?0.f:right[i])+sourceRight[i];
                }
            }
            if(graphActive&&!graphRendered) {
              if(p.mode==2) {
                float performanceVelocity=fmEngine.activeVoices()>0?lastVelocity:0.f,performanceHz=lastNoteHz;
                if(auto* src=activeSource()) if(fmEngine.activeVoices()==0 && src->activeVoices()>0) { performanceVelocity=src->lastVelocity(); performanceHz=src->lastPitchHz(); }
                graph.setPerformance(performanceVelocity,performanceHz);
                graph.process(left.data()+b,right.data()+b,
#if JucePlugin_IsSynth
                nullptr,
#else
                external.getNumChannels()>0?side.data()+b:nullptr,
#endif
                  cnt);
              } else for(int i=b;i<e;++i) {
                excite(i);
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
            };
            if(!extendedActive) renderRange(0,n);
            else {
                // Modulation destinations 85..112: the graph updates them every 16 samples, inside graph.process. Cut the chunk at those
                // ticks and apply the offsets the graph holds at the start of each piece, so the result does not depend on how the host
                // (or MIDI timing) cuts the buffer.
                for(int b=0;b<n;) {
                    const int untilTick=graph.samplesUntilControl();
                    const int e=b+juce::jmin(n-b,untilTick>0?untilTick:16);
                    // Samples [T,T+16) use the offsets of the tick at T-16: at a period start (the tick fires inside this very
                    // sample) that is the graph's latest value, further into the period the one before the latest tick.
                    applyExtendedModulation(untilTick>0?graph.extendedModulationPrevious():graph.extendedModulation(),false);
                    renderRange(b,e); b=e;
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
    // Source layer: Resynth (1, driven by the input) or Synth (2, MIDI). Switching engine resets both so no voice leaks across engines.
    const float engineRaw=allRaw[static_cast<size_t>(aether::sourceOffset+aether::sourceEngine)]->load(std::memory_order_relaxed);
    const int engineChoice=std::isfinite(engineRaw)?juce::jlimit(0,2,static_cast<int>(std::lround(engineRaw))):0;
    const bool sourceEnabledNow=allRaw[static_cast<size_t>(aether::sourceOffset+aether::sourceEnabled)]->load(std::memory_order_relaxed)>0.5f;
    const int wantedSource=(sourceEnabledNow && engineChoice>0 && (p.mode==1||p.mode==2)) ? engineChoice:0;
    if(wantedSource!=activeSourceEngine) { phaseGeometry.reset(); chaosSource.reset(); activeSourceEngine=wantedSource; }
    atlasSourceActive=graphActive||((fmActive||wantedSource>0)&&p.mode!=0);
    if(atlasSourceActive) {
        auto source=p; for(auto& enabled:source.enabled) enabled=false;
        source.outputDb=0;source.mix=1;engine.setParameters(source);
        auto output=p;for(auto& enabled:output.enabled) enabled=false;output.enabled[4]=p.enabled[4];
        output.inputDb=0;output.mode=0;output.mix=1;outputStage.setParameters(output);
        auto configuration=graphParameters();configuration.tempo=p.tempo;graph.setParameters(configuration);
        // Destinations 85..112 are live only while a graph row points at one of them (and something can consume them).
        // Otherwise the original path below runs untouched, so patches without such a row render exactly as in 2.2.0.
        extendedActive=false; wantedSourceEngine=wantedSource;
        if(p.mode!=0 && (fmActive||wantedSource>0))
            for(const auto& row:configuration.modulation)
                if(row.source!=0 && row.target>=aether::modulationTargetExtendedFirst && row.depth!=0) { extendedActive=true; break; }
        if(extendedActive) {
            // The host values are the baseline; the modulated copies are rebuilt from them at every graph control tick and
            // never written back to the parameters.
            fmBase=fmParameters();
            if(wantedSource==1) pgBase=phaseGeometryParameters(); else if(wantedSource==2) chaosBase=chaosParameters();
            for(size_t k=0;k<extendedTargets.size();++k) {
                const float v=allRaw[static_cast<size_t>(extendedTargets[k].rawIndex)]->load(std::memory_order_relaxed);
                extendedBase[k]=std::isfinite(v)?v:aether::parameterSpecs()[static_cast<size_t>(extendedTargets[k].rawIndex)].defaultValue;
            }
            // Hand the engines the host values NOW, before this block's MIDI is dispatched (a note started in this block reads the
            // Source parameters, e.g. the seed, when it starts). The offsets are those of the period the previous sample was in, so the
            // state a note sees does not depend on where the host cut the buffer. Later periods are applied by renderSegment.
            applyExtendedModulation(graph.extendedModulationPrevious(),true);
        } else {
            fmEngine.setParameters(fmParameters());
            if(wantedSource==1) phaseGeometry.setParameters(phaseGeometryParameters());
            else if(wantedSource==2) chaosSource.setParameters(chaosParameters());
        }
    } else { extendedActive=false; engine.setParameters(p); }
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
// True when a motion row of this state tree points at destination 85..112 (such a save is stamped aether::stateSchemaDestinations).
static bool treeUsesExtendedDestination(const juce::ValueTree& tree) {
    for(int n=1;n<=aether::modular::modulationCount;++n) {
        const auto row=tree.getChildWithProperty("id","modulation."+juce::String(n)+".target");
        if(!row.isValid()) continue;
        const double value=static_cast<double>(row.getProperty("value",0.0));
        if(std::isfinite(value) && aether::modulationTargetNeedsSchema4(value)) return true;
    }
    return false;
}
void AetherAudioProcessor::getStateInformation(juce::MemoryBlock& data) {
    auto tree=state.copyState();
    bool extended=treeUsesExtendedDestination(tree);
    { const juce::ScopedLock lock(metadataLock);
      tree.setProperty("preset",presetName,nullptr);
      tree.setProperty("snapshotB",snapshotB.load(),nullptr);
      if(snapshot.isValid()) {
          auto other=snapshot.createCopy(); const bool otherExtended=treeUsesExtendedDestination(other);
          other.setProperty("schema",otherExtended?aether::stateSchemaDestinations:aether::stateSchema,nullptr);
          extended=extended||otherExtended;
          tree.setProperty("alternateSnapshot",other.toXmlString(),nullptr);
      }
    }
    tree.setProperty("program",currentProgram.load(),nullptr);
    tree.setProperty("schema",extended?aether::stateSchemaDestinations:aether::stateSchema,nullptr); tree.setProperty("version","2.0.0",nullptr);
    randomizer_.writeLocksTo(tree); // optional <random> child, only when locks or a recipe exist; saves without them stay byte-identical
    auto xml=tree.createXml(); copyXmlToBinary(*xml,data);
}
void AetherAudioProcessor::setStateInformation(const void* data,int size) {
    if(data==nullptr || size<=0 || size>1024*1024) return;
    // JUCE binary XML is magic(4)+stringLength(4)+utf8[stringLength]+NUL.
    // getXmlFromBinary reads jmin(size-8, stringLength), so a truncated blob can still
    // yield a partial AetherState (schema intact, PARAM list incomplete or empty) that
    // merge would apply. Refuse any incomplete header/payload before touching state.
    if(size<9) return;
    if(juce::ByteOrder::littleEndianInt(data)!=0x21324356) return;
    const int stringLength=static_cast<int>(juce::ByteOrder::littleEndianInt(juce::addBytesToPointer(data,4)));
    if(stringLength<=0 || size<stringLength+9) return;
    auto xml=getXmlFromBinary(data,size);
    if(!xml || !xml->hasTagName("AetherState")) return;
    auto tree=juce::ValueTree::fromXml(*xml);
    if(!tree.isValid() || !tree.hasType("AetherState")) return;
    const int schema=static_cast<int>(tree.getProperty("schema",1));
    if(schema<1||schema>aether::stateSchemaNewest) return;
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
            // A save that predates a parameter (schema 1/2 never contain the source layer) must not inherit whatever
            // the running instance holds: such parameters return to their defaults, which keep the source layer off.
            else if(dest.isValid() && spec.version>=2) dest.setProperty("value",spec.defaultValue,nullptr);
        }
        if(schema==1) {
            result.getChildWithProperty("id","graph.enabled").setProperty("value",0,nullptr);
            result.getChildWithProperty("id","fm.enabled").setProperty("value",0,nullptr);
        }
        result.setProperty("schema",aether::stateSchema,nullptr);result.setProperty("version","2.0.0",nullptr);
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
    randomizer_.readLocksFrom(tree); // outside merge(): a missing or malformed <random> node means empty locks
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AetherAudioProcessor(); }

