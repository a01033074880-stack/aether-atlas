#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <thread>
#include <aether/Rand.h>
#include <aether/RandState.h>
#include <stdexcept>

namespace {
int passed=0,failed=0,skipped=0;
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
// First component with the given id and type whose own chain of parents (below `c`) is entirely visible; hidden pages are pruned.
template<class T> T* findShown(juce::Component& c,const juce::String& id) {
    if(!c.isVisible()) return nullptr;
    if(auto* t=dynamic_cast<T*>(&c);t && c.getComponentID()==id) return t;
    for(auto* child:c.getChildren()) if(auto* found=findShown<T>(*child,id)) return found;
    return nullptr;
}
double luminance(juce::Colour c) {
    auto lin=[](float v){return v<=.03928f?v/12.92:std::pow((v+.055)/1.055,2.4);};
    return .2126*lin(c.getFloatRed())+.7152*lin(c.getFloatGreen())+.0722*lin(c.getFloatBlue());
}
double contrast(juce::Colour a,juce::Colour b) {const double la=luminance(a),lb=luminance(b);return (std::max(la,lb)+.05)/(std::min(la,lb)+.05);}
// Mode picker helpers: edition 0 = AETHER Micro (FX + Resynth), 1 = AETHER Micro Synth (Synth only). Combo item ids must equal the real parameter index + 1.
juce::String modeItems(const juce::ComboBox& c) {juce::StringArray a;for(int i=0;i<c.getNumItems();++i)a.add(juce::String(c.getItemId(i))+":"+c.getItemText(i));return a.joinIntoString("|");}
juce::String modeText(int edition,int raw) {const int shown=edition==1?2:(raw==2?0:juce::jlimit(0,1,raw));return shown==0?"FX":shown==1?"Resynth":"Synth";}
// The real modulation destination names (85 + 28 options), exactly as the host sees them; the tests below also pin them against docs/modulation-destinations.md.
juce::StringArray realDestinations() {
    for(const auto& s:aether::parameterSpecs()) if(juce::String(s.id)=="modulation.1.target") return s.choices;
    throw std::runtime_error("modulation.1.target spec missing");
}
// docs/modulation-destinations.md, options 85..112 (menu name -> engine: 1 generic Source, 2 Phase Geometry, 3 Chaos, 4 FM).
struct DocDestination {const char* name;int engine;};
const DocDestination docDestinations[28]={
    {"Source / Level",1},{"Source / Spread",1},{"Source / Transpose",1},
    {"Source / Walsh seq",2},{"Source / Walsh mix",2},{"Source / CZ depth",2},{"Source / CZ env",2},{"Source / CZ velocity",2},{"Source / VPS D",2},{"Source / VPS V",2},{"Source / VPS ratio",2},{"Source / Phase random",2},
    {"Source / Amp step",3},{"Source / Dur step",3},{"Source / Dur range",3},{"Source / Momentum",3},{"Source / Amp barrier",3},
    {"FM / Mod index",4},{"FM / Morph",4},{"FM / Feedback",4},{"FM / Voice level",4},{"FM / Spread",4},
    {"FM / Op 1 level",4},{"FM / Op 2 level",4},{"FM / Op 3 level",4},{"FM / Op 4 level",4},{"FM / Op 5 level",4},{"FM / Op 6 level",4}};
juce::String headingTexts(juce::ComboBox& c) {
    juce::StringArray a;juce::PopupMenu::MenuItemIterator it(*c.getRootMenu());
    while(it.next()) if(it.getItem().isSectionHeader) a.add(it.getItem().text);
    return a.joinIntoString("|");
}
struct ScopedEdition {explicit ScopedEdition(int e){AetherAudioProcessorEditor::setEditionForTesting(e);}~ScopedEdition(){AetherAudioProcessorEditor::setEditionForTesting(-1);}};
// Wording rules: the effect edition never talks about MIDI notes / Synth mode; the Synth edition never mentions Resynth / the input audio.
void requireEditionWording(int edition,const juce::String& s,const juce::String& where) {
    if(edition==0) require(!s.contains("Synth mode")&&!s.contains("MIDI"),"effect edition text mentions Synth/MIDI: "+s+" / "+where);
    else require(!s.containsIgnoreCase("Resynth")&&!s.containsIgnoreCase("input audio"),"Synth edition text mentions Resynth/input: "+s+" / "+where);
}
template<class F> void test(const char* name,F f) {
    try { f(); ++passed; std::cout<<"PASS "<<name<<'\n'; }
    catch(const std::exception& error) { ++failed; std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n'; }
}
// Effect-only cases: they need audio input / bypass / sidechain / mono FX layout, which the instrument edition (JucePlugin_IsSynth=1) does not offer.
// In the instrument build they are skipped with a printed reason; in the effect build they run unchanged.
template<class F> void testFxOnly(const char* name,const char* synthSkipReason,F f) {
#if JucePlugin_IsSynth
    ++skipped; std::cout<<"SKIP "<<name<<" (Synth edition): "<<synthSkipReason<<'\n'; (void)f;
#else
    (void)synthSkipReason; test(name,f);
#endif
}
// ---- Randomize popover on the real RandomizeController backend (the editor builds it by default; the factory below only makes the seam explicit) ----
struct ScopedRandomBackend {
    ScopedRandomBackend() { AetherAudioProcessorEditor::setRandomizeBackendFactory([](AetherAudioProcessor& proc) { return aether::ui::makeEngineBackend(proc); }); }
    ~ScopedRandomBackend() { AetherAudioProcessorEditor::setRandomizeBackendFactory({}); }
};
// RandSpec §1.2 grouping, written out independently of the engine table (parameter id patterns, not its code).
juce::String randomGroupOf(const juce::String& id) {
    juce::StringArray part;part.addTokens(id,".",{});
    if(id=="routing.algorithm"||id=="graph.feedback"||part[0]=="route") return "Chain";
    if(part[0]=="node") return (part[2]=="type"||part[2]=="on"||part[2]=="input"||part[2]=="output")?"Chain":"ModuleParams";
    if(part[0]=="fm") return "FM";
    if(part[0]=="source") return "Source";
    if(part[0]=="lfo"||part[0]=="modulation") return "Motion";
    if(id=="global.input"||id=="global.output"||id=="global.mix"||part[0]=="master"||id=="graph.trim") return "MixGlobal";
    return "System";
}
const juce::StringArray randomGroups{"Chain","ModuleParams","FM","Source","Motion","MixGlobal","System"};
// MixGlobal / System are Never ids (§2.3): the engine leaves them alone even when unlocked.
bool randomGroupMovable(const juce::String& g) { return g!="MixGlobal"&&g!="System"; }
// Source on and a Mode where FM / Source take part (Resynth in the effect build, Synth in the instrument build).
void randomSourceLive(AetherAudioProcessor& p) { p.setParameterValue("global.mode",JucePlugin_IsSynth?2.f:1.f);p.setParameterValue("source.enabled",1);p.setParameterValue("source.engine",1); }
std::map<juce::String,float> randomValues(AetherAudioProcessor& p) {
    std::map<juce::String,float> values;
    for(const auto& spec:aether::parameterSpecs()) values[spec.id]=p.parameterValue(spec.id);   // every parameter, booleans and choices too
    return values;
}
bool sameValues(const std::map<juce::String,float>& a,const std::map<juce::String,float>& b) {
    for(const auto& [id,v]:a) if(std::abs(v-b.at(id))>1.0e-4f) return false;
    return true;
}
// Undo through the controller goes normalised -> real again, so a restored value may sit a few ulp away from the one it had before (sound engineer: <= 4 ulp).
int ulpDistance(float a,float b) {
    auto key=[](float x) { std::int32_t i; std::memcpy(&i,&x,sizeof i); return i<0?std::int32_t(0x80000000u)-i:i; };
    const std::int64_t d=std::int64_t(key(a))-std::int64_t(key(b)); return int(std::min<std::int64_t>(d<0?-d:d,1000000));
}
struct UlpStats { int differing=0,maxUlp=0; };
UlpStats compareUlp(const std::map<juce::String,float>& a,const std::map<juce::String,float>& b) {
    UlpStats s; for(const auto& [id,v]:a) { const int u=ulpDistance(v,b.at(id)); if(u>0) ++s.differing; s.maxUlp=std::max(s.maxUlp,u); } return s;
}
// Counts what a host would hear from the processor's parameters.
struct HostCounter final:juce::AudioProcessorParameter::Listener {
    std::map<int,int> changes,begins,ends;
    void parameterValueChanged(int i,float) override {++changes[i];}
    void parameterGestureChanged(int i,bool start) override {++(start?begins:ends)[i];}
    void reset() {changes.clear();begins.clear();ends.clear();}
    int totalChanges() const {int n=0;for(const auto& [i,c]:changes) n+=c;return n;}
    int totalGestures() const {int n=0;for(const auto& [i,c]:begins) n+=c;for(const auto& [i,c]:ends) n+=c;return n;}
};
struct RandomRig {
    AetherAudioProcessor p; std::unique_ptr<juce::AudioProcessorEditor> base; AetherAudioProcessorEditor* editor=nullptr; aether::ui::RandomizePanel* panel=nullptr; juce::Component* content=nullptr;
    RandomRig() {
        base.reset(p.createEditor()); editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get()); require(editor!=nullptr,"Atlas editor missing");
        panel=editor->randomizePanel(); require(panel!=nullptr,"Randomize panel missing"); content=editor->getChildComponent(0); require(content!=nullptr,"Atlas content missing");
        editor->setSize(1440,900);
        std::uint64_t counter=0; panel->setSeedSource([counter]() mutable { return (++counter)*0x9E3779B97F4A7C15ULL; });
    }
    juce::String textOf(const juce::String& id) { for(const auto& t:editor->randomizeTexts()) if(t.id==id) return t.text; return "<missing "+id+">"; }
};
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
        testFxOnly("host bypass is transparent, including odd blocks","host bypass is an effect-only feature (the instrument edition has no audio input to pass through)",[] {
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
            AetherAudioProcessor p; p.setModeClamp(AetherAudioProcessor::ModeClamp::none); /*legacy: this test needs stored mode 2 in the FX build*/p.setParameterValue("graph.enabled",0);p.setParameterValue("global.mode",2);
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
            require(xml && xml->getIntAttribute("schema")==3,"new state did not use schema 3");
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
        test("source layer: parameter contract (ids, versions, defaults, old order)",[] {
            const auto& specs=aether::parameterSpecs();
            require(aether::sourceParameterCount==28 && aether::totalParameterCount==aether::legacyParameterCount+28,"source parameter block size changed");
            std::set<juce::String> ids;
            for(const auto& spec:specs) require(ids.insert(spec.id).second,"duplicate parameter id "+spec.id);
            AetherAudioProcessor p;
            for(size_t i=0;i<specs.size();++i) {
                const bool source=static_cast<int>(i)>=aether::sourceOffset;
                require(specs[i].version==(source?2:1),"parameter version "+specs[i].id);
                require(source==specs[i].id.startsWith("source."),"source id prefix "+specs[i].id);
                auto* withId=dynamic_cast<juce::AudioProcessorParameterWithID*>(p.getParameters()[static_cast<int>(i)]);
                require(withId && withId->paramID==specs[i].id && withId->getVersionHint()==specs[i].version,"host parameter id/version/order "+specs[i].id);
            }
            require(specs[aether::sourceOffset+aether::sourceEngine].choices==aether::sourceEngineNames() && aether::sourceEngineNames()[0]=="Off","engine choice list");
            require(specs[aether::sourceOffset+aether::sourceEngine].defaultValue==0,"source engine must default to Off");
            require(p.parameterValue("source.engine")==0,"source engine not Off in a new instance");
            // Every source parameter is automatable, including seed and antialias (decision of the owner).
            for(int i=aether::sourceOffset;i<aether::totalParameterCount;++i) {
                const auto& spec=specs[static_cast<size_t>(i)];
                require(p.state.getParameter(spec.id)->isAutomatable(),"source parameter not automatable: "+spec.id);
            }
            require(p.state.getParameter("source.seed")->isAutomatable() && p.state.getParameter("source.antialias")->isAutomatable(),"seed/antialias must be automatable");
            // the legacy block must still end with the last FM edge
            require(specs[static_cast<size_t>(aether::legacyParameterCount-1)].id=="fm.edge.6.6","legacy block no longer ends with the last FM edge");
        });
        test("source layer: schema 3 round trip; schema 1/2 saves return to source Off",[] {
            AetherAudioProcessor p,q;
            p.setParameterValue("source.engine",2);p.setParameterValue("source.pg.sequency",33);p.setParameterValue("source.seed",7);
            p.setParameterValue("source.chaos.segments",19);p.setParameterValue("source.antialias",0);p.setParameterValue("source.chaos.distribution",4);
            juce::MemoryBlock data;p.getStateInformation(data);q.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
            for(const char* id:{"source.engine","source.pg.sequency","source.seed","source.chaos.segments","source.antialias","source.chaos.distribution"})
                require(std::abs(q.parameterValue(id)-p.parameterValue(id))<1.e-4f,juce::String("schema 3 lost ")+id);
            for(const int schema:{1,2}) {
                AetherAudioProcessor r; r.setParameterValue("source.engine",1);r.setParameterValue("source.pg.sequency",50);r.setParameterValue("source.seed",99);
                juce::ValueTree old("AetherState");old.setProperty("schema",schema,nullptr);old.setProperty("version","1.0.0",nullptr);
                const int count=schema==1?aether::parameterCount:aether::legacyParameterCount;
                for(int i=0;i<count;++i) {
                    const auto& spec=aether::parameterSpecs()[static_cast<size_t>(i)];
                    juce::ValueTree item("PARAM");item.setProperty("id",spec.id,nullptr);item.setProperty("value",spec.defaultValue,nullptr);old.addChild(item,-1,nullptr);
                }
                juce::MemoryBlock block;auto xml=old.createXml();juce::AudioProcessor::copyXmlToBinary(*xml,block);
                r.setStateInformation(block.getData(),static_cast<int>(block.getSize()));
                for(int i=aether::sourceOffset;i<aether::totalParameterCount;++i) {
                    const auto& spec=aether::parameterSpecs()[static_cast<size_t>(i)];
                    require(std::abs(r.parameterValue(spec.id)-spec.defaultValue)<1.e-4f,"old schema did not restore default for "+spec.id);
                }
            }
        });
        test("source layer: both engines render deterministic block-independent audio; Off and non-Synth modes are bit-identical to old path",[] {
            auto render=[](int engine,int mode,int block,bool setSourceParameters) {
                AetherAudioProcessor p; p.setModeClamp(AetherAudioProcessor::ModeClamp::none); /*legacy: this test needs stored mode 2 in the FX build*/p.setParameterValue("global.mode",static_cast<float>(mode));p.setParameterValue("graph.enabled",0);p.setParameterValue("fm.enabled",0);
                p.setParameterValue("source.engine",static_cast<float>(engine));
                if(setSourceParameters) { p.setParameterValue("source.pg.sequency",33);p.setParameterValue("source.pg.walshmix",0.6f);p.setParameterValue("source.chaos.segments",9); }
                p.prepareToPlay(48000,block);
                const int total=48000;std::vector<float> out(static_cast<size_t>(total)*2);
                for(int offset=0;offset<total;offset+=block) {
                    const int count=juce::jmin(block,total-offset);juce::AudioBuffer<float> audio(2,count);audio.clear();juce::MidiBuffer midi;
                    if(offset==0) midi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),0);
                    if(offset<=24000 && 24000<offset+count) midi.addEvent(juce::MidiMessage::noteOff(1,60),24000-offset);
                    p.processBlock(audio,midi);
                    for(int i=0;i<count;++i) { out[static_cast<size_t>(offset+i)*2]=audio.getSample(0,i);out[static_cast<size_t>(offset+i)*2+1]=audio.getSample(1,i); }
                }
                return out;
            };
            auto energy=[](const std::vector<float>& v) { double e=0;for(float x:v) { require(std::isfinite(x),"non-finite source output");e+=static_cast<double>(x)*x; } return e; };
            auto maxDiff=[](const std::vector<float>& a,const std::vector<float>& b) { float d=0;for(size_t i=0;i<a.size();++i) d=juce::jmax(d,std::abs(a[i]-b[i]));return d; };
            for(const int engine:{1,2}) {
                const auto a=render(engine,2,480,true),b=render(engine,2,480,true),c=render(engine,2,64,true);
                require(energy(a)>1.0,"source engine produced silence");
                require(maxDiff(a,b)==0.f,"source path is not reproducible between identical runs");
                require(maxDiff(a,c)<1.e-5f,"source path depends on host block size");
            }
            // Engine Off in Synth mode is the old modal path: source parameter values must not change it by a single bit.
            const auto off=render(0,2,480,false),offWithParameters=render(0,2,480,true);
            require(energy(off)>0.0 && maxDiff(off,offWithParameters)==0.f,"source parameters leaked into the old path while engine is Off");
            // FX mode ignores the source. (Resynth drives it from the input since the Resynth split; tested below.)
            for(const int engine:{1,2}) for(const int mode:{0}) {
                const auto selected=render(engine,mode,480,true),reference=render(0,mode,480,true);
                require(maxDiff(selected,reference)==0.f,"source selection changed the output outside Synth/Resynth mode");
            }
        });
        test("source layer: Input gain scales the source like the FM path",[] {
            auto rms=[](int engine,bool fm,float inputDb) {
                AetherAudioProcessor p; p.setModeClamp(AetherAudioProcessor::ModeClamp::none); /*legacy: this test needs stored mode 2 in the FX build*/p.setParameterValue("global.mode",2);p.setParameterValue("graph.enabled",0);
                p.setParameterValue("fm.enabled",fm?1.f:0.f);p.setParameterValue("fm.gain",0.2f);p.setParameterValue("fm.sustain",1);
                p.setParameterValue("source.engine",fm?0.f:static_cast<float>(engine));p.setParameterValue("source.gain",0.2f);p.setParameterValue("source.sustain",1);
                p.setParameterValue("global.input",inputDb);
                p.prepareToPlay(48000,480);
                double sum=0;int count=0;
                for(int offset=0;offset<48000;offset+=480) {
                    juce::AudioBuffer<float> audio(2,480);audio.clear();juce::MidiBuffer midi;
                    if(offset==0) midi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),0);
                    p.processBlock(audio,midi);
                    if(offset>=24000) for(int i=0;i<480;++i) for(int c=0;c<2;++c) { const double v=audio.getSample(c,i);sum+=v*v;++count; }
                }
                return std::sqrt(sum/count);
            };
            for(const float db:{-6.0206f,-12.0412f,6.0206f}) {
                const double expected=std::pow(10.0,db/20.0);
                const double fmRatio=rms(0,true,db)/rms(0,true,0);
                for(const int engine:{1,2}) {
                    const double ratio=rms(engine,false,db)/rms(engine,false,0);
                    std::cout<<"  Input "<<db<<" dB, engine "<<engine<<": source rms ratio "<<ratio<<" (FM path "<<fmRatio<<", expected "<<expected<<")\n";
                    require(rms(engine,false,0)>1.e-3,"source output is silent in the gain test");
                    require(std::abs(ratio/expected-1)<0.02,"Input gain does not scale the source output as expected");
                    require(std::abs(ratio/fmRatio-1)<0.02,"source Input scaling differs from the FM path");
                }
            }
        });
        // ---- Resynth drive of the Source layer and the mode restriction per plugin type --------------------------------
        using Clamp=AetherAudioProcessor::ModeClamp;
        // Input: 220 Hz sine, 0.5 s silence, 330 Hz sine, optional MIDI note; fixed length so every block size sees the same signal.
        auto renderIn=[](Clamp clamp,int mode,int engine,int block,bool midiNote,bool sourceEnabled=true,bool pureSine=false,float hzA=220.f,float hzB=330.f) {
            AetherAudioProcessor p;p.setModeClamp(clamp);
            p.setParameterValue("global.mode",static_cast<float>(mode));p.setParameterValue("graph.enabled",0);p.setParameterValue("fm.enabled",0);
            p.setParameterValue("source.engine",static_cast<float>(engine));p.setParameterValue("source.enabled",sourceEnabled?1.f:0.f);
            p.setParameterValue("source.pg.sequency",33);p.setParameterValue("source.pg.walshmix",pureSine?0.f:0.6f);p.setParameterValue("source.chaos.segments",9);
            if(pureSine) { p.setParameterValue("source.pg.czdepth",0);p.setParameterValue("source.spread",0);p.setParameterValue("source.gain",1); }
            p.prepareToPlay(48000,block);
            const int total=96000;std::vector<float> out(static_cast<size_t>(total)*2);
            for(int offset=0;offset<total;offset+=block) {
                const int count=juce::jmin(block,total-offset);juce::AudioBuffer<float> audio(2,count);juce::MidiBuffer midi;
                for(int i=0;i<count;++i) {
                    const int t=offset+i;float x=0;
                    if(t<24000) x=0.5f*std::sin(6.2831853f*hzA*static_cast<float>(t)/48000.f);
                    else if(t>=48000 && t<72000) x=0.5f*std::sin(6.2831853f*hzB*static_cast<float>(t)/48000.f);
                    audio.setSample(0,i,x);audio.setSample(1,i,x);
                }
                if(midiNote && offset==0) midi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),0);
                p.processBlock(audio,midi);
                for(int i=0;i<count;++i) { out[static_cast<size_t>(offset+i)*2]=audio.getSample(0,i);out[static_cast<size_t>(offset+i)*2+1]=audio.getSample(1,i); }
            }
            return out;
        };
        auto sameBits=[](const std::vector<float>& a,const std::vector<float>& b) { return a.size()==b.size() && std::memcmp(a.data(),b.data(),a.size()*sizeof(float))==0; };
        auto energyOf=[](const std::vector<float>& v,size_t from,size_t to) { double e=0;for(size_t i=from;i<to&&i<v.size();++i) { require(std::isfinite(v[i]),"non-finite output");e+=static_cast<double>(v[i])*v[i]; } return e; };
        test("mode clamp: pure mapping for both plugin types, and the processor applies it where the mode is read",[=] {
            for(int mode=-2;mode<=4;++mode) {
                require(aether::clampModeForPluginType(mode,true)==2,"Micro Synth must always be Synth");
                const int fx=aether::clampModeForPluginType(mode,false);
                require(fx==(mode==1?1:0),"Micro (effect) must allow only FX and Resynth, Synth falls back to FX");
            }
            require(aether::clampModeForPluginType(0,false)==0 && aether::clampModeForPluginType(1,false)==1 && aether::clampModeForPluginType(2,false)==0,"effect mapping 0/1/2");
            require(aether::clampModeForPluginType(0,true)==2 && aether::clampModeForPluginType(1,true)==2 && aether::clampModeForPluginType(2,true)==2,"synth mapping 0/1/2");
            // The option list and the stored value are untouched: 3 choices, a stored 2 stays 2.
            require(aether::parameterSpecs()[aether::mode].choices.size()==3,"global.mode option list changed");
            // Processor level (this executable is the effect build, the Synth restriction is selected explicitly):
            // effect plugin, stored Synth == FX; stored FX/Resynth unchanged. Synth plugin, stored FX/Resynth == Synth.
            for(const bool midi:{false,true}) {
                const auto fx0=renderIn(Clamp::none,0,0,480,midi),fx1=renderIn(Clamp::none,1,0,480,midi),fx2=renderIn(Clamp::none,2,0,480,midi);
                require(sameBits(renderIn(Clamp::fxPlugin,2,0,480,midi),fx0),"effect plugin: stored Synth must render exactly like FX");
                require(sameBits(renderIn(Clamp::fxPlugin,0,0,480,midi),fx0) && sameBits(renderIn(Clamp::fxPlugin,1,0,480,midi),fx1),"effect plugin: FX and Resynth must be unchanged");
                require(sameBits(renderIn(Clamp::synthPlugin,0,0,480,midi),fx2) && sameBits(renderIn(Clamp::synthPlugin,1,0,480,midi),fx2) && sameBits(renderIn(Clamp::synthPlugin,2,0,480,midi),fx2),"Synth plugin: stored FX/Resynth must render exactly like Synth");
            }
            // Same for the Source layer (engine on): an effect plugin storing Synth gets FX (source silent), a Synth plugin storing Resynth gets Synth.
            for(const int engine:{1,2}) {
                require(sameBits(renderIn(Clamp::fxPlugin,2,engine,480,true),renderIn(Clamp::none,0,engine,480,true)),"effect plugin, source on: stored Synth must fall back to FX");
                require(sameBits(renderIn(Clamp::synthPlugin,1,engine,480,true),renderIn(Clamp::none,2,engine,480,true)),"Synth plugin, source on: stored Resynth must play as Synth");
            }
            // getTailLengthSeconds reads the mode through the same function.
            { AetherAudioProcessor fxp,synp,none; fxp.setModeClamp(Clamp::fxPlugin);synp.setModeClamp(Clamp::synthPlugin);none.setModeClamp(Clamp::none);
              for(auto* q:{&fxp,&synp,&none}) { q->setParameterValue("fm.enabled",1);q->setParameterValue("global.mode",2); }
              none.setParameterValue("global.mode",0);
              require(fxp.getTailLengthSeconds()==none.getTailLengthSeconds(),"tail length: effect plugin storing Synth is FX");
              none.setParameterValue("global.mode",2);
              require(synp.getTailLengthSeconds()==none.getTailLengthSeconds(),"tail length: Synth plugin is Synth"); }
            // Old saves: a state with mode 2 loaded into an effect processor keeps the stored value and plays as FX.
            { AetherAudioProcessor src,dst;src.setModeClamp(Clamp::none);src.setParameterValue("global.mode",2);juce::MemoryBlock block;src.getStateInformation(block);
              dst.setModeClamp(Clamp::fxPlugin);dst.setStateInformation(block.getData(),static_cast<int>(block.getSize()));
              require(dst.parameterValue("global.mode")==2.f,"the stored mode value must not be rewritten");
              require(sameBits(renderIn(Clamp::fxPlugin,2,0,480,false),renderIn(Clamp::none,0,0,480,false)),"old save plays as FX"); }
            std::cout<<"  clampModeForPluginType: effect {0,1,2}->{0,1,0}, synth {0,1,2}->{2,2,2}; processor renders match the unclamped reference bit for bit\n";
        });
        testFxOnly("Resynth + Source: input audio drives the source (finite, deterministic, block independent, follows the input pitch)","Resynth needs audio input; the instrument edition has no audio input and always runs Synth (its Source behaviour is covered by aether_resynth_split, Synth form)",[=] {
            for(const int engine:{1,2}) {
                const auto a=renderIn(Clamp::none,1,engine,480,false),b=renderIn(Clamp::none,1,engine,480,false);
                const auto c=renderIn(Clamp::none,1,engine,64,false),d=renderIn(Clamp::none,1,engine,37,false);
                const auto off=renderIn(Clamp::none,1,0,480,false);
                const double e1=energyOf(a,6000,24000),gap=energyOf(a,40000,48000),e2=energyOf(a,52000,72000);
                std::cout<<"  engine "<<engine<<": energy tone1 "<<e1<<", gap "<<gap<<", tone2 "<<e2<<"\n";
                require(e1>1.0 && e2>1.0,"Resynth + Source rendered silence for a tonal input");
                const double gapOff=energyOf(off,40000,48000);
                std::cout<<"  engine "<<engine<<": gap energy source off (chain tail only) "<<gapOff<<"\n";
                require(gap<=gapOff*1.0001+1.e-6*e1,"source does not fade out after the input stops");
                require(!sameBits(a,off),"Resynth with the source on equals the source-off render");
                require(sameBits(a,b),"Resynth + Source is not reproducible between identical runs");
                float worst=0;for(size_t i=0;i<a.size();++i) worst=juce::jmax(worst,std::abs(a[i]-c[i]),std::abs(a[i]-d[i]));
                std::cout<<"  engine "<<engine<<": max |difference| between block 480 and blocks 64/37: "<<worst<<"\n";
                require(worst<1.e-5f,"Resynth + Source depends on the host block size");
                // Source.enabled == false: back to the plain Resynth output
                require(sameBits(renderIn(Clamp::none,1,engine,480,false,false),off),"source.enabled=0 must restore the source-off Resynth output");
            }
            // Pitch following: a pure-sine voice reproduces the input frequency (zero crossing count), input 220 and 330 Hz.
            for(const float hz:{220.f,330.f,110.f}) {
                const auto s=renderIn(Clamp::none,1,1,480,false,true,true,hz,hz);
                int crossings=0;size_t first=0,last=0;
                for(size_t i=6000;i<24000;++i) if((s[(i-1)*2]<0)!=(s[i*2]<0)) { if(!crossings) first=i;last=i;++crossings; }
                const double measured=crossings>1?(crossings-1)/2.0/(static_cast<double>(last-first)/48000.0):0.0;
                std::cout<<"  input "<<hz<<" Hz -> source sine at "<<measured<<" Hz\n";
                require(std::abs(measured/hz-1)<0.03,"driven source does not follow the detected input pitch");
            }
            // Transpose: +12 st doubles the pitch.
            { AetherAudioProcessor p;p.setParameterValue("global.mode",1);p.setParameterValue("graph.enabled",0);p.setParameterValue("fm.enabled",0);p.setParameterValue("source.engine",1);
              p.setParameterValue("source.pg.czdepth",0);p.setParameterValue("source.spread",0);p.setParameterValue("source.transpose",12);p.prepareToPlay(48000,480);
              std::vector<float> o;
              for(int offset=0;offset<24000;offset+=480) { juce::AudioBuffer<float> audio(2,480);juce::MidiBuffer midi;
                  for(int i=0;i<480;++i) { const float x=0.5f*std::sin(6.2831853f*220.f*static_cast<float>(offset+i)/48000.f);audio.setSample(0,i,x);audio.setSample(1,i,x); }
                  p.processBlock(audio,midi);for(int i=0;i<480;++i) o.push_back(audio.getSample(0,i)); }
              int crossings=0;size_t first=0,last=0;for(size_t i=6000;i<o.size();++i) if((o[i-1]<0)!=(o[i]<0)) { if(!crossings) first=i;last=i;++crossings; }
              const double measured=crossings>1?(crossings-1)/2.0/(static_cast<double>(last-first)/48000.0):0.0;
              std::cout<<"  transpose +12 st: input 220 Hz -> "<<measured<<" Hz\n";
              require(std::abs(measured/440.0-1)<0.03,"source.transpose does not shift the driven pitch"); }
        });
        testFxOnly("Resynth + Source: Source Off keeps FX, Resynth and Synth outputs bit-identical (engine Off, Off with parameters, source disabled)","FX/Resynth reference renders need audio input, which the instrument edition does not have (reference would be silent); Synth Source-off identity is covered by aether_split_unchanged, Synth form",[=] {
            for(const int mode:{0,1,2}) for(const bool midi:{false,true}) {
                const auto ref=renderIn(Clamp::none,mode,0,480,midi);
                if(mode!=2) require(energyOf(ref,0,96000)>1.e-6,"reference render is silent"); // Synth without FM/graph/source is legitimately silent
                require(sameBits(renderIn(Clamp::none,mode,0,64,midi),renderIn(Clamp::none,mode,0,64,midi)),"reference not reproducible");
                // source disabled with an engine selected is the same as engine Off
                for(const int engine:{1,2}) require(sameBits(renderIn(Clamp::none,mode,engine,480,midi,false),ref),"source.enabled=0 changed the output");
            }
            // FX mode with an engine selected is untouched (source runs only in Resynth/Synth).
            for(const int engine:{1,2}) require(sameBits(renderIn(Clamp::none,0,engine,480,true),renderIn(Clamp::none,0,0,480,true)),"engine selection changed FX mode");
        });
        test("source layer: automating seed, antialias and sound parameters is finite, deterministic and block-size independent",[] {
            auto render=[](int engine,int block) {
                AetherAudioProcessor p; p.setModeClamp(AetherAudioProcessor::ModeClamp::none); /*legacy: this test needs stored mode 2 in the FX build*/p.setParameterValue("global.mode",2);p.setParameterValue("graph.enabled",0);p.setParameterValue("fm.enabled",0);
                p.setParameterValue("source.engine",static_cast<float>(engine));p.setParameterValue("source.pg.phaserandom",1);
                p.prepareToPlay(48000,block);
                const int total=96000;std::vector<float> out(static_cast<size_t>(total)*2);
                struct Note { int sample;bool on;int note; };
                const Note notes[]={{0,true,60},{7000,true,67},{19000,false,60},{31000,true,72},{43000,false,67},{55000,true,48},{67000,false,72},{79000,false,48}};
                for(int offset=0;offset<total;offset+=block) {
                    const int count=juce::jmin(block,total-offset);
                    if(offset%960==0) { // automation tick, always at a block start for every tested block size
                        const int k=offset/960;
                        p.setParameterValue("source.seed",static_cast<float>(k%11));
                        p.setParameterValue("source.antialias",(k%3)!=0?1.f:0.f);
                        p.setParameterValue("source.pg.sequency",static_cast<float>((k*7)%64));
                        p.setParameterValue("source.pg.walshmix",static_cast<float>(k%5)/4.f);
                        p.setParameterValue("source.chaos.segments",static_cast<float>(2+(k*5)%31));
                        p.setParameterValue("source.chaos.momentum",static_cast<float>(k%4)*0.3f);
                        p.setParameterValue("source.gain",0.2f+0.1f*static_cast<float>(k%4));
                        p.setParameterValue("global.input",static_cast<float>((k%5)-2)*3.f);
                    }
                    juce::AudioBuffer<float> audio(2,count);audio.clear();juce::MidiBuffer midi;
                    for(const auto& n:notes) if(n.sample>=offset && n.sample<offset+count)
                        midi.addEvent(n.on?juce::MidiMessage::noteOn(1,n.note,0.8f):juce::MidiMessage::noteOff(1,n.note),n.sample-offset);
                    p.processBlock(audio,midi);
                    for(int i=0;i<count;++i) for(int c=0;c<2;++c) {
                        const float v=audio.getSample(c,i);require(std::isfinite(v),"NaN/Inf while automating source parameters");
                        out[static_cast<size_t>(offset+i)*2+static_cast<size_t>(c)]=v;
                    }
                }
                return out;
            };
            auto maxDiff=[](const std::vector<float>& a,const std::vector<float>& b) { float d=0;for(size_t i=0;i<a.size();++i) d=juce::jmax(d,std::abs(a[i]-b[i]));return d; };
            for(const int engine:{1,2}) {
                const auto a=render(engine,480),b=render(engine,480),c=render(engine,64),d=render(engine,192);
                double energy=0,peak=0;for(float v:a) { energy+=static_cast<double>(v)*v;peak=juce::jmax(peak,static_cast<double>(std::abs(v))); }
                std::cout<<"  engine "<<engine<<": energy "<<energy<<" peak "<<peak<<", repeat diff "<<maxDiff(a,b)<<", block 64 diff "<<maxDiff(a,c)<<", block 192 diff "<<maxDiff(a,d)<<"\n";
                require(energy>1.0 && peak<8.0,"automated source render is silent or runaway");
                require(maxDiff(a,b)==0.f,"automated source render is not reproducible");
                require(maxDiff(a,c)<1.e-5f && maxDiff(a,d)<1.e-5f,"automated source render depends on block size");
            }
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
            AetherAudioProcessor p; p.setModeClamp(AetherAudioProcessor::ModeClamp::none); /*legacy: this test needs stored mode 2 in the FX build*/p.setParameterValue("graph.enabled",0);p.setParameterValue("fm.enabled",1);p.setParameterValue("global.mode",2);
            p.setParameterValue("fm.custom",1);
            for(int i=1;i<=6;++i) p.setParameterValue("fm.op."+juce::String(i)+".carrier",0);
            p.prepareToPlay(48000,256);juce::AudioBuffer<float> b(2,256);b.clear();juce::MidiBuffer midi;midi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),0);
            p.processBlock(b,midi);
            require(b.getMagnitude(0,0,256)<1.e-7f,"graph bypass incorrectly replaced FM with the legacy oscillator");
            require(p.fmVoiceCount.load()>0,"graph bypass stopped FM voice rendering");
        });
        testFxOnly("audio-driven FM and pitch modulation do not depend on host block cuts","needs audio input and Resynth; the instrument edition always runs Synth mode",[] {
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
        testFxOnly("mono layout, repeated prepare and rates","the test configures mono on the Input bus, which the instrument edition does not have (it has no audio input)",[] {
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
            AetherAudioProcessor p; p.setModeClamp(AetherAudioProcessor::ModeClamp::none); /*legacy: this test needs stored mode 2 in the FX build*/ set(p,"global.mode",2); set(p,"space.mix",0); p.prepareToPlay(48000,128);
            for(int n=1;n<=12;++n) { p.setParameterValue("node."+juce::String(n)+".type",0);p.setParameterValue("node."+juce::String(n)+".on",0); }
            juce::AudioBuffer<float> b(2,128); b.clear(); juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1,60,0.8f),73); p.processBlock(b,midi);
            float after=0;
            for(int i=0;i<73;++i) require(std::abs(b.getSample(0,i))<1.e-7f,"note started early");
            for(int i=73;i<128;++i) after+=std::abs(b.getSample(0,i));
            require(after>0.0001f,"note did not sound");
        });
        testFxOnly("external FX sidechain excites resonance without direct leakage","needs the external sidechain input bus, which the instrument edition does not have",[] {
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
                        for(int workspace=0;workspace<4;++workspace) {
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
            for(int workspace=0;workspace<5;++workspace) for(const auto& size:sizes) {
                editor->setMinimalMode(workspace==4);if(workspace<4)editor->setWorkspace(workspace);editor->setSize(size[0],size[1]);
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
        test("Atlas text is readable, untruncated and non-overlapping at every zoom preset and window size (both plugin editions)",[&] {
            for(int edition=0;edition<2;++edition) {
            ScopedEdition editionGuard(edition);std::cout<<"  edition "<<(edition==1?"AETHER Micro Synth (Synth only)":"AETHER Micro (FX + Resynth)")<<"\n";
            AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
            auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
            auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
            // 50% and the small 75% windows use the compact canvas (fit scale < 0.72); larger windows keep the 1440x900 canvas.
            struct Case{const char* zoom;std::vector<std::pair<int,int>> sizes;};
            int destSlack=100000;juce::String destSlackWhere,destLongest;
            const std::vector<Case> cases={{"zoom.50",{{720,450},{800,450},{720,620},{1000,625},{1030,640},{1100,750}}},{"zoom.75",{{885,585},{1080,675},{1500,900}}},{"zoom.100",{{1180,780},{1440,900},{2200,1500}}}};
            for(const auto& testCase:cases) {
            require(editor->performUiAction(testCase.zoom),"zoom action missing");
            // pages 0-2 patch/fm/motion, 3-6 Source (Off / Phase Geometry / Chaos / Phase Geometry with Mode=FX), 7 All params, 8 FM with the Six operator engine off, 9 FM with Mode=FX (effect edition), 10-13 Motion row 1 targeting a Phase Geometry / Chaos / FM / generic Source destination that is silent (engine Chaos, engine Phase Geometry, FM off, engine Off).
            for(int page=0;page<14;++page) for(const auto& sizePair:testCase.sizes) {
                const int workspace=page<3?page:(page<7?3:(page==7?4:((page==8||page==9)?1:2)));
                const int size[2]={sizePair.first,sizePair.second};
                p.setParameterValue("fm.enabled",(page==8||page==12)?0.f:1.f);
                p.setParameterValue("modulation.1.source",page>=10?1.f:0.f);p.setParameterValue("modulation.1.depth",page>=10?.5f:0.f);p.setParameterValue("modulation.1.target",page==10?88.f:(page==11?97.f:(page==12?102.f:(page==13?85.f:0.f))));
                if(workspace!=3){p.setParameterValue("source.engine",page==10?2.f:(page==13?0.f:1.f));p.setParameterValue("source.enabled",1.f);}
                if(workspace!=3)p.setParameterValue("global.mode",(page==9&&edition==0)?0.f:(edition==1?2.f:1.f));
                if(workspace==3){p.setParameterValue("global.mode",page==6?0.f:(edition==1?2.f:1.f));p.setParameterValue("source.engine",page==3?0.f:(page==5?2.f:1.f));p.setParameterValue("source.enabled",1.f);}
                editor->setMinimalMode(workspace==4);if(workspace<4)editor->setWorkspace(workspace);editor->setSize(size[0],size[1]);if(workspace==3)(void)editor->sourceStatus();
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
                // FM workspace: the hint texts and the rule above the operator columns must not touch each other or any control.
                if(workspace==1) {
                    const auto geo=editor->fmPaintGeometry();
                    const juce::String where="fm size "+juce::String(size[0])+"x"+juce::String(size[1])+" "+testCase.zoom;
                    require(geo.dividerY>0&&geo.dividerX1>geo.dividerX0,"fm divider missing: "+where);
                    for(const auto& r:{geo.matrixHint,geo.liveVoices}) if(!r.isEmpty()) {
                        require(content->getLocalBounds().contains(r),"fm hint outside content: "+where);
                        require(r.getBottom()+4<=geo.dividerY,"fm hint touches the operator divider: "+where);
                        if(!expectCompact) require((geo.dividerY-r.getY())%4==0&&r.getHeight()%4==0,"fm hint is off the 4px grid: "+where);
                        for(auto* child:visible) require(!r.intersects(child->getBounds()),"fm hint overlaps control "+child->getComponentID()+": "+where);
                    }
                    for(auto* child:visible) require(!(child->getY()<geo.dividerY&&child->getBottom()>geo.dividerY),"control straddles the fm divider: "+child->getComponentID()+" "+where);
                }
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
                        // Every entry (all FM algorithm names, routing algorithms, modulation targets, ...) must fit the closed box, and every slider's minimum, maximum and current value text must fit its value box.
                        for(int i=0;i<cb->getNumItems();++i)
                            require(juce::GlyphArrangement::getStringWidthInt(font,cb->getItemText(i))<=cb->getWidth()-40,"combo text truncated: "+where+" / "+cb->getItemText(i));
                    } else if(auto* s=dynamic_cast<juce::Slider*>(&c)) {
                        for(auto* k:s->getChildren()) if(auto* label=dynamic_cast<juce::Label*>(k)) {
                            const auto font=label->getLookAndFeel().getLabelFont(*label);
                            for(double v:{s->getMinimum(),s->getMaximum(),s->getValue()})
                                require(juce::GlyphArrangement::getStringWidthInt(font,s->getTextFromValue(v))<=label->getWidth()-10,"slider value truncated: "+where+" / "+s->getTextFromValue(v));
                        }
                    }
                    for(auto* k:c.getChildren())check(*k);
                };
                check(*content);
                const juce::String here="page "+juce::String(page)+" "+testCase.zoom+" "+juce::String(size[0])+"x"+juce::String(size[1]);
                const float floorSize=juce::jlimit(11.f,16.f,10.f/scale);
                {
                    auto* modeBox=findShown<juce::ComboBox>(*content,"global.mode");require(modeBox!=nullptr,"Mode combo missing: "+here);
                    require(modeItems(*modeBox)==(edition==1?"3:Synth":"1:FX|2:Resynth"),"Mode combo items wrong: "+here+" / "+modeItems(*modeBox));
                    require(modeBox->getText()==modeText(edition,int(p.parameterValue("global.mode")+.5f)),"Mode combo shows "+modeBox->getText()+": "+here);
                    require(modeBox->getSelectedId()>0,"Mode combo blank: "+here);
                }
                if(workspace<3) {
                    // Header notice (right of the tabs): which text, which colour, and that it fits its label at the real font.
                    auto* notice=findShown<juce::Label>(*content,"page.notice");juce::String expected;bool warn=false;
                    if(page==10){expected="A row targets Phase Geometry, but engine is Chaos.";warn=true;}
                    else if(page==11){expected="A row targets Chaos, but engine is Phase Geometry.";warn=true;}
                    else if(page==12){expected="A row targets FM, but FM is silent now.";warn=true;}
                    else if(page==13){expected="A row targets Source, but Source is silent now.";warn=true;}
                    else if(page==0&&edition==1) expected="No audio input: IN carries the Source and FM voices.";
                    else if(workspace==1) {
                        if(page==8){expected="Six operator engine is off. Turn it on to hear FM.";warn=true;}
                        else if(page==9&&edition==0){expected="Idle: Mode is FX. Switch Mode to Resynth for FM.";warn=true;}
                        else expected=edition==1?"FM sounds on MIDI notes in Synth mode.":"FM follows the input audio in Resynth mode.";
                    } else if(page==2&&edition==1) expected="Envelope follows the voices; Pitch the last MIDI note.";
                    require((notice!=nullptr)==expected.isNotEmpty(),"page notice presence wrong: "+here+" / "+(notice?notice->getText():juce::String("<none>")));
                    if(notice!=nullptr) {
                        require(notice->getText()==expected && editor->pageNotice()==expected,"page notice text wrong: "+here+" / "+notice->getText());
                        const auto font=notice->getLookAndFeel().getLabelFont(*notice);
                        require(juce::GlyphArrangement::getStringWidthInt(font,expected)<=notice->getWidth(),"page notice truncated: "+here+" / "+expected);
                        require(notice->getHeight()>=int(font.getHeight())+2,"page notice clipped vertically: "+here);
                        const auto colour=notice->findColour(juce::Label::textColourId);
                        require(colour==juce::Colour(warn?0xff963c24:0xff252b2b),"page notice colour wrong: "+here);
                        require(contrast(colour,juce::Colour(0xffeee9dc))>=4.5,"page notice contrast below 4.5: "+here);
                        requireEditionWording(edition,expected,here);
                        require(notice->getY()<content->getHeight()/4,"page notice is not in the header band: "+here);
                    }
                    if(workspace==2) {
                        // Every Motion row: the destination box must hold the longest real destination name (all 113 options).
                        const auto names=realDestinations();
                        for(int n=1;n<=16;++n) {
                            auto* cb=findShown<juce::ComboBox>(*content,"modulation."+juce::String(n)+".target");require(cb!=nullptr,"Motion destination box missing: "+here);
                            require(cb->getNumItems()==113,"destination box item count "+juce::String(cb->getNumItems())+": "+here);
                            const auto font=cb->getLookAndFeel().getComboBoxFont(*cb);int widest=0;juce::String longest;
                            for(const auto& item:names){const int w=juce::GlyphArrangement::getStringWidthInt(font,item);if(w>widest){widest=w;longest=item;}}
                            if(cb->getWidth()-40-widest<destSlack){destSlack=cb->getWidth()-40-widest;destSlackWhere=here;destLongest=longest;}
                            require(widest<=cb->getWidth()-40,"destination would be truncated ("+longest+", "+juce::String(widest)+"px in "+juce::String(cb->getWidth())+"px): "+here);
                        }
                    }
                }
                if(workspace==3) {
                    const int engine=page==3?0:(page==5?2:1);
                    auto* status=findShown<juce::Label>(*content,"source.status");
                    require(status!=nullptr && !status->getText().isEmpty(),"source status missing: "+here);
                    require(juce::GlyphArrangement::getStringWidthInt(status->getLookAndFeel().getLabelFont(*status),status->getText())<=status->getWidth(),"source status truncated: "+here+" / "+status->getText());
                    const auto note=status->getText();
                    if(engine==0) require(note.containsIgnoreCase("off")&&note.containsIgnoreCase("engine"),"engine Off notice wrong: "+here+" / "+note);
                    else if(page==6&&edition==0) require(note.startsWith("Idle")&&note.contains("Mode is FX")&&note.contains("Resynth")&&!note.contains("Synth mode"),"effect FX notice wrong: "+here+" / "+note);
                    else require(note.startsWith("Active")&&(edition==1?note.contains("MIDI notes in Synth mode"):note.contains("input audio in Resynth mode")),"active status wrong: "+here+" / "+note);
                    requireEditionWording(edition,note,here);
                    if(page==6&&edition==0) require(contrast(status->findColour(juce::Label::textColourId),juce::Colour(0xffeee9dc))>=4.5,"idle notice contrast below 4.5: "+here);
                    int common=0,pg=0,chaos=0;
                    for(auto* child:visible) {
                        const auto id=child->getComponentID();
                        if(id=="source.status"||id=="source.envelope") continue;
                        if(id.startsWith("source.pg.")) ++pg; else if(id.startsWith("source.chaos.")) ++chaos; else if(id.startsWith("source.")) ++common;
                    }
                    require(common==11,"source common control count "+juce::String(common)+": "+here);
                    require(pg==(engine==1?10:0)&&chaos==(engine==2?7:0),"source engine control set wrong ("+juce::String(pg)+"/"+juce::String(chaos)+"): "+here);
                    require((findShown<juce::Component>(*content,"source.envelope")!=nullptr)==!expectCompact,"source envelope preview presence wrong: "+here);
                    const auto texts=editor->sourcePaintTexts();
                    require(!texts.empty(),"source texts missing: "+here);
                    if(engine==0) { bool empty=false;for(const auto& t:texts) empty=empty||t.text.containsIgnoreCase("No engine selected");require(empty,"engine Off empty state text missing: "+here); }
                    for(size_t i=0;i<texts.size();++i) {
                        const auto& t=texts[i];
                        require(content->getLocalBounds().contains(t.area),"source text outside content: "+t.text+" "+here);
                        requireEditionWording(edition,t.text,here);
                        require(contrast(t.colour,juce::Colour(0xffeee9dc))>=4.5,"source text contrast below 4.5: "+t.text+" "+here);
                        if(!t.display) {
                            const auto font=buttonFont.withHeight(juce::jmax(floorSize,t.size));
                            require(juce::GlyphArrangement::getStringWidthInt(font,t.text)<=t.area.getWidth(),"source text truncated: "+t.text+" "+here);
                            require(t.area.getHeight()>=int(font.getHeight())+2,"source text clipped vertically: "+t.text+" "+here);
                            require(float(t.size)*scale>=0&&font.getHeight()*scale>=9.99f,"source text under 10 effective px: "+t.text+" "+here);
                        } else require(t.area.getHeight()>=int(t.size*1.1f),"source display text clipped vertically: "+t.text+" "+here);
                        for(size_t j=i+1;j<texts.size();++j) require(!t.area.intersects(texts[j].area),"source texts overlap: "+t.text+" / "+texts[j].text+" "+here);
                        for(auto* child:visible) require(!t.area.intersects(child->getBounds()),"source text overlaps control "+child->getComponentID()+": "+t.text+" "+here);
                    }
                }
                if(workspace==4) {
                    // All params: all 28 new parameters are listed, and every caption fits its control at the real font.
                    std::map<juce::String,int> width;int sourceCount=0;
                    std::function<void(juce::Component&)> collect=[&](juce::Component& c){if(!c.isVisible())return;if(c.getComponentID().isNotEmpty()) width[c.getComponentID()]=std::max(width[c.getComponentID()],c.getWidth());for(auto* k:c.getChildren())collect(*k);};collect(*content);
                    for(const auto& spec:aether::parameterSpecs()) {
                        require(width.count(spec.id)==1,"All params lacks parameter "+spec.id+": "+here);
                        if(spec.id.startsWith("source.")) ++sourceCount;
                        if(spec.boolean) continue;
                        const auto caption=juce::String(spec.group)+" / "+juce::String(spec.name);
                        require(juce::GlyphArrangement::getStringWidthInt(buttonFont.withHeight(juce::jmax(floorSize,10.f)),caption)<=width[spec.id],"All params caption truncated: "+caption+" "+here);
                    }
                    require(sourceCount==28,"All params source parameter count "+juce::String(sourceCount)+": "+here);
                }
            }
            }
            editor->performUiAction("zoom.100");editor->setMinimalMode(false);editor->setWorkspace(0);
            std::cout<<"  real destinations: tightest fit "<<destSlack<<" px spare for \""<<destLongest.toStdString()<<"\" at "<<destSlackWhere.toStdString()<<"\n";
            }
        });
        test("Atlas page notices (Patch / FM / Motion) follow edition, Mode and the Six operator engine",[&] {
            for(int edition=0;edition<2;++edition) {
                ScopedEdition editionGuard(edition);
                AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
                auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
                auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
                editor->setSize(1440,900);
                const juce::String fxIdle="Idle: Mode is FX. Switch Mode to Resynth for FM.",off="Six operator engine is off. Turn it on to hear FM.";
                const juce::String resynth="FM follows the input audio in Resynth mode.",synth="FM sounds on MIDI notes in Synth mode.";
                auto fmNotice=[&](float mode,float fm){p.setParameterValue("global.mode",mode);p.setParameterValue("fm.enabled",fm);editor->setWorkspace(1);return editor->pageNotice();};
                if(edition==0) {
                    require(fmNotice(0.f,1.f)==fxIdle,"effect FX + FM on notice: "+fmNotice(0.f,1.f));
                    require(fmNotice(1.f,1.f)==resynth,"effect Resynth + FM on notice: "+fmNotice(1.f,1.f));
                    require(fmNotice(2.f,1.f)==fxIdle,"effect with a stored Synth mode shows FX: "+fmNotice(2.f,1.f));
                    require(fmNotice(0.f,0.f)==off&&fmNotice(1.f,0.f)==off,"effect FM-off notice wrong");
                    require(findShown<juce::Label>(*content,"page.notice")!=nullptr,"effect FM notice label hidden");
                    // FX notice is the Source-panel red; the active notice is the ink colour.
                    (void)fmNotice(0.f,1.f);require(findShown<juce::Label>(*content,"page.notice")->findColour(juce::Label::textColourId)==juce::Colour(0xff963c24),"FX notice is not earth red");
                    (void)fmNotice(1.f,1.f);require(findShown<juce::Label>(*content,"page.notice")->findColour(juce::Label::textColourId)==juce::Colour(0xff252b2b),"active FM notice should not be red");
                    editor->setWorkspace(0);require(editor->pageNotice().isEmpty()&&findShown<juce::Label>(*content,"page.notice")==nullptr,"effect Patch page must stay unchanged");
                    editor->setWorkspace(2);require(editor->pageNotice().isEmpty(),"effect Motion page must stay unchanged");
                } else {
                    for(float mode:{0.f,1.f,2.f}) require(fmNotice(mode,1.f)==synth,"Synth FM notice with stored mode "+juce::String(mode)+": "+fmNotice(mode,1.f));
                    require(fmNotice(2.f,0.f)==off,"Synth FM-off notice wrong");
                    editor->setWorkspace(0);require(editor->pageNotice()=="No audio input: IN carries the Source and FM voices.","Synth Patch notice: "+editor->pageNotice());
                    editor->setWorkspace(2);require(editor->pageNotice()=="Envelope follows the voices; Pitch the last MIDI note.","Synth Motion notice: "+editor->pageNotice());
                }
                editor->setWorkspace(3);require(editor->pageNotice().isEmpty()&&findShown<juce::Label>(*content,"page.notice")==nullptr,"Source page must not show the header notice");
                editor->setWorkspace(1);require(findShown<juce::Label>(*content,"page.notice")!=nullptr,"FM notice not back");
                editor->setMinimalMode(true);require(findShown<juce::Label>(*content,"page.notice")==nullptr,"All params must not show the header notice");editor->setMinimalMode(false);
            }
        });
        test("Atlas Synth edition: Envelope / Pitch sources and the input sends stay bound to the real parameters and explain themselves",[&] {
            for(int edition=0;edition<2;++edition) {
                ScopedEdition editionGuard(edition);
                AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
                auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
                auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
                editor->setSize(1440,900);
                const auto* sourceSpec=[]{for(const auto& s:aether::parameterSpecs()) if(juce::String(s.id)=="modulation.1.source") return &s;return static_cast<const aether::ParameterSpec*>(nullptr);}();
                require(sourceSpec!=nullptr,"modulation.1.source spec missing");
                // Patch: the input send and the global Input gain exist in both editions; the Synth edition explains that the input is the instrument's own voices (neither is greyed or hidden: they route / scale Source and FM).
                editor->setWorkspace(0);
                auto* send=findShown<juce::Slider>(*content,"node.1.input");require(send!=nullptr&&send->isEnabled(),"input send must stay visible and enabled");
                auto* globalInput=findShown<juce::Slider>(*content,"global.input");require(globalInput!=nullptr&&globalInput->isEnabled(),"global input must stay visible and enabled");
                for(auto* s:{send,globalInput}) {
                    const auto tip=s->getTooltip();
                    if(edition==1) require(tip.contains("no audio input")&&tip.contains("Source and FM"),"Synth input tooltip wrong: "+tip);
                    else require(!tip.containsIgnoreCase("Synth edition")&&!tip.containsIgnoreCase("Source and FM"),"effect input tooltip changed: "+tip);
                }
                send->setValue(0.5,juce::sendNotificationSync);require(std::abs(p.parameterValue("node.1.input")-0.5f)<.01f,"input send no longer drives node.1.input");
                // Motion: the source list is untouched (same items, same indices), still bound, and every source (Envelope, Pitch included) stays selectable.
                editor->setWorkspace(2);
                auto* source=findShown<juce::ComboBox>(*content,"modulation.1.source");require(source!=nullptr&&source->isEnabled(),"Motion source box missing or disabled");
                require(source->getNumItems()==sourceSpec->choices.size(),"Motion source item count changed");
                for(int i=0;i<source->getNumItems();++i) require(source->getItemText(i)==sourceSpec->choices[i]&&source->getItemId(i)==i+1,"Motion source mapping changed at "+juce::String(i));
                const int envelope=sourceSpec->choices.indexOf("Envelope"),pitch=sourceSpec->choices.indexOf("Pitch");
                std::cout<<"  edition "<<edition<<": Motion sources "<<sourceSpec->choices.joinIntoString(", ").toStdString()<<"\n";
                require(envelope>=0&&pitch>=0,"Envelope / Pitch source names missing: "+sourceSpec->choices.joinIntoString(","));
                for(int idx:{envelope,pitch}) {
                    source->setSelectedItemIndex(idx,juce::sendNotificationSync);require(int(p.parameterValue("modulation.1.source")+.5f)==idx,"source "+sourceSpec->choices[idx]+" does not drive the parameter");
                    if(edition==1) require(source->isEnabled()&&source->getTooltip().contains("no audio input"),"Synth source tooltip wrong: "+source->getTooltip());
                }
            }
        });
        test("Atlas Motion destination selector: real 113 options, Node / Source / FM headings, unchanged indices (both editions)",[&] {
            using Editor=AetherAudioProcessorEditor;
            require(Editor::modulationTargetGroup("None").isEmpty()&&Editor::modulationTargetGroup("Node 3 / Mix")=="Node 3"&&Editor::modulationTargetGroup("Source / Walsh mix")=="Source"&&Editor::modulationTargetGroup("FM / Feedback")=="FM","destination group parsing wrong");
            const auto real=realDestinations();
            // Option table: 0 None, 1..84 Node n / Macro A..D, Mix, Level, Pan, 85..112 exactly the names and parameters of docs/modulation-destinations.md; every name is <= 24 characters.
            require(real.size()==113&&aether::modulationTargetCount==113,"destination count "+juce::String(real.size()));
            require(real[0]=="None"&&real[1]=="Node 1 / Macro A"&&real[84]=="Node 12 / Pan","node destinations changed");
            for(int i=0;i<28;++i) {
                require(real[85+i]==docDestinations[i].name,"destination "+juce::String(85+i)+" is '"+real[85+i]+"', docs say '"+docDestinations[i].name+"'");
                require(real[85+i].length()<=24,"destination name longer than 24: "+real[85+i]);
                const auto& spec=aether::parameterSpecs()[size_t(aether::modulationExtendedParameter[size_t(i)])];const juce::String id(spec.id);
                const int engine=id.startsWith("source.pg.")?2:(id.startsWith("source.chaos.")?3:(id.startsWith("source.")?1:(id.startsWith("fm.")?4:0)));
                require(engine==docDestinations[i].engine,"destination "+real[85+i]+" modulates "+id+" (engine "+juce::String(engine)+"), docs say engine "+juce::String(docDestinations[i].engine));
            }
            juce::StringArray expectedHeadings;for(int n=1;n<=12;++n)expectedHeadings.add("Node "+juce::String(n));
            const auto allHeadings=expectedHeadings.joinIntoString("|")+"|Source|FM";
            for(int edition=0;edition<2;++edition) {
                ScopedEdition editionGuard(edition);
                AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
                auto* editor=dynamic_cast<Editor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
                auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
                editor->setSize(1440,900);editor->setWorkspace(2);
                for(int row=1;row<=16;++row) {
                    auto* box=findShown<juce::ComboBox>(*content,"modulation."+juce::String(row)+".target");require(box!=nullptr,"Motion destination box missing");
                    require(box->getNumItems()==113,"destination box count "+juce::String(box->getNumItems()));
                    require(headingTexts(*box)==allHeadings,"destination headings: "+headingTexts(*box));
                    for(int i=0;i<113;++i) require(box->getItemId(i)==i+1&&box->getItemText(i)==real[i],"destination item mapping changed at "+juce::String(i));
                }
                auto* box=findShown<juce::ComboBox>(*content,"modulation.1.target");
                // Group boundaries: the heading sits in front of 85 (Source) and 102 (FM) only; selecting an item writes its index.
                for(int idx:{0,1,84,85,87,88,96,97,101,102,106,107,112}) {box->setSelectedItemIndex(idx,juce::sendNotificationSync);require(int(p.parameterValue("modulation.1.target")+.5f)==idx&&box->getText()==real[idx],"destination "+juce::String(idx)+" does not drive modulation.1.target");}
                for(int idx:{43,85,101,102,112}) {p.setParameterValue("modulation.1.target",float(idx));require(box->getSelectedItemIndex()==idx&&box->getText()==real[idx],"host change does not select destination "+juce::String(idx));}
                require(Editor::modulationTargetGroup(real[84])=="Node 12"&&Editor::modulationTargetGroup(real[85])=="Source"&&Editor::modulationTargetGroup(real[101])=="Source"&&Editor::modulationTargetGroup(real[102])=="FM"&&Editor::modulationTargetGroup(real[112])=="FM","group boundaries wrong");
            }
        });
        test("Atlas Motion engine notices: Phase Geometry / Chaos / generic Source / FM destinations warn only when they are silent (both editions)",[&] {
            using Editor=AetherAudioProcessorEditor;
            const juce::String sourceSilent="A row targets Source, but Source is silent now.",pgNeedsEngine="A row targets Phase Geometry, but engine is Chaos.",chaosNeedsEngine="A row targets Chaos, but engine is Phase Geometry.",fmSilent="A row targets FM, but FM is silent now.";
            const juce::String synthInfo="Envelope follows the voices; Pitch the last MIDI note.";
            int checks=0;
            for(int edition=0;edition<2;++edition) {
                ScopedEdition editionGuard(edition);
                AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
                auto* editor=dynamic_cast<Editor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
                auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
                editor->setSize(1440,900);editor->setWorkspace(2);
                const float soundingMode=edition==1?2.f:1.f;
                // Mode: the sounding one, and (effect edition only) FX, where neither Source nor FM sound.
                std::vector<float> modes{soundingMode};if(edition==0) modes.push_back(0.f);
                for(float mode:modes) for(int enabled=0;enabled<2;++enabled) for(int engine=0;engine<3;++engine) for(int fmOn=0;fmOn<2;++fmOn) for(int target:{0,20,85,87,88,96,97,101,102,107,112}) for(int depth=0;depth<2;++depth) for(int src:{0,1}) {
                    p.setParameterValue("global.mode",mode);p.setParameterValue("source.enabled",float(enabled));p.setParameterValue("source.engine",float(engine));p.setParameterValue("fm.enabled",float(fmOn));
                    p.setParameterValue("modulation.1.source",float(src));p.setParameterValue("modulation.1.depth",depth?-.4f:0.f);p.setParameterValue("modulation.1.target",float(target));
                    // Independent expectation from the docs table (engine of each destination) and the Source / FM sounding rules.
                    const bool sounding=mode==soundingMode;const bool sourceOn=sounding&&enabled&&engine>0,fmRuns=sounding&&fmOn;
                    juce::String expected;
                    if(src>0&&depth&&target>=85) {
                        const int needs=docDestinations[target-85].engine;
                        if(needs==4) {if(!fmRuns) expected=fmSilent;}
                        else if(!sourceOn) expected=sourceSilent;
                        else if(needs==2&&engine!=1) expected=pgNeedsEngine;
                        else if(needs==3&&engine!=2) expected=chaosNeedsEngine;
                    }
                    const juce::String here="edition "+juce::String(edition)+" mode "+juce::String(mode)+" on "+juce::String(enabled)+" engine "+juce::String(engine)+" fm "+juce::String(fmOn)+" target "+juce::String(target)+" depth "+juce::String(depth)+" source "+juce::String(src);
                    const auto notice=editor->pageNotice();
                    if(expected.isEmpty()) require(notice==(edition==1?synthInfo:juce::String()),"unexpected Motion notice ("+notice+"): "+here);
                    else {
                        require(notice==expected,"Motion notice '"+notice+"' != '"+expected+"': "+here);
                        auto* label=findShown<juce::Label>(*content,"page.notice");require(label!=nullptr,"warning label hidden: "+here);
                        require(label->findColour(juce::Label::textColourId)==juce::Colour(0xff963c24)&&contrast(label->findColour(juce::Label::textColourId),juce::Colour(0xffeee9dc))>=4.5,"warning colour / contrast: "+here);
                        requireEditionWording(edition,notice,here);
                    }
                    ++checks;
                }
            }
            std::cout<<"  engine notice matrix: "<<checks<<" states checked\n";
        });
        test("Atlas Source panel: controls drive the 28 host parameters; engine, Mode and On change the visible set and notice (both plugin editions)",[&] {
            for(int edition=0;edition<2;++edition) {
            ScopedEdition editionGuard(edition);const float soundingRaw=edition==1?2.f:1.f;
            AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
            auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
            auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
            require(editor->performUiAction("workspace.source"),"workspace.source action missing");editor->setWorkspace(3);
            auto countShown=[&](const char* prefix){int n=0;for(auto* child:content->getChildren()) if(child->isVisible()&&child->getComponentID().startsWith(prefix)) ++n;return n;};
            for(const char* zoom:{"zoom.100","zoom.50"}) {
                require(editor->performUiAction(zoom),"zoom action missing");require(editor->performUiAction("workspace.source"),"workspace.source action missing");editor->setWorkspace(3);
                p.setParameterValue("global.mode",soundingRaw);p.setParameterValue("source.engine",0);p.setParameterValue("source.enabled",1);(void)editor->sourceStatus();
                require(countShown("source.pg.")==0&&countShown("source.chaos.")==0,"Off must show no engine controls");
                // Choosing the engine in the real combo box changes the visible block at once (no timer, no manual refresh).
                auto* engineBox=findShown<juce::ComboBox>(*content,"source.engine");require(engineBox!=nullptr,"engine combo not found");require(engineBox->getNumItems()==3,"engine combo items "+juce::String(engineBox->getNumItems()));
                engineBox->setSelectedItemIndex(1,juce::sendNotificationSync);
                require(std::abs(p.parameterValue("source.engine")-1.f)<.01f,"engine combo did not set the parameter");
                require(countShown("source.pg.")==10&&countShown("source.chaos.")==0,"Phase Geometry must show exactly its 10 controls");
                engineBox->setSelectedItemIndex(2,juce::sendNotificationSync);
                require(countShown("source.pg.")==0&&countShown("source.chaos.")==7,"Chaos must show exactly its 7 controls");
                // host-side change (automation / preset) is followed too
                p.setParameterValue("source.engine",1);(void)editor->sourceStatus();
                require(countShown("source.pg.")==10&&countShown("source.chaos.")==0,"host engine change not reflected");
                // every one of the 28 UI controls drives its parameter
                for(int engine=1;engine<=2;++engine) {
                    p.setParameterValue("source.engine",static_cast<float>(engine));(void)editor->sourceStatus();
                    for(const auto& spec:aether::parameterSpecs()) {
                        if(!spec.id.startsWith("source.")) continue;
                        const bool isPg=spec.id.startsWith("source.pg."),isChaos=spec.id.startsWith("source.chaos.");
                        if((isPg&&engine!=1)||(isChaos&&engine!=2)||spec.id=="source.engine") continue;
                        const float before=p.parameterValue(spec.id);
                        if(spec.boolean) {
                            auto* t=findShown<juce::ToggleButton>(*content,spec.id);require(t!=nullptr,"toggle missing: "+spec.id);
                            t->setToggleState(!t->getToggleState(),juce::sendNotificationSync);
                            require(std::abs(p.parameterValue(spec.id)-before)>.5f,"toggle did not change "+spec.id);
                            t->setToggleState(before>.5f,juce::sendNotificationSync);
                        } else if(!spec.choices.isEmpty()) {
                            auto* c=findShown<juce::ComboBox>(*content,spec.id);require(c!=nullptr&&c->getNumItems()==spec.choices.size(),"combo missing: "+spec.id);
                            c->setSelectedItemIndex((int(before)+1)%c->getNumItems(),juce::sendNotificationSync);
                            require(std::abs(p.parameterValue(spec.id)-float((int(before)+1)%c->getNumItems()))<.01f,"combo did not change "+spec.id);
                        } else {
                            auto* s=findShown<juce::Slider>(*content,spec.id);require(s!=nullptr,"slider missing: "+spec.id);
                            const double target=s->proportionOfLengthToValue(std::abs(s->valueToProportionOfLength(before)-.8)>.02?.8:.3);
                            s->setValue(target,juce::sendNotificationSync);
                            require(std::abs(p.parameterValue(spec.id)-float(s->getValue()))<=1.e-3f*float(s->getMaximum()-s->getMinimum()+1)&&std::abs(p.parameterValue(spec.id)-before)>1.e-6f,"slider did not change "+spec.id+" before "+juce::String(before)+" target "+juce::String(target)+" slider "+juce::String(s->getValue())+" param "+juce::String(p.parameterValue(spec.id)));
                        }
                    }
                }
                // host-visible names / automation flags
                for(const auto& spec:aether::parameterSpecs()) if(spec.id.startsWith("source.")) {
                    auto* param=p.state.getParameter(spec.id);require(param!=nullptr&&param->getName(100)==juce::String(spec.name),"host name differs from UI name: "+spec.id);
                    require(param->isAutomatable(),"automation flag (all source params automatable, incl. seed/antialias): "+spec.id);
                }
                // extreme value texts are what the fit tests measure; show them explicitly
                p.setParameterValue("source.engine",1);(void)editor->sourceStatus();
                auto* transpose=findShown<juce::Slider>(*content,"source.transpose");auto* release=findShown<juce::Slider>(*content,"source.release");auto* ratio=findShown<juce::Slider>(*content,"source.pg.vpsratio");
                require(transpose&&release&&ratio,"extreme-value sliders missing");
                require(transpose->getTextFromValue(transpose->getMinimum()).contains("24")&&transpose->getTextFromValue(transpose->getMinimum()).contains("st"),"transpose minimum text: "+transpose->getTextFromValue(transpose->getMinimum()));
                require(release->getTextFromValue(release->getMaximum()).contains("12"),"release maximum text: "+release->getTextFromValue(release->getMaximum()));
                require(ratio->getTextFromValue(ratio->getMaximum()).contains("16"),"vpsratio maximum text: "+ratio->getTextFromValue(ratio->getMaximum()));
                std::cout<<"  source extremes ("<<zoom<<"): transpose \""<<transpose->getTextFromValue(transpose->getMinimum()).toStdString()<<"\" .. \""<<transpose->getTextFromValue(transpose->getMaximum()).toStdString()<<"\", release max \""<<release->getTextFromValue(release->getMaximum()).toStdString()<<"\", vpsratio \""<<ratio->getTextFromValue(ratio->getMinimum()).toStdString()<<"\" .. \""<<ratio->getTextFromValue(ratio->getMaximum()).toStdString()<<"\"\n";
                // notices
                if(edition==0) {
                    p.setParameterValue("global.mode",0);require(editor->sourceStatus().startsWith("Idle")&&editor->sourceStatus().contains("Resynth"),"Mode FX must show the Resynth hint: "+editor->sourceStatus());
                    // a Synth-mode value restored into the effect falls back to FX, so the same hint shows
                    p.setParameterValue("global.mode",2);require(editor->sourceStatus().startsWith("Idle")&&editor->sourceStatus().contains("Mode is FX"),"restored Synth must fall back to the FX hint: "+editor->sourceStatus());
                } else {
                    p.setParameterValue("global.mode",0);require(editor->sourceStatus().startsWith("Active"),"Synth edition ignores a stray FX value: "+editor->sourceStatus());
                }
                p.setParameterValue("global.mode",soundingRaw);p.setParameterValue("source.enabled",0);require(editor->sourceStatus().startsWith("Switched off"),"On=off notice: "+editor->sourceStatus());
                p.setParameterValue("source.enabled",1);require(editor->sourceStatus().startsWith("Active"),"active notice: "+editor->sourceStatus());
                p.setParameterValue("source.engine",0);require(editor->sourceStatus().containsIgnoreCase("off"),"Off notice: "+editor->sourceStatus());
            }
            editor->performUiAction("zoom.100");editor->setMinimalMode(false);editor->setWorkspace(0);
            }
        });
        test("Atlas Mode picker offers only the modes of the plugin edition, keeps real parameter indices and falls back sensibly",[&] {
            for(int edition=0;edition<2;++edition) {
                ScopedEdition editionGuard(edition);const juce::String name=edition==1?"Synth edition":"Micro edition";
                AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
                auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
                auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
                // the parameter itself is untouched: id, name and all three choices stay
                auto* choice=dynamic_cast<juce::AudioParameterChoice*>(p.state.getParameter("global.mode"));require(choice!=nullptr,"global.mode is no choice parameter");
                require(choice->choices==juce::StringArray({"FX","Resynth","Synth"})&&choice->getName(100)=="Mode","global.mode parameter changed: "+name);
                editor->setSize(1440,900);
                auto* mode=findShown<juce::ComboBox>(*content,"global.mode");require(mode!=nullptr,"Mode combo missing: "+name);
                require(modeItems(*mode)==(edition==1?"3:Synth":"1:FX|2:Resynth"),"Mode items: "+name+" / "+modeItems(*mode));
                // every stored value shows an applicable item (never blank) and the picker never writes the parameter by itself
                for(int raw=0;raw<=2;++raw) {
                    p.setParameterValue("global.mode",static_cast<float>(raw));
                    require(mode->getSelectedId()>0&&mode->getText()==modeText(edition,raw),"Mode shows \""+mode->getText()+"\" for stored "+juce::String(raw)+": "+name);
                    require(int(p.parameterValue("global.mode")+.5f)==raw,"showing the fallback changed the parameter: "+name);
                }
                if(edition==0) {
                    // choosing items maps to the real indices 0 / 1 (not shifted), also through the All params page copy of the control
                    p.setParameterValue("global.mode",0);
                    mode->setSelectedId(2,juce::sendNotificationSync);require(int(p.parameterValue("global.mode")+.5f)==1,"Resynth item did not set index 1");
                    mode->setSelectedId(1,juce::sendNotificationSync);require(int(p.parameterValue("global.mode")+.5f)==0,"FX item did not set index 0");
                    editor->setMinimalMode(true);
                    int boxes=0;std::function<void(juce::Component&)> walk=[&](juce::Component& c){if(!c.isVisible())return;if(auto* cb=dynamic_cast<juce::ComboBox*>(&c);cb!=nullptr&&c.getComponentID()=="global.mode"){++boxes;require(modeItems(*cb)=="1:FX|2:Resynth","All params Mode items: "+modeItems(*cb));}for(auto* k:c.getChildren())walk(*k);};walk(*content);
                    require(boxes>=1,"All params page lacks the Mode control");editor->setMinimalMode(false);
                } else {
                    mode->setSelectedId(3,juce::sendNotificationSync);require(int(p.parameterValue("global.mode")+.5f)==2,"Synth item did not set index 2");
                }
            }
        });
        test("Atlas Mode picker follows the compile-time plugin edition (JucePlugin_IsSynth) when no override is set",[&] {
            AetherAudioProcessorEditor::setEditionForTesting(-1);
            AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
            auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
            auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
            editor->setSize(1440,900);
            auto* mode=findShown<juce::ComboBox>(*content,"global.mode");require(mode!=nullptr,"Mode combo missing");
#if JucePlugin_IsSynth
            const int edition=1;
#else
            const int edition=0;
#endif
            std::cout<<"  compile-time edition: "<<(edition==1?"AETHER Micro Synth":"AETHER Micro")<<", Mode items "<<modeItems(*mode).toStdString()<<"\n";
            require(modeItems(*mode)==(edition==1?"3:Synth":"1:FX|2:Resynth"),"compile-time Mode items: "+modeItems(*mode));
            for(int raw=0;raw<=2;++raw){p.setParameterValue("global.mode",static_cast<float>(raw));require(mode->getText()==modeText(edition,raw),"compile-time Mode text for "+juce::String(raw)+": "+mode->getText());}
        });
        test("Randomize: the controls exist by default (no factory installed) and drive the processor's RandomizeController",[&] {
            AetherAudioProcessorEditor::setRandomizeBackendFactory({});
            AetherAudioProcessor p;std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
            auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());require(editor!=nullptr,"Atlas editor missing");
            require(editor->randomizePanel()!=nullptr&&!editor->randomizeTexts().empty(),"Randomize panel missing without a factory");
            editor->setSize(1440,900);auto* content=editor->getChildComponent(0);require(content!=nullptr,"Atlas content missing");
            require(findShown<juce::Button>(*content,"random.header.roll")!=nullptr&&findShown<juce::Button>(*content,"random.header.menu")!=nullptr,"header Randomize buttons are not shown by default");
            require(p.randomizer().undoDepth()==0&&!p.randomizer().recipe().valid,"fresh processor already has a roll");
            editor->randomizePanel()->roll(false);
            require(p.randomizer().undoDepth()==1&&p.randomizer().recipe().valid&&editor->randomizePanel()->undoDepth()==1,"the panel did not go through the processor's controller");
        });
        test("Randomize: seeds are 8 Crockford characters, parse tolerates typing slips, label is \"(Random) XXXX-XXXX\"",[&] {
            std::uint64_t seed=0;
            require(aether::ui::parseRandomSeed("A7F3-92C1",seed)&&aether::ui::formatRandomSeed(seed)=="A7F3-92C1","parse/format round trip of A7F3-92C1");
            std::uint64_t lower=0;require(aether::ui::parseRandomSeed("  a7f3 92c1 ",lower)&&lower==seed,"lowercase / spaces not tolerated");
            std::uint64_t slips=0;require(aether::ui::parseRandomSeed("OOOO-IIIL",slips)&&aether::ui::formatRandomSeed(slips)=="0000-1111","O -> 0, I/L -> 1 mapping");
            std::uint64_t keep=7;require(!aether::ui::parseRandomSeed("A7F3-92C!",keep)&&!aether::ui::parseRandomSeed("",keep)&&!aether::ui::parseRandomSeed("123456789",keep)&&keep==7,"bad seed text was accepted");
            juce::Random rnd(1234);
            for(int i=0;i<2000;++i) {
                const std::uint64_t value=((std::uint64_t(std::uint32_t(rnd.nextInt()))<<8)^std::uint32_t(rnd.nextInt()))&aether::ui::randomSeedMask;
                const auto text=aether::ui::formatRandomSeed(value);std::uint64_t back=0;
                require(text.length()==9&&text[4]=='-'&&aether::ui::parseRandomSeed(text,back)&&back==value,"seed round trip failed: "+text);
                require(aether::ui::randomPresetLabel(value)=="(Random) "+text&&aether::ui::randomPresetLabel(value).length()<=23,"preset label format: "+text);
            }
        });
        test("Randomize: a pinned seed repeats exactly, another seed differs, the seed field rejects junk",[&] {
            ScopedRandomBackend backend;
            RandomRig a,b;std::map<juce::String,float> start=randomValues(a.p);
            for(auto* rig:{&a,&b}) {rig->panel->setPinned(true);require(rig->panel->setSeedText("A7F3-92C1"),"seed text refused");rig->panel->setAmount(.6f);rig->panel->roll(false);}
            require(sameValues(randomValues(a.p),randomValues(b.p)),"same seed gave different values");require(!sameValues(randomValues(a.p),start),"roll changed nothing");
            require(!a.panel->setSeedText("not a seed!")&&a.panel->seedText()=="A7F3-92C1","junk seed replaced the old seed");
            // Mutate + amount 0 => empty patch (§6.2): values stay, recipe still records the roll.
            RandomRig z;z.panel->setMode(aether::ui::RandomizeMode::mutate);z.panel->setPinned(true);z.panel->setSeedText("A7F3-92C1");z.panel->setAmount(0.f);auto z0=randomValues(z.p);const auto empty=z.panel->roll(false);
            require(empty.changedValues==0&&sameValues(z0,randomValues(z.p))&&z.panel->presetLabel().isEmpty()&&z.p.randomizer().undoDepth()==0&&z.textOf("last")=="Nothing changed (amount 0)","amount 0 Mutate was not an empty patch: "+z.textOf("last"));
            RandomRig c;c.panel->setPinned(true);c.panel->setSeedText("0000-0001");c.panel->setAmount(.6f);c.panel->roll(false);
            require(!sameValues(randomValues(a.p),randomValues(c.p)),"another seed gave the same values");
            // unpinned rolls draw a new seed each time and show it
            RandomRig d;d.panel->roll(false);const auto first=d.panel->seedText();d.panel->roll(false);require(first!=d.panel->seedText(),"unpinned seed did not change");
        });
        test("Randomize: locked groups keep their values, every group is lockable and non-empty",[&] {
            ScopedRandomBackend backend;
            RandomRig rig;auto& panel=*rig.panel;panel.setMode(aether::ui::RandomizeMode::full);
            require(aether::ui::randomSpecGroups()==randomGroups&&panel.lockedGroups().isEmpty(),"default locks are not empty: "+panel.lockedGroups().joinIntoString(","));
            {int counts[7]={0,0,0,0,0,0,0},total=0;const int expected[7]={194,84,84,28,64,9,32};
             for(const auto& spec:aether::parameterSpecs()) {++counts[randomGroups.indexOf(randomGroupOf(spec.id))];++total;require(aether::ui::randomGroupOf(spec.id)==randomGroupOf(spec.id),"engine grouping differs from the spec rule: "+juce::String(spec.id));}
             for(int i=0;i<7;++i) require(counts[i]==expected[i],"RandSpec §1.2 count of "+randomGroups[i]+": "+juce::String(counts[i])+" vs "+juce::String(expected[i]));
             require(total==495,"parameter total "+juce::String(total));}
            for(int gi=0;gi<7;++gi) require((rig.panel->findChildWithID("random.lock."+randomGroups[gi])!=nullptr)==(gi<5),"lock chip of "+randomGroups[gi]+" (only the five movable groups get one, bus volume has none)");
            require(rig.panel->findChildWithID("random.rollmotion")!=nullptr&&rig.panel->mode()==aether::ui::RandomizeMode::full&&std::abs(rig.panel->amount()-1.f)<1.0e-6f,"Randomize Motion button missing or defaults are not Full / 100 %");
            randomSourceLive(rig.p);rig.p.setParameterValue("graph.enabled",1);   // already on: this test is about locks, the auto-open of a closed graph has its own test below
            // The seed field must be readable before the first roll too (its text used to keep the default pale colour).
            {panel.setOpen(true);auto* seed=panel.findChildWithID("random.seed");require(seed!=nullptr,"seed field missing");
             if(seed->getWidth()>0) {const auto img=panel.createComponentSnapshot(seed->getBounds(),false,1.f);const auto bg=img.getPixelAt(2,2);double best=1.0;
                 for(int y=0;y<img.getHeight();++y) for(int x=0;x<img.getWidth();++x) best=std::max(best,contrast(img.getPixelAt(x,y),bg));
                 require(best>=4.5,"seed text contrast before the first roll: "+juce::String(best,2));}}            panel.setPinned(true);panel.setSeedText("1234-5678");panel.setAmount(.8f);
            // default: MixGlobal and System untouched, something else moves
            auto before=randomValues(rig.p);panel.roll(false);auto after=randomValues(rig.p);int moved=0;
            for(const auto& [id,v]:before) {if(!randomGroupMovable(randomGroupOf(id)))require(std::abs(after[id]-v)<1.0e-6f,"locked "+randomGroupOf(id)+" value moved: "+id);else if(std::abs(after[id]-v)>1.0e-6f)++moved;}
            require(moved>10,"too few values moved with only MixGlobal / System locked: "+juce::String(moved));
            // one open group at a time: only that group changes
            for(const auto& open:randomGroups) {
                for(const auto& g:randomGroups) panel.setLocked(g,g!=open);
                decltype(panel.roll(false)) recipe;int inGroup=0;   // pinned seed: a fresh one per try (the same seed re-writes the same values); a roll may legitimately leave a group alone (e.g. Synth keeps Source off), so allow a few tries
                for(int attempt=0;attempt<8;++attempt) {
                    panel.setSeedText("A00"+juce::String(randomGroups.indexOf(open))+"-77"+juce::String(randomGroups.indexOf(open))+juce::String(attempt));
                    before=randomValues(rig.p);recipe=panel.roll(false);after=randomValues(rig.p);inGroup=0;
                    for(const auto& [id,v]:before) {const bool changed=after[id]!=v;if(randomGroupOf(id)==open){if(changed)++inGroup;}else require(!changed,"value outside the open group "+open+" moved: "+id);}
                    if(inGroup>0||!randomGroupMovable(open)) break;
                }
                if(randomGroupMovable(open)) require(inGroup>0,"group "+open+" has no movable parameter (rejected "+juce::String(int(recipe.rejected))+", note '"+recipe.note+"', changed "+juce::String(recipe.changedValues)+", mode "+juce::String(rig.p.parameterValue("global.mode"))+", fm "+juce::String(rig.p.parameterValue("fm.enabled"))+", src "+juce::String(rig.p.parameterValue("source.enabled"))+"/"+juce::String(rig.p.parameterValue("source.engine"))+", seed "+panel.seedText()+")"); else require(inGroup==0,"Never group "+open+" moved even though unlocked: "+juce::String(inGroup));
                require(recipe.changedValues>=inGroup&&recipe.changedValues-inGroup<=4&&recipe.touchedGroups==(randomGroupMovable(open)?juce::StringArray({open}):juce::StringArray()),"recipe does not match the roll for "+open+": "+juce::String(recipe.changedValues)+" vs "+juce::String(inGroup));
            }
            // everything locked: nothing changes and the panel says so
            for(const auto& g:randomGroups) panel.setLocked(g,true);
            before=randomValues(rig.p);const auto none=panel.roll(false);require(sameValues(before,randomValues(rig.p))&&none.changedValues==0,"all-locked roll changed values");
            require(rig.textOf("last")=="Nothing changed (all locked)","all-locked message: "+rig.textOf("last"));
            panel.resetLocks();require(panel.lockedGroups().isEmpty(),"resetLocks");
            panel.setLocked("Motion",true);require(panel.isLocked("Motion")&&rig.textOf("lock.count")=="1 locked","lock count text: "+rig.textOf("lock.count"));
            // §4.2 edition rules: FM and Source stay put in FX (effect build) and in Resynth while Source is off.
            for(const auto& g:randomGroups) panel.setLocked(g,g!="FM"&&g!="Source");
            if(!JucePlugin_IsSynth) {
                rig.p.setParameterValue("global.mode",0);before=randomValues(rig.p);panel.roll(false);after=randomValues(rig.p);
                require(sameValues(before,after),"FX mode moved FM / Source values");
                rig.p.setParameterValue("global.mode",1);rig.p.setParameterValue("source.enabled",0);before=randomValues(rig.p);panel.roll(false);after=randomValues(rig.p);
                require(sameValues(before,after),"Resynth with Source off moved FM / Source values");
                rig.p.setParameterValue("source.enabled",1);rig.p.setParameterValue("source.engine",0);before=randomValues(rig.p);panel.roll(false);after=randomValues(rig.p);
                require(sameValues(before,after),"Resynth with Source enabled but engine Off moved FM / Source values (ActiveWhen SrcOn needs engine >= 1)");
            }
            randomSourceLive(rig.p);before=randomValues(rig.p);panel.roll(false);after=randomValues(rig.p);require(!sameValues(before,after),"FM / Source did not move with Source on");
            panel.setLocked("Nope",true);require(!panel.isLocked("Nope"),"unknown group accepted");
        });
        test("Randomize Motion: only Motion-group values move (LFOs, modulation rows), nothing else",[&] {
            ScopedRandomBackend backend;
            RandomRig rig;auto& panel=*rig.panel;
            randomSourceLive(rig.p);rig.p.setParameterValue("graph.enabled",1);
            panel.setPinned(true);panel.setSeedText("5A5A-5A5A");
            auto before=randomValues(rig.p);const auto rec=panel.rollMotion();auto after=randomValues(rig.p);int moved=0;
            for(const auto& [id,v]:before) {const bool changed=after[id]!=v;if(randomGroupOf(id)=="Motion") {if(changed)++moved;} else require(!changed,"Randomize Motion moved a value outside Motion: "+id);}
            require(!rec.rejected&&moved>0,"Randomize Motion moved nothing (rejected "+juce::String(int(rec.rejected))+", note '"+rec.note+"')");
            panel.setLocked("Motion",true);before=randomValues(rig.p);panel.setSeedText("1111-2222");panel.rollMotion();
            require(sameValues(before,randomValues(rig.p)),"Randomize Motion moved values while Motion is locked");
        });
        test("Randomize Global (2.3.1): a Full roll opens a closed graph, moves every group, leaves bus volume alone; Randomize Motion rewrites rows",[&] {
            ScopedRandomBackend backend;
            const juce::StringArray busIds{"global.input","global.output","global.mix","master.enabled","master.width","master.ceiling","master.release","master.dither","fm.gain","source.gain"};
            RandomRig rig;auto& panel=*rig.panel;
            randomSourceLive(rig.p);rig.p.setParameterValue("graph.enabled",0);
            panel.setMode(aether::ui::RandomizeMode::full);panel.setAmount(1.f);panel.setPinned(true);panel.setSeedText("7E57-0231");
            auto before=randomValues(rig.p);const auto rec=panel.roll(false);auto after=randomValues(rig.p);
            require(!rec.rejected&&rec.changedValues>40,"global roll with the graph off was refused or tiny (rejected "+juce::String(int(rec.rejected))+", note '"+rec.note+"', changed "+juce::String(rec.changedValues)+"): "+rig.textOf("last"));
            require(after["graph.enabled"]==1.f,"the engine did not open the graph for a Full roll");
            for(const auto& id:busIds) require(std::abs(after[id]-before[id])<1.0e-6f,"bus volume value moved: "+id);
            require(rig.textOf("last").contains("changed in"),"result line: "+rig.textOf("last"));
            juce::StringArray moved;for(const auto& [id,v]:before) if(after[id]!=v&&!moved.contains(randomGroupOf(id))) moved.add(randomGroupOf(id));
            for(const auto& g:{"Chain","ModuleParams","FM","Source","Motion"}) require(moved.contains(g),"the global roll left group "+juce::String(g)+" untouched");
            // Randomize Motion with the graph off: opens it, writes only Motion (+ graph.enabled), changes row source / destination, never aims at a bus-volume target
            RandomRig m;randomSourceLive(m.p);m.p.setParameterValue("graph.enabled",0);m.panel->setPinned(true);
            int rowsChanged=0;
            for(const char* seed:{"0A0A-0001","0A0A-0002","0A0A-0003"}) {
                m.panel->setSeedText(seed);auto b=randomValues(m.p);const auto r=m.panel->rollMotion();auto a=randomValues(m.p);
                require(!r.rejected&&a["graph.enabled"]==1.f,"Randomize Motion with the graph off: rejected "+juce::String(int(r.rejected))+", note '"+r.note+"'");
                for(const auto& [id,v]:b) {if(randomGroupOf(id)!="Motion"&&id!="graph.enabled") require(a[id]==v,"Randomize Motion moved "+id);}
                for(int row=1;row<=16;++row) {
                    const auto base="modulation."+juce::String(row);
                    if(a[base+".source"]!=b[base+".source"]||a[base+".target"]!=b[base+".target"]) ++rowsChanged;
                    if(std::abs(a[base+".depth"])>1.0e-6f) {const int t=int(std::lround(a[base+".target"]));require(t!=85&&t!=87&&t!=105,"modulation row "+juce::String(row)+" aims at a bus-volume target: "+juce::String(t));}
                }
            }
            require(rowsChanged>=3,"Randomize Motion left the modulation rows' source / destination alone: "+juce::String(rowsChanged));
        });
        test("Randomize: undo history is capped at 16, steps restore the states (<= 4 ulp per parameter), button shows x/16",[&] {
            ScopedRandomBackend backend;
            RandomRig rig;auto& panel=*rig.panel;panel.setAmount(.7f);
            require(panel.undoDepth()==0&&rig.textOf("random.undo")=="Undo Random  0/16"&&!panel.undo(),"fresh undo state: "+rig.textOf("random.undo"));
            std::vector<std::map<juce::String,float>> states;states.push_back(randomValues(rig.p));
            for(int i=1;i<=20;++i) {
                panel.roll(false);states.push_back(randomValues(rig.p));
                require(panel.undoDepth()==std::min(i,16)&&rig.p.randomizer().undoDepth()==std::min(i,16)&&rig.textOf("random.undo")=="Undo Random  "+juce::String(std::min(i,16))+"/16","undo depth after roll "+juce::String(i)+": "+rig.textOf("random.undo"));
            }
            int differingTotal=0,worst=0;
            for(int step=1;step<=16;++step) {
                require(panel.undo(),"undo "+juce::String(step)+" refused");
                const auto s=compareUlp(randomValues(rig.p),states[size_t(20-step)]);differingTotal+=s.differing;worst=std::max(worst,s.maxUlp);
                require(s.maxUlp<=4,"undo "+juce::String(step)+" restored a value "+juce::String(s.maxUlp)+" ulp away");
                require(panel.undoDepth()==16-step&&rig.textOf("random.undo")=="Undo Random  "+juce::String(16-step)+"/16","undo text after step "+juce::String(step)+": "+rig.textOf("random.undo"));
            }
            std::cout<<"  undo x16: "<<differingTotal<<" parameter values differed from the pre-roll value (all within "<<worst<<" ulp; limit 4), of "<<16*int(aether::parameterSpecs().size())<<" compared\n";
            require(!panel.undo()&&compareUlp(randomValues(rig.p),states[4]).maxUlp<=4,"undo went past the 16th step");
            // the same through the editor action and the real button
            panel.roll(false);require(rig.editor->performUiAction("random.undo")&&panel.undoDepth()==0&&compareUlp(randomValues(rig.p),states[4]).maxUlp<=4,"undo action");
        });
        test("Randomize: the preset name is randomPresetName(source, seed), the source is remembered and undo / a preset pick end the roll",[&] {
            ScopedRandomBackend backend;
            RandomRig rig;auto& panel=*rig.panel;
            auto* combo=findShown<juce::ComboBox>(*rig.content,"preset.factory");require(combo!=nullptr,"preset combo missing");
            const auto original=rig.p.currentPresetName();require(combo->getText()==original,"preset combo before roll: "+combo->getText());
            auto nameOf=[](const juce::String& source,const juce::String& seedText) {std::uint64_t s=0;aether::ui::parseRandomSeed(seedText,s);return juce::String(juce::CharPointer_UTF8(aether::rand::randomPresetName(std::string(source.toRawUTF8()),s).c_str()));};
            panel.setPinned(true);panel.setSeedText("a7f3-92c1");panel.roll(false);
            require(rig.p.currentPresetName()==nameOf(original,"A7F3-92C1")&&combo->getText()==rig.p.currentPresetName(),"preset name after roll: "+combo->getText());
            require(rig.p.currentPresetName().endsWith(" (Random) A7F392C1")&&rig.p.currentPresetName().startsWith(original),"preset name format: "+rig.p.currentPresetName());
            require(panel.presetLabel()=="(Random) A7F3-92C1"&&rig.textOf("preset.value")=="(Random) A7F3-92C1","panel label: "+panel.presetLabel());
            require(panel.sourcePreset()==original&&rig.textOf("preset.source")=="from "+original,"source preset not remembered: "+rig.textOf("preset.source"));
            panel.notePreset(rig.p.currentPresetName(),rig.p.isSnapshotB());require(panel.presetLabel()=="(Random) A7F3-92C1","timer tick dropped the label");
            panel.roll(false);panel.setSeedText("0000-0002");panel.roll(false);require(panel.sourcePreset()==original&&combo->getText()==nameOf(original,"0000-0002"),"chained rolls: "+combo->getText());
            // undo to the start restores the preset name
            while(panel.undo()) {}require(panel.presetLabel().isEmpty()&&combo->getText()==original&&rig.p.currentPresetName()==original,"undo to the start kept the random name: "+combo->getText());
            // picking a factory preset ends the roll
            panel.roll(false);require(combo->getText().contains("(Random) "),"roll after undo");
            combo->setSelectedId(3,juce::sendNotificationSync);
            require(panel.presetLabel().isEmpty()&&combo->getText()==AetherAudioProcessor::factoryPresetNames()[2],"preset pick kept the random name: "+combo->getText());
            // a preset that changed behind our back (host recall) does not keep the label either
            panel.roll(false);panel.notePreset("Somebody else",false);require(panel.presetLabel().isEmpty(),"foreign preset kept the label");
            // a very long source name is cut, the seed never is
            {juce::String longSource;for(int i=0;i<30;++i) longSource<<"Warm Pad ";const auto name=juce::String(juce::CharPointer_UTF8(aether::rand::randomPresetName(std::string(longSource.toRawUTF8()),0xA7F392C1ull).c_str()));
             require(name.getNumBytesAsUTF8()<=128&&name.endsWith("(Random) "+juce::String(juce::CharPointer_UTF8(aether::rand::randomPresetName("x",0xA7F392C1ull).c_str())).getLastCharacters(8)),"long source name: "+name);}
            // the other A/B face does not wear the label
            RandomRig slot;slot.panel->roll(false);require(slot.panel->presetLabel().startsWith("(Random) "),"no label after the roll");
            slot.p.toggleAB();slot.panel->notePreset(slot.p.currentPresetName(),slot.p.isSnapshotB());
            const bool keptOnB=slot.panel->presetLabel().isNotEmpty();
            std::cout<<"  after A/B toggle the label of a roll made on A is "<<(keptOnB?"shown":"hidden")<<" (preset name now '"<<slot.p.currentPresetName()<<"')\n";
            require(!keptOnB,"label shown on the wrong A/B face");
            slot.p.toggleAB();slot.panel->notePreset(slot.p.currentPresetName(),slot.p.isSnapshotB());
        });
        test("Randomize into B: no host notification, the user stays on A, B takes the roll; undo returns B to its previous content",[&] {
            ScopedRandomBackend backend;
            RandomRig rig;auto& panel=*rig.panel;panel.setAmount(.7f);
            const auto a=randomValues(rig.p);require(!rig.p.isSnapshotB(),"A is not live at start");
            const auto name=rig.p.currentPresetName();
            const auto bBefore=rig.p.randomizer().captureSnapshot(aether::rand::Face::B);
            HostCounter counter;for(auto* prm:rig.p.getParameters()) prm->addListener(&counter);
            const auto recipe=panel.roll(true);
            const int notified=counter.totalChanges(),gestures=counter.totalGestures();
            for(auto* prm:rig.p.getParameters()) prm->removeListener(&counter);
            std::cout<<"  into B: "<<recipe.changedValues<<" values changed, host notifications "<<notified<<", gestures "<<gestures<<"\n";
            require(recipe.intoB&&!recipe.rejected&&recipe.changedValues>0,"into-B roll did not change B");
            require(notified==0&&gestures==0,"into B notified the host: "+juce::String(notified)+" changes, "+juce::String(gestures)+" gestures");
            require(!rig.p.isSnapshotB()&&!recipe.onLiveFace,"the user did not stay on A");
            require(sameValues(a,randomValues(rig.p))&&compareUlp(a,randomValues(rig.p)).differing==0,"A changed by Randomize into B");
            require(rig.p.currentPresetName()==name,"into B renamed the live preset: "+rig.p.currentPresetName());
            require(rig.textOf("preset.source").endsWith("(in B)")&&panel.presetLabel().isEmpty(),"B result not shown as B: "+rig.textOf("preset.source")+" / "+panel.presetLabel());
            require(rig.p.randomizer().undoDepth()==1&&panel.undoDepth()==1,"into B did not push one undo step");
            const auto bAfter=rig.p.randomizer().captureSnapshot(aether::rand::Face::B);require(bAfter.value!=bBefore.value,"B did not change");
            // B really holds the roll once the user switches to it
            rig.p.toggleAB();const auto b=randomValues(rig.p);require(!sameValues(a,b),"B is the same as A");
            {int nd=0,worstB=0;float worstAbs=0;juce::String firstBad;for(size_t i=0;i<aether::parameterSpecs().size();++i) {const float av=bAfter.value[i],bv=b.at(aether::parameterSpecs()[i].id);const int u=ulpDistance(av,bv);const float ad=std::abs(av-bv);if(u>0)++nd;worstB=std::max(worstB,u);worstAbs=std::max(worstAbs,ad);if(ad>1.0e-4f&&firstBad.isEmpty()) firstBad=aether::parameterSpecs()[i].id+" snapshot "+juce::String(bAfter.value[i],9)+" live "+juce::String(b.at(aether::parameterSpecs()[i].id),9);}
             std::cout<<"  B face after A/B toggle vs controller snapshot: "<<nd<<" values differ, worst "<<worstB<<" ulp, worst absolute difference "<<worstAbs<<" (the A/B swap goes through normalised host values, so it is not bit-exact)\n";require(worstAbs<=1.0e-4f,"B face differs from the snapshot: "+firstBad);}
            rig.p.toggleAB();require(!rig.p.isSnapshotB(),"toggle back");
            // undo: physical B face restored, A (live) untouched, still no host traffic
            for(auto* prm:rig.p.getParameters()) prm->addListener(&counter);counter.reset();
            require(panel.undo(),"undo of into-B refused");
            const int undoNotified=counter.totalChanges();for(auto* prm:rig.p.getParameters()) prm->removeListener(&counter);
            require(undoNotified==0&&!rig.p.isSnapshotB()&&sameValues(a,randomValues(rig.p)),"undo of into-B disturbed A / the host ("+juce::String(undoNotified)+")");
            const auto bUndone=rig.p.randomizer().captureSnapshot(aether::rand::Face::B);int worst=0;for(size_t i=0;i<bUndone.value.size();++i) worst=std::max(worst,ulpDistance(bUndone.value[i],bBefore.value[i]));
            require(worst<=4,"undo of into-B restored B "+juce::String(worst)+" ulp away");
            // a plain roll while B is live stays on B; an undo made on B restores the A face it came from, not the live one
            RandomRig two;auto& pn=*two.panel;pn.setAmount(.7f);const auto a0=randomValues(two.p);pn.roll(false);const auto a1=randomValues(two.p);
            two.p.toggleAB();const auto bLive=randomValues(two.p);require(two.p.isSnapshotB(),"toggle to B");
            require(pn.undo(),"undo while B is live refused");
            require(two.p.isSnapshotB()&&sameValues(bLive,randomValues(two.p)),"undo while B is live touched the live B");
            two.p.toggleAB();require(compareUlp(a0,randomValues(two.p)).maxUlp<=4,"undo made on B did not restore face A");(void)a1;
        });
        test("Randomize engine: parameterSpecs() and the engine table agree, and the UI group of every id is the engine group",[&] {
            const auto& specs=aether::parameterSpecs();const auto table=aether::rand::paramTable();
            require(int(specs.size())==aether::rand::kParamCount&&aether::rand::kParamCount==495,"parameter count "+juce::String(int(specs.size())));
            for(size_t i=0;i<specs.size();++i) {
                require(specs[i].id==juce::String(table[i].id),"engine table order differs at "+juce::String(int(i))+": "+specs[i].id+" vs "+table[i].id);
                require(aether::ui::randomGroupOf(specs[i].id)==randomGroups[int(table[i].group)]&&randomGroupOf(specs[i].id)==randomGroups[int(table[i].group)],"group of "+specs[i].id);
                require(aether::rand::paramIndex(specs[i].id.toStdString())==int(i),"paramIndex of "+specs[i].id);
            }
        });
        test("Randomize engine: UI locks map to the LockSet, locked and Never parameters never move (every lock combination, Full and Mutate)",[&] {
            const auto table=aether::rand::paramTable();int rolls=0,moved=0,overCount=0;
            for(int state=0;state<2;++state) {
                AetherAudioProcessor p;if(state==1) {randomSourceLive(p);p.setParameterValue("modulation.1.source",1);p.setParameterValue("modulation.1.target",4);p.setParameterValue("modulation.1.depth",.5f);}
                auto backend=aether::ui::makeEngineBackend(p);
                for(int mask=0;mask<32;++mask) for(int full=0;full<2;++full) for(int s=0;s<2;++s) {
                    aether::ui::RandomizeRequest req;req.seed=0xA7F392C1ull+std::uint64_t(mask*131+s*7+state);req.mode=full?aether::ui::RandomizeMode::full:aether::ui::RandomizeMode::mutate;req.amount=.8f;
                    for(int g=0;g<5;++g) if(mask>>g&1) req.lockedGroups.add(randomGroups[g]);   // MixGlobal / System stay unlocked: Never alone must hold them
                    const auto before=randomValues(p);const auto rec=backend->randomize(req);const auto after=randomValues(p);++rolls;
                    require(!rec.rejected,"roll rejected: "+rec.note);int changed=0;
                    for(size_t i=0;i<table.size();++i) {
                        const juce::String id=table[i].id;const bool differs=after.at(id)!=before.at(id);changed+=differs?1:0;
                        if(req.lockedGroups.contains(randomGroups[int(table[i].group)])) require(!differs,"locked "+randomGroups[int(table[i].group)]+" moved: "+id);
                        if(table[i].cls==aether::rand::Classification::Never) require(!differs,"Never parameter moved: "+id);
                    }
                    require(changed<=rec.changedValues&&rec.changedValues-changed<=juce::jmax(4,rec.changedValues/10),"recipe count "+juce::String(rec.changedValues)+" vs "+juce::String(changed));overCount+=rec.changedValues-changed;moved+=changed;
                }
            }
            std::cout<<"  lock combinations: "<<rolls<<" rolls, "<<moved<<" values moved, recipe.changedValues over-counted by "<<overCount<<" in total\n";require(rolls==256&&moved>500,"locks test too weak: rolls "+juce::String(rolls)+" moved "+juce::String(moved));
        });
        test("Randomize engine: the same seed makes the same patch (backend and generate()), another seed another one",[&] {
            for(int full=0;full<2;++full) {
                AetherAudioProcessor a,b;randomSourceLive(a);randomSourceLive(b);auto ba=aether::ui::makeEngineBackend(a);auto bb=aether::ui::makeEngineBackend(b);
                aether::ui::RandomizeRequest req;req.seed=0x1234567890ull&aether::ui::randomSeedMask;req.mode=full?aether::ui::RandomizeMode::full:aether::ui::RandomizeMode::mutate;req.amount=.6f;req.lockedGroups={"MixGlobal","System"};
                const auto ra=ba->randomize(req);const auto rb=bb->randomize(req);const auto va=randomValues(a),vb=randomValues(b);
                require(ra.changedValues>0&&ra.changedValues==rb.changedValues&&ra.attempt==rb.attempt&&ra.algoVersion==aether::rand::kRandAlgoVersion,"same seed, different recipe");
                for(const auto& [id,v]:va) require(v==vb.at(id),"same seed gave different "+id);
                req.seed^=1;AetherAudioProcessor c;randomSourceLive(c);aether::ui::makeEngineBackend(c)->randomize(req);
                bool differs=false;const auto vc=randomValues(c);for(const auto& [id,v]:va) differs|=v!=vc.at(id);require(differs,"another seed gave the same values");
            }
            // generate() itself: bitwise equal diffs for equal input
            AetherAudioProcessor p;randomSourceLive(p);aether::rand::StateView view;
            for(size_t i=0;i<aether::parameterSpecs().size();++i) view.value[i]=p.parameterValue(aether::parameterSpecs()[i].id);
            view.edition={JucePlugin_IsSynth?2:1,JucePlugin_IsSynth!=0};aether::rand::Config cfg;cfg.seed=42;cfg.mode=aether::rand::Mode::Full;aether::rand::LockSet none;
            const auto p1=aether::rand::generate(view,cfg,none),p2=aether::rand::generate(view,cfg,none);
            require(!p1.diffs.empty()&&p1.diffs.size()==p2.diffs.size(),"generate() diff count differs");
            for(size_t i=0;i<p1.diffs.size();++i) require(p1.diffs[i].specIndex==p2.diffs[i].specIndex&&std::memcmp(&p1.diffs[i].value,&p2.diffs[i].value,4)==0,"generate() diff differs at "+juce::String(int(i)));
            require(aether::rand::validate(view,p1,cfg,none).ok,"validate() rejects the engine's own patch");
        });
        test("Randomize engine: a live roll is one replaceState: one notification per changed parameter, no gesture (JUCE level; no DAW)",[&] {
            AetherAudioProcessor p;randomSourceLive(p);HostCounter counter;for(auto* prm:p.getParameters()) prm->addListener(&counter);
            auto backend=aether::ui::makeEngineBackend(p);aether::ui::RandomizeRequest req;req.seed=0x0BADC0DEull;req.mode=aether::ui::RandomizeMode::full;req.lockedGroups={"MixGlobal","System"};
            const auto& specs=aether::parameterSpecs();std::map<juce::String,float> before=randomValues(p);std::map<juce::String,int> indexOf;
            for(auto* prm:p.getParameters()) if(auto* withId=dynamic_cast<juce::AudioProcessorParameterWithID*>(prm)) indexOf[withId->paramID]=prm->getParameterIndex();
            counter.reset();const auto rec=backend->randomize(req);const auto after=randomValues(p);int notified=0,moved=0;
            for(const auto& spec:specs) {
                const int idx=indexOf.at(spec.id);const bool differs=after.at(spec.id)!=before.at(spec.id);
                if(differs) {++moved;require(counter.changes[idx]==1,"changed "+spec.id+" was notified "+juce::String(counter.changes[idx])+" times");}
                else require(counter.changes[idx]==0,"unchanged "+spec.id+" was notified");
                notified+=counter.changes[idx];
            }
            require(counter.totalGestures()==0,"a gesture was sent: "+juce::String(counter.totalGestures()));
            require(rec.changedValues>20&&moved<=rec.changedValues&&rec.changedValues-moved<=8&&notified==moved,"notifications "+juce::String(notified)+" for "+juce::String(rec.changedValues)+" changed values ("+juce::String(moved)+" moved)");
            std::cout<<"  recipe.changedValues "<<rec.changedValues<<" vs parameters whose value really changed "<<moved<<"\n";
            std::cout<<"  live roll: "<<rec.changedValues<<" changed, "<<notified<<" notifications, "<<counter.totalGestures()<<" gestures\n";
            // undo of a live roll is the same path
            counter.reset();require(backend->undo(),"undo refused");const auto afterUndo=randomValues(p);int undoNotified=counter.totalChanges();
            const auto s=compareUlp(afterUndo,before);
            require(counter.totalGestures()==0&&undoNotified>=moved-s.differing&&undoNotified<=moved+4,"undo notifications "+juce::String(undoNotified)+" vs "+juce::String(rec.changedValues)+", gestures "+juce::String(counter.totalGestures()));
            require(s.maxUlp<=4,"undo restored a value "+juce::String(s.maxUlp)+" ulp away");
            std::cout<<"  undo: "<<undoNotified<<" notifications, "<<s.differing<<" values differ from before by <= "<<s.maxUlp<<" ulp\n";
            for(auto* prm:p.getParameters()) prm->removeListener(&counter);
        });
        test("Randomize: locks follow the controller's LockSet in both directions, survive a state save and load, and the roll shows 'edited' after a change",[&] {
            ScopedRandomBackend backend;
            RandomRig a;auto& panel=*a.panel;
            require(a.p.randomizer().locks().params.none()&&panel.lockedGroups().isEmpty(),"fresh locks are not the default");
            panel.setLocked("FM",true);panel.setLocked("MixGlobal",false);
            {const auto set=a.p.randomizer().locks();const auto table=aether::rand::paramTable();
             for(size_t i=0;i<table.size();++i) {const auto g=randomGroups[int(table[i].group)];const bool want=g=="FM";require(set.params.test(i)==want,"LockSet bit of "+juce::String(table[i].id)+" ("+g+") is "+juce::String(int(set.params.test(i))));}
             require((set.sections&aether::rand::kLockFm)!=0&&(set.sections&aether::rand::kLockSource)==0,"LockSet sections");}
            // LockSet written elsewhere (state load, host) shows in the chips on the next tick
            aether::rand::LockSet other;{const auto table=aether::rand::paramTable();for(size_t i=0;i<table.size();++i) if(randomGroups[int(table[i].group)]=="Source") other.params.set(i);other.sections|=aether::rand::kLockSource;}
            a.p.randomizer().setLocks(other);panel.notePreset(a.p.currentPresetName(),false);
            require(panel.lockedGroups()==juce::StringArray({"Source"}),"chips did not follow the LockSet: "+panel.lockedGroups().joinIntoString(","));
            panel.setLocked("Motion",true);panel.setLocked("Source",true);panel.setLocked("Chain",false);panel.setSeedText("A7F3-92C1");panel.setPinned(true);panel.roll(false);
            const auto rolled=a.p.randomizer().recipe();require(rolled.valid&&rolled.seed==0x51DE348981ull,"recipe not recorded (seed "+juce::String(std::int64_t(rolled.seed))+")");
            juce::MemoryBlock saved;a.p.getStateInformation(saved);
            RandomRig b;require(b.panel->lockedGroups().isEmpty(),"second rig does not start with the default");
            b.p.setStateInformation(saved.getData(),int(saved.getSize()));b.panel->notePreset(b.p.currentPresetName(),b.p.isSnapshotB());
            auto sorted=[](juce::StringArray s){s.sort(false);return s;};
            require(sorted(b.panel->lockedGroups())==sorted(panel.lockedGroups()),"locks lost by save/load: "+b.panel->lockedGroups().joinIntoString(",")+" vs "+panel.lockedGroups().joinIntoString(","));
            require(b.p.randomizer().recipe().valid&&b.p.randomizer().recipe().seed==rolled.seed&&b.panel->presetLabel()=="(Random) A7F3-92C1","recipe or label lost by save/load: '"+b.panel->presetLabel()+"'");
            require(b.p.randomizer().undoDepth()==0&&b.panel->undoDepth()==0,"a loaded state kept an undo history");
            require(b.p.randomizer().recipe().resultHash!=0&&aether::rand::hashValues(b.p.randomizer().captureSnapshot(aether::rand::Face::A).value)==b.p.randomizer().recipe().resultHash,"loaded values do not match the saved recipe hash");
            require(!a.textOf("last").endsWith(", edited"),"roll shows edited before any change: "+a.textOf("last"));
            a.p.setParameterValue("source.gain",a.p.parameterValue("source.gain")>.5f?.2f:.8f);a.panel->notePreset(a.p.currentPresetName(),false);
            require(a.textOf("last").endsWith(", edited"),"edited mark missing: "+a.textOf("last"));
        });
        test("Randomize engine: generate() runs on the message thread only (a call from another thread is refused and writes nothing)",[&] {
            AetherAudioProcessor p;randomSourceLive(p);auto backend=aether::ui::makeEngineBackend(p);aether::ui::RandomizeRequest req;req.seed=5;req.mode=aether::ui::RandomizeMode::full;
            const auto before=randomValues(p);aether::ui::RandomizeRecipe rec,recB;bool undone=true;
            std::thread worker([&] {rec=backend->randomize(req);recB=backend->randomizeIntoB(req);undone=backend->undo();});worker.join();
            require(rec.rejected&&recB.rejected&&rec.note=="wrong thread"&&!undone&&backend->undoDepth()==0&&sameValues(randomValues(p),before)&&!p.isSnapshotB(),"worker thread call was not refused");
            require(!backend->randomize(req).rejected&&backend->undoDepth()==1,"owner thread roll refused after the worker attempt");
        });
        test("Randomize header buttons and popover: inside the canvas, no overlap, text fits and keeps 4.5:1 contrast (both editions, all zoom / size cases)",[&] {
            ScopedRandomBackend backend;
            for(int edition=0;edition<2;++edition) {
                ScopedEdition editionGuard(edition);const juce::String edName=edition==1?"synth":"micro";
                RandomRig rig;auto& panel=*rig.panel;auto* editor=rig.editor;auto* content=rig.content;
                struct Case{const char* zoom;std::vector<std::pair<int,int>> sizes;};
                const std::vector<Case> cases={{"zoom.50",{{720,450},{800,450},{720,620},{1000,625},{1030,640},{1100,750}}},{"zoom.75",{{885,585},{1080,675},{1500,900}}},{"zoom.100",{{1180,780},{1440,900},{2200,1500}}}};
                // states: 0 fresh, 1 rolled, 2 rolled into B with more locks and Full mode, 3 all locked
                for(int state=0;state<4;++state) {
                    for(const auto& g:randomGroups) panel.setLocked(g,state==3||(state==2&&g!="Source")||g=="MixGlobal"||g=="System");
                    panel.setMode(state==2?aether::ui::RandomizeMode::full:aether::ui::RandomizeMode::mutate);panel.setPinned(true);panel.setSeedText("ZZZZ-ZZZZ");
                    rig.p.setParameterValue("global.mode",edition==1?2.f:(state%2==0?1.f:0.f));
                    if(state==0){while(panel.undo()) {}panel.clearRecipe();} else panel.roll(state==2);
                    for(const auto& testCase:cases) {
                        require(editor->performUiAction(testCase.zoom),"zoom action missing");
                        for(const auto& size:testCase.sizes) for(bool open:{false,true}) {
                            editor->setSize(size.first,size.second);require(editor->performUiAction(open?"random.open":"random.close"),"random open/close action missing");
                            const juce::String where=edName+" state "+juce::String(state)+(open?" open ":" closed ")+testCase.zoom+" "+juce::String(size.first)+"x"+juce::String(size.second);
                            require(panel.isOpen()==open&&panel.isVisible()==open,"popover visibility: "+where);
                            // header buttons: inside the canvas and clear of every other control (the popover itself floats above the page)
                            std::vector<juce::Component*> visible;
                            for(auto* child:content->getChildren()) if(child->isVisible()&&!child->getBounds().isEmpty()&&child!=&panel&&dynamic_cast<juce::TooltipWindow*>(child)==nullptr) {
                                require(content->getLocalBounds().contains(child->getBounds()),"control outside the canvas: "+child->getComponentID()+" "+where);visible.push_back(child);
                            }
                            auto* roll=findShown<juce::TextButton>(*content,"random.header.roll");auto* caret=findShown<juce::Button>(*content,"random.header.menu");
                            require(roll!=nullptr&&caret!=nullptr,"header Randomize buttons missing: "+where);
                            for(auto* hb:{static_cast<juce::Component*>(roll),static_cast<juce::Component*>(caret)}) for(auto* other:visible) if(other!=hb)
                                require(!hb->getBounds().intersects(other->getBounds()),"header button "+hb->getComponentID()+" overlaps "+other->getComponentID()+": "+where);
                            const auto font=roll->getLookAndFeel().getTextButtonFont(*roll,28);
                            require(juce::GlyphArrangement::getStringWidthInt(font,roll->getButtonText())<=roll->getWidth()-10,"header button text truncated ("+roll->getButtonText()+" in "+juce::String(roll->getWidth())+" px): "+where);
                            require(contrast(roll->findColour(juce::TextButton::textColourOffId),roll->findColour(juce::TextButton::buttonColourId))>=4.5,"header button contrast: "+where);
                            if(!open) continue;
                            // popover: inside the canvas, children inside and apart
                            require(content->getLocalBounds().contains(panel.getBounds()),"popover outside the canvas: "+where);
                            std::vector<juce::Component*> kids;for(auto* k:panel.getChildren()) if(k->isVisible()&&!k->getBounds().isEmpty()) {require(panel.getLocalBounds().contains(k->getBounds()),"popover control outside: "+k->getComponentID()+" "+where);kids.push_back(k);}
                            require(kids.size()>=14,"popover lost controls: "+juce::String(int(kids.size()))+" "+where);
                            for(size_t i=0;i<kids.size();++i) for(size_t j=i+1;j<kids.size();++j) require(!kids[i]->getBounds().intersects(kids[j]->getBounds()),"popover controls overlap: "+kids[i]->getComponentID()+" / "+kids[j]->getComponentID()+" "+where);
                            // texts: fit their area at the real font, stay inside, stay apart, 4.5:1 against their own background
                            const auto items=editor->randomizeTexts();require(items.size()>=20,"popover texts missing: "+where);
                            for(const auto& t:items) {
                                if(t.text.isEmpty()) continue;
                                const juce::String here=t.id+" \""+t.text+"\" "+where;
                                require(t.size*(content->getTransform().getScaleFactor())>=9.99f,"text below 10 effective px: "+here);
                                require(panel.getLocalBounds().contains(t.area),"text area outside the popover: "+here);
                                if(!t.ellipsis) require(juce::GlyphArrangement::getStringWidthInt(font.withHeight(t.size),t.text)<=t.area.getWidth(),"text truncated: "+here);
                                require(t.area.getHeight()>=int(t.size)+2,"text area too low: "+here);
                                require(contrast(t.colour,t.background)>=4.5,"contrast "+juce::String(contrast(t.colour,t.background),2)+" < 4.5: "+here);
                                requireEditionWording(edition,t.text,here);
                            }
                            for(size_t i=0;i<items.size();++i) for(size_t j=i+1;j<items.size();++j) {
                                if(items[i].text.isEmpty()||items[j].text.isEmpty()) continue;
                                require(!items[i].area.intersects(items[j].area),"popover texts overlap: "+items[i].id+" / "+items[j].id+" "+where);
                            }
                            require(rig.textOf("hint").isNotEmpty(),"mode hint missing: "+where);
                        }
                    }
                }
                editor->performUiAction("zoom.100");editor->performUiAction("random.close");
            }
        });
        std::cout<<"RESULT "<<passed<<" tests passed, "<<failed<<" failed, "<<skipped<<" skipped\n"; return failed?1:0;
    } catch(const std::exception& error) { std::cerr<<"FAIL "<<error.what()<<'\n'; return 1; }
}
