// 2.2.0 Resynth + Source and plugin-type mode split: behavioural tests (written BEFORE the implementation).
//
// Built twice from this one file, because JucePlugin_IsSynth is a compile-time macro:
//   AetherResynthSplitFx     links the effect plugin  "AETHER Micro"       (JucePlugin_IsSynth == 0)
//   AetherResynthSplitSynth  links the synth plugin   "AETHER Micro Synth" (JucePlugin_IsSynth == 1)
// Each executable first checks that its own macro matches AETHER_EXPECT_SYNTH, so a mis-wired target fails loudly.
//
// Mode values (global.mode, choice list is frozen for state compatibility): 0 = FX, 1 = Resynth, 2 = Synth.
//
// Specification under test (user decision 2026-10-25):
//   * effect plugin:  Synth(2) is not available -> a stored/requested 2 behaves exactly as FX(0); 0 and 1 are kept.
//   * synth plugin:   only Synth exists        -> a stored/requested 0 or 1 behaves exactly as Synth(2).
//   * Resynth + Source (engine > 0, source.enabled): the Source engine is driven by the input audio (pitch + envelope).
//   * FX mode never renders the Source; Synth mode is unchanged.
// "Behaves exactly as" is asserted as a bit-identical render of the same processor setup with the mode written explicitly,
// so the tests do not depend on how the implementation clamps (in the audio path, or by rewriting the parameter).
//
// Groups (command line  --group unchanged|split|all , default all):
//   unchanged  behaviour that must be identical before and after the 2.2.0 change (must be green on the 2.1.0 baseline).
//   split      new behaviour (expected RED on the 2.1.0 baseline, GREEN after the implementation lands).
// Other modes:
//   --generate-golden <file>   write the golden summary for this plugin form (used once, on the 2.1.0 baseline)
//   --list                     list test names with their group
#include "PluginProcessor.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef AETHER_EXPECT_SYNTH
#error "AETHER_EXPECT_SYNTH must be defined by the build (0 = effect plugin, 1 = synth plugin)"
#endif
#ifndef AETHER_SOURCE_DIR
#error "AETHER_SOURCE_DIR must be defined by the build"
#endif

namespace {
using Processor=AetherAudioProcessor;
constexpr bool kSynth=(JucePlugin_IsSynth!=0);
constexpr double kRate=48000.0;
constexpr int kFx=0,kResynth=1,kSynthMode=2;
constexpr double kBlockSizeTol=1.0e-5;      // same tolerance as tests/FixtureTests.cpp
constexpr double kStatAbsTol=1.0e-6,kStatRelTol=1.0e-4;

int passed=0,failed=0;
std::string selectedGroup="all";
void require(bool ok,const std::string& why) { if(!ok) throw std::runtime_error(why); }
template<class F> void test(const char* group,const char* name,F f) {
    if(selectedGroup=="list") { std::cout<<group<<'\t'<<name<<'\n'; return; }
    if(selectedGroup!="all" && selectedGroup!=group) return;
    try { f(); ++passed; std::cout<<"PASS ["<<group<<"] "<<name<<std::endl; }
    catch(const std::exception& error) { ++failed; std::cout<<"FAIL ["<<group<<"] "<<name<<": "<<error.what()<<std::endl; }
}
std::string num(double v) { char b[48]; std::snprintf(b,sizeof b,"%.6g",v); return b; }

// ---- audio helpers --------------------------------------------------------
struct Audio { std::vector<float> l,r; };
struct MidiEvent { int at; juce::MidiMessage message; };
using Setup=std::function<void(Processor&)>;

Audio silence(int frames) { Audio a; a.l.assign(static_cast<size_t>(frames),0.f); a.r=a.l; return a; }
// Same character as the fixture test input: two sines plus a little fixed-seed noise.
Audio noiseSine(int frames) {
    Audio a=silence(frames); juce::Random rng(20261025);
    for(int i=0;i<frames;++i) {
        const double t=i/kRate;
        a.l[static_cast<size_t>(i)]=static_cast<float>(0.20*std::sin(2*juce::MathConstants<double>::pi*220*t)+0.05*(rng.nextDouble()*2-1));
        a.r[static_cast<size_t>(i)]=static_cast<float>(0.20*std::sin(2*juce::MathConstants<double>::pi*330*t)+0.05*(rng.nextDouble()*2-1));
    }
    return a;
}
// Sine with a 10 ms linear attack at `start` and an ABRUPT cut at `end` (worst case for clicks); silence elsewhere.
Audio tone(int frames,double hz,double amplitude,int start,int end) {
    Audio a=silence(frames);
    for(int i=start;i<std::min(end,frames);++i) {
        const double attack=std::min(1.0,(i-start)/(0.010*kRate));
        const float v=static_cast<float>(amplitude*attack*std::sin(2*juce::MathConstants<double>::pi*hz*(i-start)/kRate));
        a.l[static_cast<size_t>(i)]=v; a.r[static_cast<size_t>(i)]=v;
    }
    return a;
}
const std::vector<MidiEvent>& noMidi() { static const std::vector<MidiEvent> none; return none; }
const std::vector<MidiEvent>& notes() {
    static const std::vector<MidiEvent> script{
        {1000,juce::MidiMessage::noteOn(1,60,0.8f)},{12000,juce::MidiMessage::noteOn(1,67,0.6f)},
        {30000,juce::MidiMessage::noteOff(1,60)},{40000,juce::MidiMessage::noteOff(1,67)}};
    return script;
}
Audio render(const Setup& setup,const Audio& in,const std::vector<MidiEvent>& midiScript,int block=256) {
    Processor p; p.prepareToPlay(kRate,block); setup(p);
    const int frames=static_cast<int>(in.l.size());
    Audio out=silence(frames);
    for(int offset=0;offset<frames;offset+=block) {
        const int n=std::min(block,frames-offset);
        juce::AudioBuffer<float> buffer(2,n); juce::MidiBuffer midi;
        std::copy_n(in.l.data()+offset,n,buffer.getWritePointer(0)); std::copy_n(in.r.data()+offset,n,buffer.getWritePointer(1));
        for(const auto& event:midiScript) if(event.at>=offset && event.at<offset+n) midi.addEvent(event.message,event.at-offset);
        p.processBlock(buffer,midi);
        std::copy_n(buffer.getReadPointer(0),n,out.l.data()+offset); std::copy_n(buffer.getReadPointer(1),n,out.r.data()+offset);
    }
    return out;
}
bool finiteAll(const Audio& a) { for(size_t i=0;i<a.l.size();++i) if(!std::isfinite(a.l[i])||!std::isfinite(a.r[i])) return false; return true; }
double maxAbsDiff(const Audio& a,const Audio& b) {
    double d=0; for(size_t i=0;i<a.l.size();++i) d=std::max({d,double(std::abs(a.l[i]-b.l[i])),double(std::abs(a.r[i]-b.r[i]))}); return d;
}
double rms(const Audio& a,int from,int to) {
    double e=0; for(int i=from;i<to;++i) e+=double(a.l[static_cast<size_t>(i)])*a.l[static_cast<size_t>(i)]+double(a.r[static_cast<size_t>(i)])*a.r[static_cast<size_t>(i)];
    return std::sqrt(e/(2.0*std::max(1,to-from)));
}
double rmsDiff(const Audio& a,const Audio& b,int from,int to) {
    double e=0; for(int i=from;i<to;++i) { const double dl=double(a.l[static_cast<size_t>(i)])-b.l[static_cast<size_t>(i)],dr=double(a.r[static_cast<size_t>(i)])-b.r[static_cast<size_t>(i)]; e+=dl*dl+dr*dr; }
    return std::sqrt(e/(2.0*std::max(1,to-from)));
}
double peak(const Audio& a,int from,int to) {
    double m=0; for(int i=from;i<to;++i) m=std::max({m,double(std::abs(a.l[static_cast<size_t>(i)])),double(std::abs(a.r[static_cast<size_t>(i)]))}); return m;
}
// Largest adjacent-sample difference (both channels) with index in (from, to].
double maxStep(const Audio& a,int from,int to) {
    double m=0; for(int i=std::max(1,from+1);i<=to && i<static_cast<int>(a.l.size());++i)
        m=std::max({m,double(std::abs(a.l[static_cast<size_t>(i)]-a.l[static_cast<size_t>(i-1)])),double(std::abs(a.r[static_cast<size_t>(i)]-a.r[static_cast<size_t>(i-1)]))});
    return m;
}
// Fundamental via normalised autocorrelation of the mono mix: first local maximum within 90% of the global one (60 Hz..1 kHz).
double estimateHz(const Audio& a,int from,int to) {
    const int n=to-from; std::vector<double> x(static_cast<size_t>(n)); double mean=0;
    for(int i=0;i<n;++i) { x[static_cast<size_t>(i)]=0.5*(double(a.l[static_cast<size_t>(from+i)])+a.r[static_cast<size_t>(from+i)]); mean+=x[static_cast<size_t>(i)]; }
    mean/=n; for(auto& v:x) v-=mean;
    const int minLag=static_cast<int>(kRate/1000),maxLag=static_cast<int>(kRate/60);
    double e0=0; for(int i=0;i<n-maxLag;++i) e0+=x[static_cast<size_t>(i)]*x[static_cast<size_t>(i)];
    if(e0<=1.0e-12) return 0;
    std::vector<double> r(static_cast<size_t>(maxLag+2),0.0); double best=0;
    for(int lag=minLag;lag<=maxLag;++lag) { double s=0; for(int i=0;i<n-maxLag;++i) s+=x[static_cast<size_t>(i)]*x[static_cast<size_t>(i+lag)]; r[static_cast<size_t>(lag)]=s/e0; best=std::max(best,r[static_cast<size_t>(lag)]); }
    if(best<0.3) return 0;
    for(int lag=minLag+1;lag<maxLag;++lag)
        if(r[static_cast<size_t>(lag)]>=0.9*best && r[static_cast<size_t>(lag)]>=r[static_cast<size_t>(lag-1)] && r[static_cast<size_t>(lag)]>=r[static_cast<size_t>(lag+1)]) return kRate/lag;
    return 0;
}

// ---- parameter / scenario helpers ----------------------------------------
void put(Processor& p,const char* id,float v) { p.setParameterValue(id,v); }
Setup withMode(const Setup& scenario,int mode) { return [scenario,mode](Processor& p) { scenario(p); put(p,"global.mode",static_cast<float>(mode)); }; }
struct Scenario { std::string name; Setup setup; };
// Signal-path configurations that exercise every renderer the mode switch can reach.
std::vector<Scenario> scenarios() {
    return {
        {"legacy chain (graph off, FM off, source off)",[](Processor& p) { put(p,"graph.enabled",0); put(p,"fm.enabled",0); }},
        {"FM engine only (graph off)",[](Processor& p) { put(p,"graph.enabled",0); put(p,"fm.enabled",1); }},
        {"Atlas graph + FM",[](Processor& p) { put(p,"graph.enabled",1); put(p,"fm.enabled",1); }},
        {"Phase Geometry source (graph off, FM off)",[](Processor& p) { put(p,"graph.enabled",0); put(p,"fm.enabled",0); put(p,"source.engine",1); put(p,"source.enabled",1); }},
        {"Chaos source + FM (graph off)",[](Processor& p) { put(p,"graph.enabled",0); put(p,"fm.enabled",1); put(p,"source.engine",2); put(p,"source.enabled",1); }},
    };
}
// Resynth + Source: only the Source engine drives the output (no FM, no graph, fully wet).
Setup resynthSource(int engine,bool sourceOn=true) {
    return [engine,sourceOn](Processor& p) {
        put(p,"graph.enabled",0); put(p,"fm.enabled",0); put(p,"global.mix",1);
        put(p,"source.engine",static_cast<float>(engine)); put(p,"source.enabled",sourceOn?1.f:0.f);
        put(p,"global.mode",kResynth);
    };
}

juce::File fixtureDir() { return juce::File(AETHER_SOURCE_DIR).getChildFile("tests").getChildFile("fixtures"); }
struct Fixture { std::string label; juce::File file; bool preset; };
Fixture stateFixture(const char* name) { return {std::string("state ")+name,fixtureDir().getChildFile("state-v2.0.1").getChildFile(juce::String(name)+".bin"),false}; }
Fixture presetFixture(const char* name) { return {std::string("preset ")+name,fixtureDir().getChildFile("presets-v1.0.0").getChildFile(juce::String(name)+".aether"),true}; }
void loadFixture(Processor& p,const Fixture& f) {
    if(f.preset) { const auto r=p.loadPreset(f.file); require(r.wasOk(),"preset load failed: "+f.label+": "+r.getErrorMessage().toStdString()); return; }
    juce::MemoryBlock data; require(f.file.loadFileAsData(data)&&data.getSize()>0,"cannot read "+f.label);
    p.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
}
// global.mode as stored in the file itself (guards against fixture drift: the tests below assume these values).
int storedMode(const Fixture& f) {
    if(f.preset) { juce::var root; require(juce::JSON::parse(f.file.loadFileAsString(),root).wasOk(),"preset JSON invalid"); return static_cast<int>(root["parameters"]["global.mode"]); }
    juce::MemoryBlock data; require(f.file.loadFileAsData(data),"cannot read "+f.label);
    const auto xml=juce::AudioProcessor::getXmlFromBinary(data.getData(),static_cast<int>(data.getSize())); require(xml!=nullptr,"state not decodable");
    const auto tree=juce::ValueTree::fromXml(*xml); const auto child=tree.getChildWithProperty("id","global.mode");
    require(child.isValid(),"state has no global.mode"); return static_cast<int>(static_cast<double>(child.getProperty("value")));
}

constexpr int kScenarioFrames=72000;       // 1.5 s
constexpr int kToneFrames=96000;           // 2.0 s
constexpr int kToneOn=12000,kToneOff=60000; // input tone 0.25 s .. 1.25 s

// ---- golden ---------------------------------------------------------------
constexpr int kGoldenFrames=48000,kGoldenChunk=1920,kGoldenBlock=256;
struct Stats { std::vector<double> rmsL,rmsR,peakL,peakR; std::string sha; };
Stats analyse(const Audio& a) {
    Stats s;
    auto lane=[&](const std::vector<float>& x,std::vector<double>& rmsOut,std::vector<double>& peakOut) {
        for(int c=0;c<kGoldenFrames/kGoldenChunk;++c) { double sum=0,pk=0; for(int i=c*kGoldenChunk;i<(c+1)*kGoldenChunk;++i) { const double v=x[static_cast<size_t>(i)]; sum+=v*v; pk=std::max(pk,std::abs(v)); } rmsOut.push_back(std::sqrt(sum/kGoldenChunk)); peakOut.push_back(pk); }
    };
    lane(a.l,s.rmsL,s.peakL); lane(a.r,s.rmsR,s.peakR);
    juce::MemoryBlock bytes; bytes.append(a.l.data(),a.l.size()*sizeof(float)); bytes.append(a.r.data(),a.r.size()*sizeof(float));
    s.sha=juce::SHA256(bytes.getData(),bytes.getSize()).toHexString().toStdString(); return s;
}
// Golden samples for THIS plugin form. The mode is written explicitly after the preset is loaded, so the golden describes
// the mode semantics that exist both before and after the split:  synth form -> Synth(2);  effect form -> FX(0) and Resynth(1)
// with the Source off (factory presets leave source.engine at Off).
struct GoldenCase { std::string id; int preset; int mode; };
std::vector<GoldenCase> goldenCases() {
    std::vector<GoldenCase> cases; const int count=Processor::factoryPresetNames().size();
    for(int k=0;k<count;++k) {
        const auto tag="preset-"+std::string(k<10?"0":"")+std::to_string(k);
        if(kSynth) cases.push_back({"synth/"+tag,k,kSynthMode});
        else { cases.push_back({"micro-fx/"+tag,k,kFx}); cases.push_back({"micro-resynth/"+tag,k,kResynth}); }
    }
    return cases;
}
Audio goldenRender(const GoldenCase& c) {
    return render([c](Processor& p) { p.loadFactoryPreset(c.preset); put(p,"global.mode",static_cast<float>(c.mode)); },noiseSine(kGoldenFrames),notes(),kGoldenBlock);
}
std::string numbers(const std::vector<double>& v) {
    std::string out="[";
    for(size_t i=0;i<v.size();++i) { char b[40]; std::snprintf(b,sizeof b,"%.9g",v[i]); out+=(i?", ":"")+std::string(b); }
    return out+"]";
}
void writeGolden(const juce::File& file) {
    const auto cases=goldenCases();
    std::string text="{\n  \"generator\": \"AetherResynthSplit"+std::string(kSynth?"Synth":"Fx")+" --generate-golden\",\n  \"form\": \""+(kSynth?"synth":"micro")+"\",\n  \"codeVersion\": \""+JucePlugin_VersionString+"\",\n";
    text+="  \"sampleRate\": 48000,\n  \"frames\": "+std::to_string(kGoldenFrames)+",\n  \"chunk\": "+std::to_string(kGoldenChunk)+",\n  \"block\": "+std::to_string(kGoldenBlock)+",\n  \"samples\": {\n";
    for(size_t k=0;k<cases.size();++k) {
        const auto a=goldenRender(cases[k]); require(finiteAll(a),"non-finite while generating "+cases[k].id); const auto s=analyse(a);
        text+="    \""+cases[k].id+"\": {\n      \"sha256\": \""+s.sha+"\",\n      \"rmsL\": "+numbers(s.rmsL)+",\n      \"rmsR\": "+numbers(s.rmsR)+",\n      \"peakL\": "+numbers(s.peakL)+",\n      \"peakR\": "+numbers(s.peakR)+"\n    }"+(k+1<cases.size()?",":"")+"\n";
        std::cout<<"golden "<<cases[k].id<<" sha256="<<s.sha<<std::endl;
    }
    text+="  }\n}\n";
    require(file.getParentDirectory().createDirectory().wasOk(),"cannot create golden directory");
    require(file.replaceWithData(text.data(),text.size()),"cannot write golden file");
}
std::vector<double> numberArray(const juce::var& v) { std::vector<double> out; if(auto* array=v.getArray()) for(const auto& item:*array) out.push_back(static_cast<double>(item)); return out; }
bool close(double a,double b) { return std::abs(a-b)<=kStatAbsTol+kStatRelTol*std::max(std::abs(a),std::abs(b)); }
juce::File goldenFile() { return fixtureDir().getChildFile("golden").getChildFile(kSynth?"render-split-synth-48000.json":"render-split-micro-48000.json"); }

// ---- shared body: "this mode request must sound exactly like that mode request" -------------------------------------------
// Returns "" when the two setups render bit-identically, otherwise a one-line description. Loops collect every problem
// (collect) instead of stopping at the first one, so a red run lists all failing scenarios.
std::string sameRenderProblem(const std::string& what,const Setup& actual,const Setup& expected,const Audio& in,const std::vector<MidiEvent>& midi) {
    const auto a=render(actual,in,midi),b=render(expected,in,midi);
    if(!finiteAll(a)||!finiteAll(b)) return what+": non-finite output";
    const double d=maxAbsDiff(a,b);
    if(d==0.0) return {};
    return what+": renders differ, max |diff| "+num(d)+" (rms A "+num(rms(a,0,static_cast<int>(a.l.size())))+", rms B "+num(rms(b,0,static_cast<int>(b.l.size())))+")";
}
void collect(std::string& bad,const std::string& problem) { if(!problem.empty()) bad+="\n      - "+problem; }
void requireSameRender(const std::string& what,const Setup& actual,const Setup& expected,const Audio& in,const std::vector<MidiEvent>& midi) {
    const auto m=sameRenderProblem(what,actual,expected,in,midi); require(m.empty(),m);
}
}

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args; for(int i=1;i<argc;++i) args.add(argv[i]);
    try {
        for(int i=0;i<args.size();++i) {
            if(args[i]=="--group" && i+1<args.size()) selectedGroup=args[i+1].toStdString();
            if(args[i]=="--list") selectedGroup="list";
            if(args[i]=="--generate-golden" && i+1<args.size()) { writeGolden(juce::File(args[i+1])); return 0; }
        }
        std::cout<<"plugin form: "<<(kSynth?"AETHER Micro Synth (JucePlugin_IsSynth=1)":"AETHER Micro (effect, JucePlugin_IsSynth=0)")<<", group "<<selectedGroup<<std::endl;

        // =============================== group: unchanged ===============================
        test("unchanged","this executable really is the expected plugin form",[] {
            require((AETHER_EXPECT_SYNTH!=0)==kSynth,"AETHER_EXPECT_SYNTH does not match JucePlugin_IsSynth: target is mis-wired");
            Processor p; const auto inputs=p.getBusCount(true);
            if(kSynth) require(inputs==0,"synth form must have no input bus, has "+std::to_string(inputs));
            else require(inputs>=1,"effect form must have an input bus");
            require(juce::String(JucePlugin_Name).contains(kSynth?"Synth":"Micro"),"unexpected plugin name");
            if(!kSynth) require(!juce::String(JucePlugin_Name).contains("Synth"),"effect form carries the Synth name");
        });
        test("unchanged","global.mode keeps its option list and default (state compatibility)",[] {
            const auto& specs=aether::parameterSpecs(); const auto it=std::find_if(specs.begin(),specs.end(),[](const auto& s) { return juce::String(s.id)=="global.mode"; });
            require(it!=specs.end(),"global.mode missing");
            require(it->minimum==0.f && it->maximum==2.f,"global.mode range changed: "+num(it->minimum)+".."+num(it->maximum));
            Processor holder; auto* choice=dynamic_cast<juce::AudioParameterChoice*>(holder.state.getParameter("global.mode"));
            require(choice!=nullptr && choice->choices.size()==3,"global.mode is not a 3-item choice");
            require(choice->choices[0]=="FX" && choice->choices[1]=="Resynth" && choice->choices[2]=="Synth","global.mode option names/order changed");
            Processor p; require(static_cast<int>(p.parameterValue("global.mode"))==(kSynth?kSynthMode:kFx),"default global.mode of this form changed");
        });
        test("unchanged","old fixtures (v1.0.0 presets, v2.0.1 states) load and render finite in this form",[] {
            const Audio in=noiseSine(24000);
            std::vector<Fixture> all;
            for(const auto& f:fixtureDir().getChildFile("presets-v1.0.0").findChildFiles(juce::File::findFiles,false,"*.aether")) all.push_back({"preset "+f.getFileNameWithoutExtension().toStdString(),f,true});
            for(const auto& f:fixtureDir().getChildFile("state-v2.0.1").findChildFiles(juce::File::findFiles,false,"*.bin")) all.push_back({"state "+f.getFileNameWithoutExtension().toStdString(),f,false});
            require(all.size()>=21,"fixture inventory shrank: "+std::to_string(all.size()));
            for(const auto& f:all) require(finiteAll(render([&](Processor& p) { loadFixture(p,f); },in,notes())),"non-finite render of "+f.label);
        });
        test("unchanged","render is deterministic and block-size independent in this form (FM+graph, Phase Geometry, Chaos)",[] {
            const auto all=scenarios(); const Audio in=noiseSine(kScenarioFrames);
            const int mode=kSynth?kSynthMode:kFx;
            for(const size_t index:{size_t(2),size_t(3),size_t(4)}) {
                const auto setup=withMode(all[index].setup,mode);
                const auto a=render(setup,in,notes(),256),b=render(setup,in,notes(),256);
                require(finiteAll(a),all[index].name+": non-finite");
                require(maxAbsDiff(a,b)==0.0,all[index].name+": two identical renders differ by "+num(maxAbsDiff(a,b)));
                for(const int block:{17,257,1000}) { const double d=maxAbsDiff(a,render(setup,in,notes(),block)); require(d<=kBlockSizeTol,all[index].name+": block "+std::to_string(block)+" vs 256 differs by "+num(d)); }
            }
        });
        test("unchanged",kSynth?"synth form, mode 2: FM and both Source engines stay audible and finite":"effect form, mode 0: Source on never changes the render (FX has no Source)",[] {
            const Audio in=noiseSine(kScenarioFrames);
            if(kSynth) {
                for(const size_t index:{size_t(1),size_t(3),size_t(4)}) {
                    const auto sc=scenarios()[index]; const auto a=render(withMode(sc.setup,kSynthMode),in,notes());
                    require(finiteAll(a),sc.name+": non-finite");
                    require(rms(a,12000,50000)>0.005,sc.name+": Synth render is silent/too quiet, rms "+num(rms(a,12000,50000)));
                }
                // Source layer is a real contribution in Synth mode (unchanged feature).
                const auto sc=scenarios();
                const auto withSource=render(withMode(sc[3].setup,kSynthMode),in,notes()),without=render(withMode([&](Processor& p) { sc[3].setup(p); put(p,"source.enabled",0); },kSynthMode),in,notes());
                require(maxAbsDiff(withSource,without)>1.0e-3,"Source on/off made no difference in Synth mode");
            } else {
                for(const auto& sc:scenarios()) {
                    if(sc.name.find("source")==std::string::npos) continue;
                    Setup off=[&](Processor& p) { sc.setup(p); put(p,"source.enabled",0); };
                    requireSameRender("FX, "+sc.name+": Source on vs off",withMode(sc.setup,kFx),withMode(off,kFx),in,notes());
                }
                // Source disabled in Resynth: engine choice alone changes nothing.
                for(const int engine:{1,2}) {
                    Setup base=[](Processor& p) { put(p,"graph.enabled",0); put(p,"fm.enabled",1); };
                    Setup disabled=[=](Processor& p) { base(p); put(p,"source.engine",static_cast<float>(engine)); put(p,"source.enabled",0); };
                    requireSameRender("Resynth, engine "+std::to_string(engine)+" but source.enabled=0 vs engine Off",withMode(disabled,kResynth),withMode(base,kResynth),in,noMidi());
                }
            }
        });
        if(!kSynth) test("unchanged","effect form: Resynth (1) is still different from FX (0) with audio input (modes 0 and 1 are both kept)",[] {
            const Audio in=noiseSine(kScenarioFrames); const auto all=scenarios();
            for(const size_t index:{size_t(1),size_t(2)}) {
                const auto fx=render(withMode(all[index].setup,kFx),in,noMidi()),re=render(withMode(all[index].setup,kResynth),in,noMidi());
                require(finiteAll(fx)&&finiteAll(re),all[index].name+": non-finite");
                require(maxAbsDiff(fx,re)>1.0e-3,all[index].name+": mode 1 renders like mode 0 - Resynth was lost (max diff "+num(maxAbsDiff(fx,re))+")");
            }
        });
        test("unchanged","golden: the 36 factory presets render as before in this form (tests/fixtures/golden/render-split-*.json)",[] {
            juce::var root; const auto file=goldenFile(); require(file.existsAsFile(),"golden missing: "+file.getFullPathName().toStdString());
            require(juce::JSON::parse(file.loadFileAsString(),root).wasOk(),"golden JSON invalid");
            auto* table=root["samples"].getDynamicObject(); require(table!=nullptr,"golden has no samples");
            require(static_cast<int>(root["frames"])==kGoldenFrames && static_cast<int>(root["chunk"])==kGoldenChunk,"golden geometry differs from the test");
            const auto cases=goldenCases(); require(table->getProperties().size()==static_cast<int>(cases.size()),"golden sample count "+std::to_string(table->getProperties().size())+" != "+std::to_string(cases.size()));
            std::string bad; int exact=0;
            for(const auto& c:cases) {
                const auto entry=table->getProperty(juce::Identifier(c.id)); require(!entry.isVoid(),"golden lacks "+c.id);
                const auto s=analyse(goldenRender(c)); if(s.sha==entry["sha256"].toString().toStdString()) ++exact;
                const std::pair<const char*,const std::vector<double>*> lanes[]={{"rmsL",&s.rmsL},{"rmsR",&s.rmsR},{"peakL",&s.peakL},{"peakR",&s.peakR}};
                for(const auto& lane:lanes) {
                    const auto want=numberArray(entry[juce::Identifier(lane.first)]);
                    if(want.size()!=lane.second->size()) { bad+=c.id+" "+lane.first+" length; "; continue; }
                    for(size_t i=0;i<want.size();++i) if(!close(want[i],(*lane.second)[i])) { bad+=c.id+" "+lane.first+"["+std::to_string(i)+"] golden "+num(want[i])+" now "+num((*lane.second)[i])+"; "; break; }
                }
            }
            std::cout<<"  bit-exact SHA-256 matches: "<<exact<<" of "<<cases.size()<<std::endl;
            require(bad.empty(),bad);
        });

        // ================================= group: split =================================
        if(!kSynth) {
            test("split","effect form: a requested/stored mode 2 (Synth) behaves exactly as FX (0)",[] {
                const Audio in=noiseSine(kScenarioFrames);
                std::string bad; for(const auto& sc:scenarios()) collect(bad,sameRenderProblem("mode 2 vs mode 0, "+sc.name,withMode(sc.setup,kSynthMode),withMode(sc.setup,kFx),in,notes())); require(bad.empty(),bad);
            });
            test("split","effect form: old states/presets saved with mode 2 load and fall back to FX (0)",[] {
                const Audio in=noiseSine(kScenarioFrames);
                const std::vector<Fixture> old{stateFixture("custom-graph-fm"),stateFixture("factory-11-Copper-Strings"),presetFixture("Copper Strings"),presetFixture("Prism Keys")};
                std::string bad;
                for(const auto& f:old) {
                    require(storedMode(f)==kSynthMode,"fixture drift: "+f.label+" no longer stores mode 2");
                    collect(bad,sameRenderProblem("load "+f.label+" (stored mode 2) vs same + mode 0",[&](Processor& p) { loadFixture(p,f); },
                                      [&](Processor& p) { loadFixture(p,f); put(p,"global.mode",kFx); },in,notes()));
                }
                require(bad.empty(),bad);
            });
            test("split","effect form: old state with mode 2 does not crash across prepare/reset and keeps rendering finite",[] {
                const Audio in=noiseSine(kScenarioFrames); const auto f=stateFixture("custom-graph-fm");
                Processor p; loadFixture(p,f);
                for(const int block:{64,480}) { // prepare after load, as hosts do, then process, reset, process again
                    p.prepareToPlay(kRate,block);
                    for(int round=0;round<2;++round) {
                        for(int offset=0;offset+block<=24000;offset+=block) {
                            juce::AudioBuffer<float> buffer(2,block); juce::MidiBuffer midi; midi.addEvent(juce::MidiMessage::noteOn(1,60,0.7f),0);
                            std::copy_n(in.l.data()+offset,block,buffer.getWritePointer(0)); std::copy_n(in.r.data()+offset,block,buffer.getWritePointer(1));
                            p.processBlock(buffer,midi);
                            for(int i=0;i<block;++i) require(std::isfinite(buffer.getSample(0,i))&&std::isfinite(buffer.getSample(1,i)),"non-finite sample after loading a mode-2 state");
                        }
                        p.reset();
                    }
                }
            });
            // ---- Resynth + Source: Source engine driven by the input audio --------------------------------------------------
            const struct { const char* name; int engine; } engines[]={{"Phase Geometry",1},{"Chaos",2}};
            for(const auto& e:engines) {
                static std::vector<std::string> names; names.push_back(std::string("Resynth + Source (")+e.name+"): audible, finite, differs from Source off, silent input stays silent, no clicks at input start/cut");
                const int engine=e.engine;
                test("split",names.back().c_str(),[engine] {
                    const Audio in=tone(kToneFrames,220,0.5,kToneOn,kToneOff);
                    const auto on=render(resynthSource(engine,true),in,noMidi()),off=render(resynthSource(engine,false),in,noMidi());
                    require(finiteAll(on),"non-finite output");
                    const double diff=rmsDiff(on,off,24000,kToneOff);
                    require(diff>0.01,"Source had no effect in Resynth (rms difference vs Source off "+num(diff)+"): the Source engine is not driven by the input");
                    const double level=rms(on,24000,kToneOff);
                    require(level>0.01,"Resynth+Source output too quiet, rms "+num(level));
                    require(peak(on,0,kToneFrames)<8.0,"output not bounded, peak "+num(peak(on,0,kToneFrames)));
                    require(peak(on,0,kToneOn-480)<=1.0e-3,"output during silent input is not silent (peak "+num(peak(on,0,kToneOn-480))+")");
                    const double steady=maxStep(on,24000,kToneOff-1),bound=std::max(2.0*steady,0.1);
                    require(maxStep(on,kToneOn-1,kToneOn+2400)<=bound,"click at input onset: max step "+num(maxStep(on,kToneOn-1,kToneOn+2400))+" > "+num(bound)+" (steady "+num(steady)+")");
                    require(maxStep(on,kToneOff-1,kToneOff+4800)<=bound,"click at input cut: max step "+num(maxStep(on,kToneOff-1,kToneOff+4800))+" > "+num(bound)+" (steady "+num(steady)+")");
                    require(rms(on,kToneOff+19200,kToneFrames)<0.01,"output still sounding 0.4 s after input stopped, rms "+num(rms(on,kToneOff+19200,kToneFrames)));
                });
            }
            test("split","Resynth + Source (Phase Geometry): frequency follows the detected input pitch (220 Hz and 330 Hz)",[] {
                for(const double hz:{220.0,330.0}) {
                    const Audio in=tone(kToneFrames,hz,0.5,kToneOn,kToneOff);
                    const auto on=render(resynthSource(1,true),in,noMidi()),off=render(resynthSource(1,false),in,noMidi());
                    require(rmsDiff(on,off,24000,kToneOff)>0.01,"Source had no effect in Resynth, pitch test would prove nothing ("+num(hz)+" Hz input)");
                    const double f0=estimateHz(on,26000,kToneOff-2000);
                    require(f0>0 && std::abs(f0-hz)<=0.04*hz,"output fundamental "+num(f0)+" Hz does not follow the "+num(hz)+" Hz input (tolerance 4%)");
                }
            });
            test("split","Resynth + Source (both engines): amplitude follows the input envelope (0.5 vs 0.1)",[] {
                for(const int engine:{1,2}) {
                    const auto loud=render(resynthSource(engine,true),tone(kToneFrames,220,0.5,kToneOn,kToneOff),noMidi());
                    const auto quiet=render(resynthSource(engine,true),tone(kToneFrames,220,0.1,kToneOn,kToneOff),noMidi());
                    const auto off=render(resynthSource(engine,false),tone(kToneFrames,220,0.5,kToneOn,kToneOff),noMidi());
                    require(rmsDiff(loud,off,24000,kToneOff)>0.01,"engine "+std::to_string(engine)+": Source had no effect in Resynth");
                    const double a=rms(loud,24000,kToneOff),b=rms(quiet,24000,kToneOff);
                    require(a>1.5*b && b>1.0e-5,"engine "+std::to_string(engine)+": output level does not follow the input level (rms "+num(a)+" for 0.5, "+num(b)+" for 0.1)");
                }
            });
            test("split","Resynth + Source: deterministic and block-size independent (17/257/1000 vs 256)",[] {
                const Audio in=tone(kToneFrames,220,0.5,kToneOn,kToneOff);
                for(const int engine:{1,2}) {
                    const auto a=render(resynthSource(engine,true),in,noMidi(),256),b=render(resynthSource(engine,true),in,noMidi(),256),off=render(resynthSource(engine,false),in,noMidi(),256);
                    require(rmsDiff(a,off,24000,kToneOff)>0.01,"engine "+std::to_string(engine)+": Source had no effect in Resynth, determinism would prove nothing");
                    require(maxAbsDiff(a,b)==0.0,"engine "+std::to_string(engine)+": identical renders differ by "+num(maxAbsDiff(a,b)));
                    for(const int block:{17,257,1000}) { const double d=maxAbsDiff(a,render(resynthSource(engine,true),in,noMidi(),block)); require(d<=kBlockSizeTol,"engine "+std::to_string(engine)+": block "+std::to_string(block)+" differs by "+num(d)); }
                }
            });
            test("split","Resynth + Source: extreme source parameters stay finite and bounded",[] {
                const Audio in=tone(kToneFrames,220,0.9,kToneOn,kToneOff);
                for(const int engine:{1,2}) {
                    auto setup=[engine](Processor& p) { resynthSource(engine,true)(p); put(p,"source.gain",1); put(p,"source.pg.czdepth",1); put(p,"source.pg.vpsratio",16); put(p,"source.chaos.ampstep",1); put(p,"source.chaos.durstep",1); put(p,"source.chaos.segments",32); put(p,"global.input",12); };
                    const auto a=render(setup,in,noMidi());
                    auto setupOff=[setup](Processor& p) { setup(p); put(p,"source.enabled",0); };
                    require(rmsDiff(a,render(setupOff,in,noMidi()),24000,kToneOff)>0.01,"engine "+std::to_string(engine)+": Source had no effect in Resynth, bounds test would prove nothing");
                    require(finiteAll(a),"engine "+std::to_string(engine)+": non-finite with extreme parameters");
                    require(peak(a,0,kToneFrames)<=32.0,"engine "+std::to_string(engine)+": unbounded output");
                    require(rms(a,24000,kToneOff)>0.01,"engine "+std::to_string(engine)+": silent with extreme parameters");
                }
            });
        } else {
            test("split","synth form: a requested/stored mode 0 (FX) or 1 (Resynth) behaves exactly as Synth (2)",[] {
                const Audio in=noiseSine(kScenarioFrames);
                std::string bad;
                for(const auto& sc:scenarios()) for(const int mode:{kFx,kResynth})
                    collect(bad,sameRenderProblem("mode "+std::to_string(mode)+" vs mode 2, "+sc.name,withMode(sc.setup,mode),withMode(sc.setup,kSynthMode),in,notes()));
                require(bad.empty(),bad);
            });
            test("split","synth form: old states/presets saved with mode 0 or 1 load and render as Synth",[] {
                const Audio in=noiseSine(kScenarioFrames);
                const std::vector<Fixture> old{stateFixture("default"),stateFixture("ab-snapshot-b"),stateFixture("factory-00-Gentle-Orbit"),stateFixture("factory-05-Iron-Drum-Room"),
                    stateFixture("factory-12-Atlas---Suspended-Paper"),stateFixture("factory-20-Atlas---Frozen-Architecture"),stateFixture("factory-35-Motion---Stochastic-Room"),
                    presetFixture("Resynth Halo"),presetFixture("Clean Canvas"),presetFixture("Glass Resonator")};
                std::string bad;
                for(const auto& f:old) {
                    const int stored=storedMode(f); require(stored==kFx||stored==kResynth,"fixture drift: "+f.label+" no longer stores mode 0/1");
                    collect(bad,sameRenderProblem("load "+f.label+" (stored mode "+std::to_string(stored)+") vs same + mode 2",[&](Processor& p) { loadFixture(p,f); },
                                      [&](Processor& p) { loadFixture(p,f); put(p,"global.mode",kSynthMode); },in,notes()));
                }
                require(bad.empty(),bad);
            });
            test("split","synth form: factory presets (including the two stored with mode 1) all render as Synth",[] {
                const Audio in=noiseSine(kScenarioFrames); const int count=Processor::factoryPresetNames().size();
                std::string bad;
                for(int k=0;k<count;++k)
                    collect(bad,sameRenderProblem("factory preset "+std::to_string(k)+" as loaded vs mode 2",[k](Processor& p) { p.loadFactoryPreset(k); },
                                      [k](Processor& p) { p.loadFactoryPreset(k); put(p,"global.mode",kSynthMode); },in,notes()));
                require(bad.empty(),bad);
            });
        }
        std::cout<<"RESULT "<<passed<<" tests passed, "<<failed<<" failed"<<std::endl; return failed?1:0;
    } catch(const std::exception& error) { std::cout<<"FATAL "<<error.what()<<std::endl; return 1; }
}


