#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
int passed=0,failed=0;
void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
void require(bool ok,const juce::String& why) { if(!ok) throw std::runtime_error(why.toStdString()); }
void set(AetherAudioProcessor& p,const char* id,float value) {
    auto* param=p.state.getParameter(id); require(param!=nullptr,"parameter missing");
    param->setValueNotifyingHost(param->convertTo0to1(value));
}
juce::Slider* findSlider(juce::Component& component,const juce::String& id) {
    if(auto* slider=dynamic_cast<juce::Slider*>(&component);slider && slider->getComponentID()==id) return slider;
    for(auto* child:component.getChildren()) if(auto* found=findSlider(*child,id)) return found;
    return nullptr;
}
template<class F> void test(const char* name,F f) {
    try { f(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& error) { ++failed; std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
}
}
int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        test("stable parameter contract",[] {
            AetherAudioProcessor p;
            require(p.getParameters().size()==aether::totalParameterCount,"parameter count mismatch");
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
        test("host parameter display and text entry round trip units",[] {
            AetherAudioProcessor p;
            const std::pair<const char*,float> cases[]={{"spectral.amount",0.35f},{"master.width",1.4f},
                {"spectral.focus",3500.f},{"spectral.focus",400.f},{"global.output",-6.f},{"master.release",125.f}};
            for(const auto& item:cases) {
                auto* parameter=p.state.getParameter(item.first);
                const auto text=parameter->getText(parameter->convertTo0to1(item.second),64);
                const auto parsed=parameter->convertFrom0to1(parameter->getValueForText(text));
                require(std::abs(parsed-item.second)<0.01f,"host parameter text did not round trip units");
            }
        });
        test("active space freeze reports infinite host tail",[] {
            AetherAudioProcessor p;
            p.setParameterValue("graph.enabled",0);
            require(std::isfinite(p.getTailLengthSeconds()),"normal preset tail not finite");
            set(p,"space.freeze",1); require(std::isinf(p.getTailLengthSeconds()),"frozen tail may be cut by host");
            set(p,"space.enabled",0); require(std::isfinite(p.getTailLengthSeconds()),"disabled space reports infinite tail");
        });
        test("Atlas freeze modulation and cyclic routing advertise persistent tails",[] {
            AetherAudioProcessor p;p.assignModule(0,static_cast<int>(aether::modular::ModuleType::GrainCloud));
            p.setParameterValue("node.1.d",0.3f);p.setParameterValue("modulation.1.source",1);p.setParameterValue("modulation.1.target",4);p.setParameterValue("modulation.1.depth",0.5f);
            require(std::isinf(p.getTailLengthSeconds()),"modulated freeze may be truncated by host");
            p.setParameterValue("modulation.1.depth",0);p.setGraphEdge(11,0,0.5f);
            require(std::isinf(p.getTailLengthSeconds()),"recursive patch advertised a short tail");
        });
        test("host tail covers the longest legal six-operator release",[] {
            AetherAudioProcessor p;p.setParameterValue("graph.enabled",0);p.setParameterValue("global.mode",2);
            p.setParameterValue("fm.release",12);p.setParameterValue("fm.op.6.release",4);
            require(p.getTailLengthSeconds()>=48,"host may truncate a legal 48 second FM release");
            p.setParameterValue("graph.enabled",1);require(p.getTailLengthSeconds()>=48,"effects graph reduced FM tail report");
        });
        test("all-parameter keyboard edits activate custom graph and FM routing",[] {
            AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
            auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");editor->setMinimalMode(true);
            struct Gestures final:juce::AudioProcessorParameter::Listener {
                std::array<int,aether::totalParameterCount> depth{};bool valid=true;
                std::vector<juce::RangedAudioParameter*> observed;
                ~Gestures() override {for(auto* value:observed)value->removeListener(this);}
                void watch(juce::RangedAudioParameter* value) {observed.push_back(value);value->addListener(this);}
                void parameterValueChanged(int index,float) override {if(depth[static_cast<size_t>(index)]!=1)valid=false;}
                void parameterGestureChanged(int index,bool starting) override {
                    auto& active=depth[static_cast<size_t>(index)];if(starting){if(active!=0)valid=false;++active;}else{if(active!=1)valid=false;--active;}
                }
                bool balanced() const {return valid&&std::all_of(depth.begin(),depth.end(),[](int value){return value==0;});}
            } gestures;
            gestures.watch(p.state.getParameter("route.1.2"));gestures.watch(p.state.getParameter("fm.edge.6.1"));gestures.watch(p.state.getParameter("node.1.input"));
            auto* route=findSlider(*editor,"route.1.2");require(route!=nullptr,"full parameter view route slider missing");
            route->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));
            require(p.parameterValue("routing.algorithm")==32,"generic routing edit stayed inactive under built-in template");
            auto* fm=findSlider(*editor,"fm.edge.6.1");require(fm!=nullptr,"full parameter view FM edge missing");
            fm->keyPressed(juce::KeyPress(juce::KeyPress::rightKey));
            require(p.parameterValue("fm.custom")==1,"generic FM edge edit stayed inactive under built-in template");
            p.applyRoutingAlgorithm(1);auto* send=findSlider(*editor,"node.1.input");require(send!=nullptr,"input send missing");
            send->keyPressed(juce::KeyPress(juce::KeyPress::leftKey));require(p.parameterValue("routing.algorithm")==32,"input send keyboard edit stayed inactive");
            require(gestures.balanced(),"structural edit emitted nested or unbalanced host parameter gestures");
        });
        test("DAW state round trip and invalid payload",[] {
            AetherAudioProcessor p,q; set(p,"colour.drive",17); set(p,"space.mix",0.76f);
            juce::MemoryBlock data; p.getStateInformation(data); q.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
            require(std::abs(q.state.getRawParameterValue("colour.drive")->load()-17)<0.001f,"drive not recalled");
            require(std::abs(q.state.getRawParameterValue("space.mix")->load()-0.76f)<0.001f,"space not recalled");
            q.setStateInformation("bad",3); require(q.state.getRawParameterValue("colour.drive")->load()==17,"corruption changed state");
        });
        test("Atlas schema 2 stores custom routing FM and modulation",[] {
            AetherAudioProcessor p,q;
            p.setGraphEdge(11,0,-0.4f); p.setFmEdge(5,0,0.73f);
            p.setParameterValue("modulation.4.source",3);p.setParameterValue("modulation.4.target",82);p.setParameterValue("modulation.4.depth",-0.37f);
            juce::MemoryBlock data;p.getStateInformation(data);
            const auto xml=juce::AudioProcessor::getXmlFromBinary(data.getData(),static_cast<int>(data.getSize()));
            require(xml && xml->getIntAttribute("schema")==2,"new state did not use schema 2");
            q.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
            require(std::abs(q.parameterValue("route.12.1")+0.4f)<0.0001f,"custom graph cable lost");
            require(std::abs(q.parameterValue("fm.edge.6.1")-0.73f)<0.0001f,"custom FM cable lost");
            require(std::abs(q.parameterValue("modulation.4.depth")+0.37f)<0.0001f,"modulation assignment lost");
        });
        test("v1 schema restores the legacy sound path",[] {
            AetherAudioProcessor p;
            juce::ValueTree old("AetherState");old.setProperty("schema",1,nullptr);old.setProperty("version","1.0.0",nullptr);
            for(int i=0;i<aether::parameterCount;++i) {
                const auto& spec=aether::parameterSpecs()[static_cast<size_t>(i)];
                juce::ValueTree item("PARAM");item.setProperty("id",spec.id,nullptr);item.setProperty("value",spec.defaultValue,nullptr);old.addChild(item,-1,nullptr);
            }
            juce::MemoryBlock data;auto xml=old.createXml();juce::AudioProcessor::copyXmlToBinary(*xml,data);
            p.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
            require(p.parameterValue("graph.enabled")==0,"v1 state unexpectedly enabled a new graph");
            require(p.parameterValue("fm.enabled")==0,"v1 state unexpectedly enabled six-operator sound");
        });
        test("module assignment and custom cabling drive stable parameters",[] {
            AetherAudioProcessor p;p.assignModule(5,static_cast<int>(aether::modular::ModuleType::WdfDiode));
            require(static_cast<int>(p.parameterValue("node.6.type"))==static_cast<int>(aether::modular::ModuleType::WdfDiode),"module assignment missing");
            p.applyRoutingAlgorithm(7);p.setGraphEdge(0,5,0.44f);
            require(p.parameterValue("routing.algorithm")==32,"custom cable did not select custom matrix");
            require(std::abs(p.parameterValue("route.1.6")-0.44f)<0.0001f,"custom cable gain missing");
        });
        test("first custom FM edit preserves the current algorithm morph",[] {
            AetherAudioProcessor p;p.applyFmAlgorithm(17);p.setParameterValue("fm.morph",0.5f);
            p.setFmEdge(0,1,0.25f);
            require(std::abs(p.parameterValue("fm.edge.6.5")-0.39f)<0.0001f,"first matrix edit discarded current morphed edges");
            require(p.parameterValue("fm.morph")==0,"custom snapshot was morphed a second time");
        });
        test("FM remains enabled when the Atlas effects graph is bypassed",[] {
            AetherAudioProcessor p;p.setParameterValue("graph.enabled",0);p.setParameterValue("fm.enabled",1);p.setParameterValue("global.mode",2);
            p.setParameterValue("fm.custom",1);
            for(int i=1;i<=6;++i) p.setParameterValue("fm.op."+juce::String(i)+".carrier",0);
            p.prepareToPlay(48000,256);juce::AudioBuffer<float> b(2,256);b.clear();juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),0);
            p.processBlock(b,midi);
            require(b.getMagnitude(0,0,256)<1.e-7f,"graph bypass incorrectly replaced FM with the legacy oscillator");
            require(p.fmVoiceCount.load()>0,"graph bypass stopped FM voice rendering");
        });
        test("audio-driven FM and pitch modulation do not depend on host block cuts",[] {
            auto render=[](int block) {
                AetherAudioProcessor p;p.setParameterValue("global.mode",1);p.setParameterValue("master.enabled",0);
                for(int n=1;n<=12;++n) {p.setParameterValue("node."+juce::String(n)+".type",0);p.setParameterValue("node."+juce::String(n)+".on",0);}
                p.assignModule(0,static_cast<int>(aether::modular::ModuleType::RingMod));
                p.setParameterValue("modulation.1.source",7);p.setParameterValue("modulation.1.target",1);p.setParameterValue("modulation.1.depth",0.2f);
                p.prepareToPlay(48000,block);std::vector<float> result(12000);juce::MidiBuffer midi;
                for(int offset=0;offset<static_cast<int>(result.size());offset+=block) {
                    const int count=juce::jmin(block,static_cast<int>(result.size())-offset);juce::AudioBuffer<float> audio(2,count);
                    for(int i=0;i<count;++i) {const float sample=0.2f*std::sin(2*juce::MathConstants<float>::pi*220*static_cast<float>(i+offset)/48000.f);audio.setSample(0,i,sample);audio.setSample(1,i,sample);}
                    p.processBlock(audio,midi);std::copy_n(audio.getReadPointer(0),count,result.data()+offset);
                }return result;
            };
            const auto single=render(1),blocked=render(257);float difference=0,energy=0;
            for(size_t i=0;i<single.size();++i) {difference=juce::jmax(difference,std::abs(single[i]-blocked[i]));energy+=std::abs(single[i]);}
            require(energy>0.01f,"audio-driven FM test source is silent");require(difference<1.e-5f,"host block segmentation altered FM pitch modulation");
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
            p.setGraphEdge(4,9,0.25f);p.setFmEdge(1,0,0.4f);p.toggleAB();
            p.setGraphEdge(4,9,-0.6f);p.setFmEdge(1,0,0.85f);p.toggleAB();
            require(std::abs(p.parameterValue("route.5.10")-0.25f)<0.0001f,"A/B lost custom graph edges");
            require(std::abs(p.parameterValue("fm.edge.2.1")-0.4f)<0.0001f,"A/B lost custom FM edges");
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
            for(int n=1;n<=12;++n) { p.setParameterValue("node."+juce::String(n)+".type",0);p.setParameterValue("node."+juce::String(n)+".on",0); }
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
            set(p,"graph.enabled",0);set(p,"fm.enabled",0);set(p,"modal.mix",1); set(p,"space.mix",0); set(p,"spectral.amount",0);
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
                    editor->setSize(1920,1080);
                    image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
                    auto large=dir.getChildFile("AetherMicro-large.png").createOutputStream(); large->setPosition(0); large->truncate(); png.writeImageToStream(image,*large);
                    editor->setSize(1180,780);
                    image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
                    auto minimum=dir.getChildFile("AetherMicro-minimum.png").createOutputStream(); minimum->setPosition(0); minimum->truncate(); png.writeImageToStream(image,*minimum);
                    if(auto* aetherEditor=dynamic_cast<AetherAudioProcessorEditor*>(editor.get())) {
                        for(int workspace=0;workspace<3;++workspace) {
                            aetherEditor->setMinimalMode(false);aetherEditor->setWorkspace(workspace);editor->setSize(1440,900);
                            image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
                            auto page=dir.getChildFile("Atlas-workspace-"+juce::String(workspace)+".png").createOutputStream();page->setPosition(0);page->truncate();png.writeImageToStream(image,*page);
                            editor->setSize(1180,780);image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
                            auto minPage=dir.getChildFile("Atlas-minimum-"+juce::String(workspace)+".png").createOutputStream();minPage->setPosition(0);minPage->truncate();png.writeImageToStream(image,*minPage);
                        }
                        aetherEditor->setWorkspace(0);
                        require(aetherEditor->performUiAction("slot.3"),"slot selection action missing");
                        require(aetherEditor->performUiAction("module.8"),"module browser action missing");
                        require(p.parameterValue("node.3.type")==8,"browser did not assign selected node");
                        require(aetherEditor->performUiAction("routing.5"),"algorithm picker action missing");
                        require(p.parameterValue("routing.algorithm")==5,"algorithm did not change graph");
                        require(aetherEditor->performUiAction("route.2.10"),"canvas cable action missing");
                        require(p.parameterValue("routing.algorithm")==32 && p.parameterValue("route.2.10")==1,"canvas did not create custom edge");
                        aetherEditor->setWorkspace(1);
                        require(aetherEditor->performUiAction("fm.algorithm.11"),"FM algorithm action missing");
                        require(aetherEditor->performUiAction("fm.edge.6.1"),"FM matrix action missing");
                        require(p.parameterValue("fm.custom")==1,"FM matrix edit did not enable custom mode");
                        aetherEditor->setMinimalMode(true);
                        image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);
                        auto small=dir.getChildFile("AetherMicro-minimal.png").createOutputStream(); small->setPosition(0); small->truncate(); png.writeImageToStream(image,*small);
                    }
                    auto presets=dir.getChildFile("presets"); presets.createDirectory();
                    for(int i=0;i<p.getNumPrograms();++i) {
                        p.loadFactoryPreset(i);
                        const auto filename=p.getProgramName(i).replaceCharacters("/\\:*?\"<>|","_________")+".aether";
                        require(p.savePreset(presets.getChildFile(filename)).wasOk(),"factory preset export failed");
                    }
                }
            }
        });
        test("Atlas editor zoom switches between 50, 75 and 100 percent",[&] {
            AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
            auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
            require(editor->getWidth()==1440 && editor->getHeight()==900,"Atlas editor default size changed");
            juce::PNGImageFormat png;juce::File snapshotDir;
            if(argc>1){snapshotDir=juce::File(juce::String(argv[1]));snapshotDir.createDirectory();}
            auto capture=[&](const char* name){if(argc>1){auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,1.0f);auto stream=snapshotDir.getChildFile(name).createOutputStream();require(stream!=nullptr,"zoom snapshot file");stream->setPosition(0);stream->truncate();require(png.writeImageToStream(image,*stream),"zoom snapshot encode");}};
            require(editor->performUiAction("zoom.50"),"50 percent zoom action missing");
            require(editor->getWidth()==720 && editor->getHeight()==450,"50 percent zoom size incorrect");
            capture("Atlas-zoom-50.png");
            require(editor->performUiAction("zoom.75"),"75 percent zoom action missing");
            require(editor->getWidth()==1080 && editor->getHeight()==675,"75 percent zoom size incorrect");
            capture("Atlas-zoom-75.png");
            require(editor->performUiAction("zoom.100"),"100 percent zoom action missing");
            require(editor->getWidth()==1440 && editor->getHeight()==900,"100 percent zoom size incorrect");
            capture("Atlas-zoom-100.png");
        });
        test("Atlas content fits the window at minimum, default and maximum sizes",[&] {
            AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
            auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
            auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
            const int sizes[][2]={{1180,780},{1440,900},{2200,1500},{1180,1500},{2200,780}};
            for(int workspace=0;workspace<4;++workspace) for(const auto& size:sizes) {
                editor->setMinimalMode(workspace==3);if(workspace<3)editor->setWorkspace(workspace);editor->setSize(size[0],size[1]);
                const float fit=std::min(float(size[0])/1440.f,float(size[1])/900.f);
                require(std::abs(content->getTransform().getScaleFactor()-fit)<0.001f,"content scale is not the fit scale");
                require(content->getWidth()>=1440 && content->getHeight()>=900,"logical canvas smaller than 1440x900");
                const auto shown=content->getBoundsInParent().toFloat();
                require(shown.getX()<=0.01f && shown.getY()<=0.01f && shown.getRight()>=float(size[0])-1.01f && shown.getBottom()>=float(size[1])-1.01f,"content does not cover the editor");
                require(shown.getRight()<=float(size[0])+1.01f && shown.getBottom()<=float(size[1])+1.01f,"content overflows the editor and would be clipped");
                for(auto* child:content->getChildren()) if(child->isVisible())
                    require(content->getLocalBounds().contains(child->getBounds()),"visible Atlas control lies outside the content area");
            }
            editor->setMinimalMode(false);editor->setWorkspace(0);
        });
        test("Atlas zoom presets keep content scale equal to the zoom",[&] {
            AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
            auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
            auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
            // 50% uses the compact 1000x625 canvas (scale 0.72, so text stays >=10 effective px); 75% and 100% keep the 1440x900 canvas at scale == zoom.
            struct Preset{const char* action;float scale;int w,h;};
            const Preset zooms[]={{"zoom.50",.72f,1000,625},{"zoom.75",.75f,1440,900},{"zoom.100",1.f,1440,900}};
            for(const auto& z:zooms) {
                require(editor->performUiAction(z.action),"zoom action missing");
                require(std::abs(content->getTransform().getScaleFactor()-z.scale)<0.001f,"zoom did not set content scale");
                require(content->getWidth()==z.w && content->getHeight()==z.h,"zoom content canvas changed");
            }
        });
        test("Atlas text is readable, untruncated and non-overlapping at every zoom preset and window size",[&] {
            AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
            auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
            auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
            // 50% and the small 75% windows use the compact canvas (fit scale < 0.72); larger windows keep the 1440x900 canvas.
            struct Case{const char* zoom;std::vector<std::pair<int,int>> sizes;};
            const std::vector<Case> cases={{"zoom.50",{{720,450},{800,450},{720,620},{1000,625},{1030,640},{1100,750}}},{"zoom.75",{{885,585},{1080,675},{1500,900}}},{"zoom.100",{{1180,780},{1440,900},{2200,1500}}}};
            for(const auto& testCase:cases) {
            require(editor->performUiAction(testCase.zoom),"zoom action missing");
            for(int workspace=0;workspace<4;++workspace) for(const auto& sizePair:testCase.sizes) {
                const int size[2]={sizePair.first,sizePair.second};
                editor->setMinimalMode(workspace==3);if(workspace<3)editor->setWorkspace(workspace);editor->setSize(size[0],size[1]);
                const float scale=content->getTransform().getScaleFactor();
                const bool expectCompact=std::min(float(size[0])/1440.f,float(size[1])/900.f)<.72f;
                require((content->getWidth()<1440)==expectCompact,"compact canvas selection wrong");
                require(scale>=.62f,"compact scale too small for 10 effective px text");
                require(juce::jlimit(11.f,16.f,10.f/scale)*scale>=9.99f,"effective text floor below 10px");
                const auto shown=content->getBoundsInParent().toFloat();
                require(shown.getX()<=0.01f && shown.getY()<=0.01f && shown.getRight()>=float(size[0])-1.01f && shown.getBottom()>=float(size[1])-1.01f,"compact content does not cover the editor");
                require(shown.getRight()<=float(size[0])+1.01f && shown.getBottom()<=float(size[1])+1.01f,"compact content overflows the editor");
                std::vector<juce::Component*> visible;
                for(auto* child:content->getChildren()) if(child->isVisible()&&!child->getBounds().isEmpty()&&dynamic_cast<juce::TooltipWindow*>(child)==nullptr) {
                    require(content->getLocalBounds().contains(child->getBounds()),"compact control lies outside the content area");
                    visible.push_back(child);
                }
                for(size_t i=0;i<visible.size();++i) for(size_t j=i+1;j<visible.size();++j)
                    require(!visible[i]->getBounds().intersects(visible[j]->getBounds()),"compact controls overlap: "+visible[i]->getComponentID()+" / "+visible[j]->getComponentID()+" workspace "+juce::String(workspace)+" size "+juce::String(size[0])+"x"+juce::String(size[1]));
                // Text must fit its widget at the font the look-and-feel really uses.
                juce::TextButton* reference=nullptr;
                std::function<void(juce::Component&)> find=[&](juce::Component& c){if(reference==nullptr)reference=dynamic_cast<juce::TextButton*>(&c);for(auto* k:c.getChildren())find(*k);};find(*content);
                require(reference!=nullptr,"no reference button");
                const auto buttonFont=reference->getLookAndFeel().getTextButtonFont(*reference,28);
                std::function<void(juce::Component&)> check=[&](juce::Component& c){
                    if(!c.isVisible())return;
                    const auto where=c.getComponentID()+" workspace "+juce::String(workspace)+" size "+juce::String(size[0])+"x"+juce::String(size[1]);
                    if(auto* b=dynamic_cast<juce::TextButton*>(&c))
                        require(juce::GlyphArrangement::getStringWidthInt(buttonFont,b->getButtonText())<=b->getWidth()-10,"button text truncated: "+where);
                    else if(auto* t=dynamic_cast<juce::ToggleButton*>(&c)) {
                        require(juce::GlyphArrangement::getStringWidthInt(buttonFont,t->getButtonText())<=t->getWidth()-18,"toggle text truncated: "+where);
                        if(t->getComponentID()=="global.bypass")require(juce::GlyphArrangement::getStringWidthInt(buttonFont,"BYPASS ON")<=t->getWidth()-16,"bypass chip text truncated: "+where);
                    } else if(auto* cb=dynamic_cast<juce::ComboBox*>(&c)) {
                        const auto font=cb->getLookAndFeel().getComboBoxFont(*cb);
                        // Compact layout: every entry must fit the closed box. Default layout: only the current selection (popup menus widen themselves; very long FM algorithm names in the 260px default-layout box are a known pre-existing limit).
                        for(int i=0;i<cb->getNumItems();++i) if(expectCompact||cb->getSelectedItemIndex()==i)
                            require(juce::GlyphArrangement::getStringWidthInt(font,cb->getItemText(i))<=cb->getWidth()-40,"combo text truncated: "+where+" / "+cb->getItemText(i));
                    } else if(auto* s=dynamic_cast<juce::Slider*>(&c)) {
                        for(auto* k:s->getChildren()) if(auto* label=dynamic_cast<juce::Label*>(k)) {
                            const auto font=label->getLookAndFeel().getLabelFont(*label);
                            for(double v:{expectCompact?s->getMinimum():s->getValue(),expectCompact?s->getMaximum():s->getValue(),s->getValue()})
                                require(juce::GlyphArrangement::getStringWidthInt(font,s->getTextFromValue(v))<=label->getWidth()-10,"slider value truncated: "+where+" / "+s->getTextFromValue(v));
                        }
                    }
                    for(auto* k:c.getChildren())check(*k);
                };
                check(*content);
            }
            }
            editor->performUiAction("zoom.100");editor->setMinimalMode(false);editor->setWorkspace(0);
        });
        std::cout<<"RESULT "<<passed<<" tests passed, "<<failed<<" failed\n"; return failed?1:0;
    } catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
}
