#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
int passed=0;
void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
void set(AetherAudioProcessor& p,const char* id,float value) {
    auto* param=p.state.getParameter(id); require(param!=nullptr,"parameter missing");
    param->setValueNotifyingHost(param->convertTo0to1(value));
}
template<class F> void test(const char* name,F f) { f(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        test("stable parameter contract",[] {
            AetherAudioProcessor p;
            require(p.getParameters().size()==aether::parameterCount,"parameter count mismatch");
            for(const auto& spec:aether::parameterSpecs()) {
                auto* v=p.state.getParameter(spec.id);
                require(v!=nullptr,"parameter not exposed");
                require(std::abs(v->convertFrom0to1(0)-spec.minimum)<0.01f,"parameter min");
                require(std::abs(v->convertFrom0to1(1)-spec.maximum)<0.01f,"parameter max");
            }
        });
        test("host bypass is transparent, including odd blocks",[] {
            AetherAudioProcessor p; p.prepareToPlay(48000,64); set(p,"global.bypass",1);
            juce::MidiBuffer midi;
            for(const int n:{1,17,64,513,4097}) {
                juce::AudioBuffer<float> b(2,n); b.clear();
                for(int i=0;i<n;++i) { b.setSample(0,i,0.2f); b.setSample(1,i,-0.1f); }
                p.processBlock(b,midi);
                for(int i=0;i<n;++i) require(b.getSample(0,i)==0.2f && b.getSample(1,i)==-0.1f,"bypass changed audio");
            }
        });
        test("DAW state round trip and invalid payload",[] {
            AetherAudioProcessor p,q; set(p,"colour.drive",17); set(p,"space.mix",0.76f);
            juce::MemoryBlock data; p.getStateInformation(data); q.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
            require(std::abs(q.state.getRawParameterValue("colour.drive")->load()-17)<0.001f,"drive not recalled");
            require(std::abs(q.state.getRawParameterValue("space.mix")->load()-0.76f)<0.001f,"space not recalled");
            q.setStateInformation("bad",3); require(q.state.getRawParameterValue("colour.drive")->load()==17,"corruption changed state");
        });
        test("all factory presets render finite stereo",[] {
            AetherAudioProcessor p; p.prepareToPlay(48000,64);
            for(int k=0;k<p.getNumPrograms();++k) {
                p.loadFactoryPreset(k); p.reset();
                juce::AudioBuffer<float> b(2,257); juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),13);
                for(int block=0;block<16;++block) {
                    for(int c=0;c<2;++c) for(int i=0;i<257;++i) b.setSample(c,i,0.1f*std::sin(static_cast<float>(i+block*257)*0.1f));
                    p.processBlock(b,midi); midi.clear();
                    for(int c=0;c<2;++c) for(int i=0;i<257;++i) require(std::isfinite(b.getSample(c,i)),"preset non-finite");
                }
            }
        });
        test("user preset JSON roundtrip and validation",[] {
            AetherAudioProcessor p; set(p,"modal.tune",7);
            auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("aether-test",".aether");
            require(p.savePreset(file).wasOk(),"preset save failed");
            set(p,"modal.tune",-4); require(p.loadPreset(file).wasOk(),"preset load failed");
            require(std::abs(p.state.getRawParameterValue("modal.tune")->load()-7)<0.001f,"preset not restored");
            file.replaceWithText("{\"schema\":999,\"parameters\":{}}");
            require(p.loadPreset(file).failed(),"unsupported schema accepted"); file.deleteFile();
        });
        test("A/B snapshots restore independent values",[] {
            AetherAudioProcessor p; set(p,"colour.drive",5); p.toggleAB(); set(p,"colour.drive",19); p.toggleAB();
            require(std::abs(p.state.getRawParameterValue("colour.drive")->load()-5)<0.001f,"A not restored");
            p.toggleAB(); require(std::abs(p.state.getRawParameterValue("colour.drive")->load()-19)<0.001f,"B not restored");
            juce::MemoryBlock data; p.getStateInformation(data);
            AetherAudioProcessor q; q.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
            require(q.isSnapshotB(),"B selection not recalled"); q.toggleAB();
            require(std::abs(q.state.getRawParameterValue("colour.drive")->load()-5)<0.001f,"alternate snapshot not recalled");
        });
        test("mono layout, repeated prepare and rates",[] {
            AetherAudioProcessor p; auto layout=p.getBusesLayout();
            layout.inputBuses.set(0,juce::AudioChannelSet::mono()); layout.outputBuses.set(0,juce::AudioChannelSet::mono());
            require(p.setBusesLayout(layout),"mono unsupported");
            juce::MidiBuffer midi; juce::AudioBuffer<float> b(1,997);
            for(double rate:{44100.,48000.,96000.,192000.}) {
                p.prepareToPlay(rate,64); b.clear(); b.setSample(0,0,0.5f); p.processBlock(b,midi);
                for(int i=0;i<b.getNumSamples();++i) require(std::isfinite(b.getSample(0,i)),"mono invalid");
                p.releaseResources();
            }
        });
        test("sample-offset MIDI starts only at its timestamp",[] {
            AetherAudioProcessor p; set(p,"global.mode",2); set(p,"space.mix",0); p.prepareToPlay(48000,128);
            juce::AudioBuffer<float> b(2,128); b.clear(); juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),73); p.processBlock(b,midi);
            float after=0;
            for(int i=0;i<73;++i) require(std::abs(b.getSample(0,i))<1.e-7f,"note started early");
            for(int i=73;i<128;++i) after+=std::abs(b.getSample(0,i));
            require(after>0.0001f,"note did not sound");
        });
        test("external FX sidechain excites resonance without direct leakage",[] {
            AetherAudioProcessor p; auto layout=p.getBusesLayout();
            layout.inputBuses.set(1,juce::AudioChannelSet::mono());
            require(p.setBusesLayout(layout),"mono sidechain unsupported");
            set(p,"modal.mix",1); set(p,"space.mix",0); set(p,"spectral.amount",0);
            p.prepareToPlay(48000,256);
            juce::AudioBuffer<float> b(3,256); juce::MidiBuffer midi; float energy=0;
            for(int block=0;block<20;++block) {
                b.clear(); for(int i=0;i<256;++i) b.setSample(2,i,0.3f*std::sin(0.0144f*static_cast<float>(i+block*256)));
                p.processBlock(b,midi); for(int i=0;i<256;++i) energy+=std::abs(b.getSample(0,i));
            }
            require(energy>0.01f,"sidechain did not excite modal stage");
            set(p,"modal.enabled",0); p.reset(); b.clear(); b.applyGain(1);
            for(int i=0;i<256;++i) b.setSample(2,i,0.3f);
            p.processBlock(b,midi);
            for(int i=0;i<256;++i) require(std::abs(b.getSample(0,i))<1.e-7f,"sidechain leaked directly to output");
        });
        test("editor lifecycle and rendered snapshots",[&] {
            AetherAudioProcessor p; p.prepareToPlay(48000,512);
            if(argc>1) {
                juce::AudioBuffer<float> audio(2,512); juce::MidiBuffer midi;
                for(int block=0;block<32;++block) {
                    for(int c=0;c<2;++c) for(int i=0;i<512;++i) audio.setSample(c,i,0.22f*std::sin(0.06f*static_cast<float>(i+block*512)));
                    p.processBlock(audio,midi);
                }
            }
            for(int k=0;k<3;++k) {
                std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
                require(editor && editor->getWidth()>=700,"editor missing");
                if(argc>1 && k==0) {
                    auto dir=juce::File(juce::String(argv[1])); dir.createDirectory();
                    juce::PNGImageFormat png;
                    auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
                    auto stream=dir.getChildFile("AetherMicro-main.png").createOutputStream(); require(stream!=nullptr,"snapshot file");
                    stream->setPosition(0); stream->truncate();
                    require(png.writeImageToStream(image,*stream),"snapshot encode");
                    editor->setSize(1280,900);
                    image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
                    auto large=dir.getChildFile("AetherMicro-large.png").createOutputStream(); large->setPosition(0); large->truncate(); png.writeImageToStream(image,*large);
                    editor->setSize(900,680);
                    image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
                    auto minimum=dir.getChildFile("AetherMicro-minimum.png").createOutputStream(); minimum->setPosition(0); minimum->truncate(); png.writeImageToStream(image,*minimum);
                    if(auto* aetherEditor=dynamic_cast<AetherAudioProcessorEditor*>(editor.get())) {
                        aetherEditor->setMinimalMode(true);
                        image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
                        auto small=dir.getChildFile("AetherMicro-minimal.png").createOutputStream(); small->setPosition(0); small->truncate(); png.writeImageToStream(image,*small);
                    }
                    auto presets=dir.getChildFile("presets"); presets.createDirectory();
                    for(int i=0;i<p.getNumPrograms();++i) { p.loadFactoryPreset(i); p.savePreset(presets.getChildFile(p.getProgramName(i)+".aether")); }
                }
            }
        });
        std::cout<<"RESULT "<<passed<<" tests passed\n"; return 0;
    } catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
}
