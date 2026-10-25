// 2.3.0 modulation-destination tests, written BEFORE the destination extension (baseline: integration/2.2.0-rc, 84 destinations).
//
// Built twice from this one file (JucePlugin_IsSynth is a compile-time macro):
//   AetherModTargetsFx     links "AETHER Micro"        (effect form)
//   AetherModTargetsSynth  links "AETHER Micro Synth"  (instrument form)
//
// Groups (command line  --group invariant|new|all , default all):
//   invariant  must be GREEN on the 2.2.0 baseline and stay green after the destination extension (2.3.0 may only APPEND destinations).
//   new        asserts the DRAFT 2.3.0 destination list 85..112 (option count 113). RED on the 2.2.0 baseline, GREEN once the
//              destination extension (branch feat/mod-targets-230) is implemented. These tests do not guess: every expectation
//              comes from the sound engineer's draft (labels are not hard-coded: only the 'Source / ' / 'FM / ' prefixes and a 24-character limit are asserted).
// Other modes:
//   --only <substring>              run only tests whose name contains the substring
//   --list                          list test names with their group
//   --generate-target-table <file>  write the frozen destination/source name table (used once, on the 2.2.0 baseline)
//
// Everything in the invariant group is parameterised by the PUBLIC destination count (aether::modular::targetCount), so
// destinations appended later are covered automatically:
//   * table snapshot (prefix of the current table must equal the frozen 2.2.0 table, option count == targetCount+1)
//   * old saves (schema 1/2 fixtures + schema 3 round trip): routing read back unchanged
//   * extreme depth: every destination x several sources x depth +-1 (and out-of-range +-2) renders finite and bounded
//   * validity: modulating a destination changes what its parameter controls (engine level: modulation == direct parameter
//     write; plugin level: modulation changes the output iff writing the underlying parameter directly does).
#include "PluginProcessor.h"
#include <chrono>
#include <aether/Modular.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
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
namespace mod=aether::modular;
constexpr bool kSynth=(JucePlugin_IsSynth!=0);
constexpr double kRate=48000.0;
constexpr int kBaselineTargets=84;           // 2.2.0: destinations 1..84 = 12 nodes x 7 (Macro A-D, Mix, Level, Pan)
constexpr int kDraftTargets=112;             // 2.3.0 draft: 85..112 appended
constexpr double kBound=16.0;                // "bounded": |sample| <= 16 (+24 dBFS); the graph engine itself clamps at 8

int passed=0,failed=0;
std::string selectedGroup="all",onlyFilter;
void require(bool ok,const std::string& why) { if(!ok) throw std::runtime_error(why); }
template<class F> void test(const char* group,const std::string& name,F f) {
    if(selectedGroup=="list") { std::cout<<group<<'\t'<<name<<'\n'; return; }
    if(selectedGroup!="all" && selectedGroup!=group) return;
    if(!onlyFilter.empty() && name.find(onlyFilter)==std::string::npos) return;
    const auto started=std::chrono::steady_clock::now(); auto ms=[&]{ return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count(); };
    try { f(); ++passed; std::cout<<"PASS ["<<group<<"] "<<name<<"  ("<<ms()<<" ms)"<<std::endl; }
    catch(const std::exception& error) { ++failed; std::cout<<"FAIL ["<<group<<"] "<<name<<": "<<error.what()<<"  ("<<ms()<<" ms)"<<std::endl; }
}
std::string num(double v) { char b[48]; std::snprintf(b,sizeof b,"%.6g",v); return b; }

// ---- audio helpers --------------------------------------------------------
struct Audio { std::vector<float> l,r; };
struct MidiEvent { int at; juce::MidiMessage message; };
using Setup=std::function<void(Processor&)>;
Audio silence(int frames) { Audio a; a.l.assign(static_cast<size_t>(frames),0.f); a.r=a.l; return a; }
Audio noiseSine(int frames) {
    Audio a=silence(frames); juce::Random rng(20261025);
    for(int i=0;i<frames;++i) {
        const double t=i/kRate;
        a.l[static_cast<size_t>(i)]=static_cast<float>(0.20*std::sin(2*juce::MathConstants<double>::pi*220*t)+0.05*(rng.nextDouble()*2-1));
        a.r[static_cast<size_t>(i)]=static_cast<float>(0.20*std::sin(2*juce::MathConstants<double>::pi*330*t)+0.05*(rng.nextDouble()*2-1));
    }
    return a;
}
const std::vector<MidiEvent>& notes() {
    static const std::vector<MidiEvent> script{
        {1000,juce::MidiMessage::noteOn(1,60,0.8f)},{12000,juce::MidiMessage::noteOn(1,67,0.6f)},
        {30000,juce::MidiMessage::noteOff(1,60)},{34000,juce::MidiMessage::noteOff(1,67)}};
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
double peakAll(const Audio& a) { double m=0; for(size_t i=0;i<a.l.size();++i) m=std::max({m,double(std::abs(a.l[i])),double(std::abs(a.r[i]))}); return m; }
double maxAbsDiff(const Audio& a,const Audio& b) {
    double d=0; for(size_t i=0;i<a.l.size();++i) d=std::max({d,double(std::abs(a.l[i]-b.l[i])),double(std::abs(a.r[i]-b.r[i]))}); return d;
}
void put(Processor& p,const char* id,float v) { p.setParameterValue(id,v); }
juce::String rowId(int row,const char* field) { return "modulation."+juce::String(row)+"."+field; }

// ---- table of destination names -------------------------------------------
juce::StringArray currentTargetNames() { return aether::modulationTargetNames(); }
juce::File fixtureFile() { return juce::File(AETHER_SOURCE_DIR).getChildFile("tests").getChildFile("fixtures").getChildFile("mod-targets").getChildFile("targets-v2.2.0.txt"); }
struct FrozenTable { std::vector<std::string> targets,sources; };
FrozenTable loadFrozen() {
    const auto file=fixtureFile(); require(file.existsAsFile(),"missing frozen table "+file.getFullPathName().toStdString());
    FrozenTable t; std::istringstream in(file.loadFileAsString().toStdString()); std::string line;
    while(std::getline(in,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line.empty()||line[0]=='#') continue;
        const auto tab=line.find('\t'); require(tab!=std::string::npos,"bad frozen line: "+line);
        const auto kind=line[0]; const int index=std::stoi(line.substr(1,tab-1)); const auto name=line.substr(tab+1);
        auto& v=(kind=='T')?t.targets:t.sources; require(kind=='T'||kind=='S',"bad frozen kind: "+line);
        require(index==static_cast<int>(v.size()),"frozen table index gap at "+line); v.push_back(name);
    }
    return t;
}
int optionCount(Processor& p,const juce::String& id) {
    auto* choice=dynamic_cast<juce::AudioParameterChoice*>(p.state.getParameter(id)); require(choice!=nullptr,"not a choice parameter: "+id.toStdString());
    return choice->choices.size();
}
juce::StringArray optionNames(Processor& p,const juce::String& id) {
    auto* choice=dynamic_cast<juce::AudioParameterChoice*>(p.state.getParameter(id)); require(choice!=nullptr,"not a choice parameter: "+id.toStdString());
    return choice->choices;
}

// ---- routing readback -------------------------------------------------------
struct Row { double source,target,depth; };
std::vector<Row> routing(Processor& p) {
    std::vector<Row> rows;
    for(int n=1;n<=mod::modulationCount;++n) rows.push_back({p.parameterValue(rowId(n,"source")),p.parameterValue(rowId(n,"target")),p.parameterValue(rowId(n,"depth"))});
    return rows;
}
std::string describe(const Row& r) { return "(src "+num(r.source)+", tgt "+num(r.target)+", depth "+num(r.depth)+")"; }
void requireRouting(const std::vector<Row>& got,const std::vector<Row>& want,const std::string& what) {
    require(got.size()==want.size(),what+": row count");
    for(size_t i=0;i<got.size();++i)
        require(got[i].source==want[i].source && got[i].target==want[i].target && std::abs(got[i].depth-want[i].depth)<1e-6,
                what+": row "+std::to_string(i+1)+" got "+describe(got[i])+" want "+describe(want[i]));
}
std::string textOf(Processor& p,const juce::String& id) {
    auto* parameter=p.state.getParameter(id); require(parameter!=nullptr,"missing parameter "+id.toStdString());
    return parameter->getText(parameter->getValue(),256).toStdString();
}

struct Sample { std::string name; bool isPreset; juce::File file; };
std::vector<Sample> samples() {
    std::vector<Sample> out; const auto base=juce::File(AETHER_SOURCE_DIR).getChildFile("tests").getChildFile("fixtures");
    auto gather=[&](const char* folder,const char* pattern,bool preset) {
        std::vector<juce::File> files; for(const auto& f:base.getChildFile(folder).findChildFiles(juce::File::findFiles,false,pattern)) files.push_back(f);
        std::sort(files.begin(),files.end(),[](const juce::File& a,const juce::File& b){return a.getFileName()<b.getFileName();});
        for(const auto& f:files) out.push_back({std::string(folder)+"/"+f.getFileName().toStdString(),preset,f});
    };
    gather("presets-v1.0.0","*.aether",true); gather("state-v2.0.1","*.bin",false); return out;
}
void load(Processor& p,const Sample& s) {
    if(s.isPreset) { const auto r=p.loadPreset(s.file); require(r.wasOk(),"preset load failed: "+r.getErrorMessage().toStdString()); return; }
    juce::MemoryBlock data; require(s.file.loadFileAsData(data)&&data.getSize()>0,"cannot read "+s.name);
    p.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
}
// The routing a fixture file itself contains, read straight from the file (independent of the processor).
std::vector<Row> routingInFile(const Sample& s) {
    std::vector<Row> rows(mod::modulationCount,Row{0,0,0});
    if(s.isPreset) return rows;                    // schema 1 presets never contain modulation.* (legacy mod.* only)
    juce::MemoryBlock data; require(s.file.loadFileAsData(data),"cannot read state");
    const auto xml=juce::AudioProcessor::getXmlFromBinary(data.getData(),static_cast<int>(data.getSize())); require(xml!=nullptr,"not binary XML");
    const auto tree=juce::ValueTree::fromXml(*xml);
    for(int n=1;n<=mod::modulationCount;++n) {
        const auto get=[&](const char* f) { const auto c=tree.getChildWithProperty("id",rowId(n,f)); require(c.isValid(),"state lacks "+rowId(n,f).toStdString()); return static_cast<double>(c.getProperty("value")); };
        rows[static_cast<size_t>(n-1)]={get("source"),get("target"),get("depth")};
    }
    return rows;
}

// ---- engine-level helpers ----------------------------------------------------
constexpr int kEngineFrames=4800,kEngineBlock=256;
mod::Parameters parallelGraph(int rotation) {
    mod::Parameters p; mod::applyAlgorithm(1,p);       // algorithm 1: twelve independent one-node chains, every node gets the input
    for(int k=0;k<mod::slotCount;++k) {
        mod::assignModule(p,k,static_cast<mod::ModuleType>(1+(k+rotation*7)%20));
        auto& s=p.slots[static_cast<size_t>(k)]; s.enabled=true; s.mix=0.5f;
    }
    for(int i=0;i<4;++i) { p.lfo[static_cast<size_t>(i)].rate=8.f+3.f*static_cast<float>(i); p.lfo[static_cast<size_t>(i)].shape=(i%2)?3:0; }
    return p;
}
Audio engineRender(const mod::Parameters& prm,float velocity) {
    static const Audio input=noiseSine(kEngineFrames);
    mod::GraphEngine g; g.prepare(kRate,kEngineBlock); g.setPerformance(velocity,440.f); g.setParameters(prm);
    Audio out=input;
    for(int o=0;o<kEngineFrames;o+=kEngineBlock) g.process(out.l.data()+o,out.r.data()+o,nullptr,std::min(kEngineBlock,kEngineFrames-o));
    return out;
}
// Engine contract for destinations 1..84 (Modular.cpp updateControls): slot=(t-1)/7, attr=(t-1)%7; attr 0-3 macro, 4 mix, 5 level(24 dB per unit), 6 pan.
void applyDirect(mod::Parameters& p,int target,float amount) {
    auto& s=p.slots[static_cast<size_t>((target-1)/7)]; const int attr=(target-1)%7;
    auto clampf=[](float v,float lo,float hi) { return std::min(hi,std::max(lo,v)); };
    if(attr<4) s.macro[static_cast<size_t>(attr)]=clampf(s.macro[static_cast<size_t>(attr)]+amount,0,1);
    else if(attr==4) s.mix=clampf(s.mix+amount,0,1);
    else if(attr==5) s.levelDb=clampf(s.levelDb+24*amount,-24,12);
    else s.pan=clampf(s.pan+amount,-1,1);
}

// ---- plugin-level configurations ------------------------------------------------
constexpr int kPluginFrames=36000;
void neutralGraph(Processor& p) {                       // graph on, 12 pass-through nodes in series: audio unchanged, modulation sources alive
    put(p,"graph.enabled",1); put(p,"routing.algorithm",0);
    for(int n=1;n<=12;++n) { const auto id="node."+juce::String(n)+"."; put(p,(id+"type").toRawUTF8(),0); put(p,(id+"on").toRawUTF8(),1); put(p,(id+"mix").toRawUTF8(),1); }
}
void slotGraph(Processor& p) {                          // graph on, 12 independent audible nodes (every slot destination can be heard)
    put(p,"graph.enabled",1); put(p,"routing.algorithm",1);
    for(int n=1;n<=12;++n) {
        const auto id="node."+juce::String(n)+".";
        put(p,(id+"type").toRawUTF8(),static_cast<float>(1+(n-1)%20)); put(p,(id+"on").toRawUTF8(),1); put(p,(id+"mix").toRawUTF8(),0.5f);
    }
}
void lfoRow(Processor& p,int row,int source,int target,float depth) {
    put(p,"lfo.1.shape",3); put(p,"lfo.1.rate",6); put(p,"lfo.2.shape",0); put(p,"lfo.2.rate",5);
    p.setParameterValue(rowId(row,"source"),static_cast<float>(source)); p.setParameterValue(rowId(row,"target"),static_cast<float>(target)); p.setParameterValue(rowId(row,"depth"),depth);
}

// ---- draft 2.3.0 destinations 85..112 (sound engineer's draft). Numbers, the total (113 options) and the group boundaries are asserted;
//      labels are NOT hard-coded: 85..101 must start with 'Source / ', 102..112 with 'FM / ', all <= 24 characters. parameter is the assumed target parameter. -------
enum Domain { SourceCommon=0, PhaseGeometry=1, ChaosEngine=2, FmEngine=3 };
struct Draft { int index; const char* parameter; Domain domain; };
const std::vector<Draft>& drafts() {
    static const std::vector<Draft> d{
        {85,"source.gain",SourceCommon},{86,"source.spread",SourceCommon},{87,"source.transpose",SourceCommon},
        {88,"source.pg.sequency",PhaseGeometry},{89,"source.pg.walshmix",PhaseGeometry},{90,"source.pg.czdepth",PhaseGeometry},
        {91,"source.pg.czenvelope",PhaseGeometry},{92,"source.pg.czvelocity",PhaseGeometry},{93,"source.pg.vpsd",PhaseGeometry},
        {94,"source.pg.vpsv",PhaseGeometry},{95,"source.pg.vpsratio",PhaseGeometry},{96,"source.pg.phaserandom",PhaseGeometry},
        {97,"source.chaos.ampstep",ChaosEngine},{98,"source.chaos.durstep",ChaosEngine},{99,"source.chaos.durrange",ChaosEngine},
        {100,"source.chaos.momentum",ChaosEngine},{101,"source.chaos.ampbarrier",ChaosEngine},
        {102,"fm.index",FmEngine},{103,"fm.morph",FmEngine},{104,"fm.feedback",FmEngine},
        {105,"fm.gain",FmEngine},{106,"fm.spread",FmEngine},
        {107,"fm.op.1.level",FmEngine},{108,"fm.op.2.level",FmEngine},{109,"fm.op.3.level",FmEngine},
        {110,"fm.op.4.level",FmEngine},{111,"fm.op.5.level",FmEngine},{112,"fm.op.6.level",FmEngine}};
    return d;
}
// Engine/mode configuration in which the draft destination's domain is audible.  `engine` forces the Source engine (0 off, 1 PG, 2 Chaos).
Setup domainSetup(Domain domain,int sourceEngineOverride=-1) {
    return [=](Processor& p) {
        neutralGraph(p); put(p,"global.mix",1);
        if(!kSynth) put(p,"global.mode",1);              // effect form: Resynth (Source and FM are driven by input audio + MIDI); instrument form clamps to Synth itself
        const bool fmOn=(domain==FmEngine);
        put(p,"fm.enabled",fmOn?1.f:0.f);
        int engine=(domain==ChaosEngine)?2:(domain==SourceCommon||domain==PhaseGeometry)?1:0;
        if(sourceEngineOverride>=0) engine=sourceEngineOverride;
        put(p,"source.engine",static_cast<float>(engine)); put(p,"source.enabled",engine>0?1.f:0.f);
        if(domain==PhaseGeometry) put(p,"source.pg.walshmix",0.5f);
    };
}
// Writes `parameterId` directly to base+amount in the normalised domain (what the draft semantics say modulation does to the internal copy).
void directWrite(Processor& p,const char* parameterId,float normalisedAmount) {
    auto* parameter=p.state.getParameter(parameterId); require(parameter!=nullptr,std::string("no parameter ")+parameterId);
    parameter->setValueNotifyingHost(std::min(1.f,std::max(0.f,parameter->getValue()+normalisedAmount)));
}
const Draft& draftFor(int index) { for(const auto& d:drafts()) if(d.index==index) return d; throw std::runtime_error("no draft"); }

void requireDestinationExists(int index) {
    Processor p; const int options=optionCount(p,"modulation.1.target");
    require(options>index,"destination "+std::to_string(index)+" does not exist: modulation.N.target has "+std::to_string(options)+" options (indices 0.."+std::to_string(options-1)+"), mod::targetCount="+std::to_string(mod::targetCount));
}
} // namespace

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    std::string generateTo;
    for(int i=1;i<argc;++i) {
        const std::string a=argv[i];
        if(a=="--group" && i+1<argc) selectedGroup=argv[++i];
        else if(a=="--list") selectedGroup="list";
        else if(a=="--only" && i+1<argc) onlyFilter=argv[++i];
        else if(a=="--generate-target-table" && i+1<argc) generateTo=argv[++i];
    }
    if(!generateTo.empty()) {
        std::ostringstream o; o<<"# Frozen modulation destination / source tables of the 2.2.0 baseline (integration/2.2.0-rc, 991e493).\n"
            "# Generated by AetherModTargets*  --generate-target-table.  T<index><TAB><name> = modulation.N.target options, S<index><TAB><name> = modulation.N.source options.\n"
            "# 2.3.0 may only APPEND destinations: the first 85 target lines must keep reading exactly like this.\n";
        const auto names=currentTargetNames(); for(int i=0;i<names.size();++i) o<<'T'<<i<<'\t'<<names[i].toStdString()<<'\n';
        for(int i=0;i<mod::sourceCount;++i) o<<'S'<<i<<'\t'<<mod::sourceName(i)<<'\n';
        juce::File f(generateTo); f.getParentDirectory().createDirectory(); f.replaceWithText(juce::String(o.str()));
        std::cout<<"wrote "<<generateTo<<" ("<<names.size()<<" targets)\n"; return 0;
    }
    require(kSynth==(AETHER_EXPECT_SYNTH!=0),"plugin form mismatch");

    // ================================ (a) table snapshot ===========================================
    test("invariant","a1 frozen 2.2.0 destination table is a prefix of the current table",[] {
        const auto frozen=loadFrozen(); const auto now=currentTargetNames();
        require(frozen.targets.size()==static_cast<size_t>(kBaselineTargets+1),"frozen table must hold 85 entries (None + 84)");
        require(now.size()>=static_cast<int>(frozen.targets.size()),"current table shrank to "+std::to_string(now.size()));
        for(size_t i=0;i<frozen.targets.size();++i) require(now[static_cast<int>(i)].toStdString()==frozen.targets[i],"destination "+std::to_string(i)+" changed: '"+frozen.targets[i]+"' -> '"+now[static_cast<int>(i)].toStdString()+"'");
    });
    test("invariant","a2 destination count is public and consistent (targetCount+1 options in every row, same names)",[] {
        require(mod::targetCount>=kBaselineTargets,"mod::targetCount shrank");
        const auto now=currentTargetNames(); require(now.size()==mod::targetCount+1,"modulationTargetNames() has "+std::to_string(now.size())+" entries, targetCount="+std::to_string(mod::targetCount));
        require(mod::modulationCount==16,"modulation row count changed");
        Processor p;
        for(int n=1;n<=mod::modulationCount;++n) {
            const auto names=optionNames(p,rowId(n,"target")); require(names==now,"row "+std::to_string(n)+" option list differs from modulationTargetNames()");
            auto* parameter=p.state.getParameter(rowId(n,"target")); require(parameter->getNumSteps()==mod::targetCount+1,"row "+std::to_string(n)+" step count");
        }
    });
    test("invariant","a3 destination 1..84 mean node (t-1)/7+1 and attribute (t-1)%7 (names)",[] {
        const auto now=currentTargetNames(); const char* attr[]{"Macro A","Macro B","Macro C","Macro D","Mix","Level","Pan"};
        for(int t=1;t<=kBaselineTargets;++t) require(now[t].toStdString()=="Node "+std::to_string((t-1)/7+1)+" / "+attr[(t-1)%7],"destination "+std::to_string(t)+" is '"+now[t].toStdString()+"'");
    });
    test("invariant","a4 source table (10 entries) and modulation parameter IDs/order unchanged",[] {
        const auto frozen=loadFrozen(); require(mod::sourceCount>=10 && frozen.sources.size()==10,"source count");
        for(int i=0;i<10;++i) require(std::string(mod::sourceName(i))==frozen.sources[static_cast<size_t>(i)],"source "+std::to_string(i)+" changed");
        const auto& specs=aether::parameterSpecs();
        for(int n=1;n<=mod::modulationCount;++n) for(int k=0;k<3;++k) {
            const char* field[]{"source","target","depth"}; const auto want=rowId(n,field[k]);
            require(specs[static_cast<size_t>(aether::modulationOffset+(n-1)*3+k)].id==want,"parameter order changed at "+want.toStdString());
        }
        require(aether::totalParameterCount==aether::sourceOffset+aether::sourceParameterCount,"parameter layout");
    });

    // ================================ (b) old saves ====================================================
    for(const auto& s:samples()) test("invariant","b1 old save loads with routing unchanged: "+s.name,[s] {
        Processor p; load(p,s); const auto want=routingInFile(s); requireRouting(routing(p),want,s.name);
        for(int n=1;n<=mod::modulationCount;++n) { // the option index keeps its meaning: text at the stored index is the frozen 2.2.0 name
            const int idx=static_cast<int>(want[static_cast<size_t>(n-1)].target); const auto frozen=loadFrozen();
            require(idx>=0&&idx<=kBaselineTargets,"fixture target out of the 2.2.0 range");
            require(textOf(p,rowId(n,"target"))==frozen.targets[static_cast<size_t>(idx)],"row "+std::to_string(n)+" option text changed for index "+std::to_string(idx));
        }
    });
    test("invariant","b2 known fixture routings (hard-coded from the committed files)",[] {
        Sample custom{"custom",false,juce::File(AETHER_SOURCE_DIR).getChildFile("tests/fixtures/state-v2.0.1/custom-graph-fm.bin")};
        Sample motion{"motion",false,juce::File(AETHER_SOURCE_DIR).getChildFile("tests/fixtures/state-v2.0.1/factory-35-Motion---Stochastic-Room.bin")};
        { Processor p; load(p,custom); auto r=routing(p); require(r[3].source==3&&r[3].target==82&&std::abs(r[3].depth+0.37)<1e-6,"custom-graph-fm row 4 "+describe(r[3]));
          require(textOf(p,"modulation.4.target")=="Node 12 / Mix","row 4 text"); for(int i=0;i<16;++i) if(i!=3) require(r[static_cast<size_t>(i)].depth==0&&r[static_cast<size_t>(i)].target==0,"custom-graph-fm other row nonzero"); }
        { Processor p; load(p,motion); auto r=routing(p); require(r[0].source==1&&r[0].target==1&&std::abs(r[0].depth-0.3)<1e-6,"motion row 1 "+describe(r[0]));
          require(r[1].source==2&&r[1].target==17&&std::abs(r[1].depth-0.2)<1e-6,"motion row 2 "+describe(r[1])); require(textOf(p,"modulation.2.target")=="Node 3 / Macro C","row 2 text"); }
    });
    test("invariant","b3 schema 3 round trip keeps every row incl. destination 84; same bytes re-stamped schema 2 and 1 keep the routing",[] {
        Processor a; for(int n=1;n<=mod::modulationCount;++n) { a.setParameterValue(rowId(n,"source"),static_cast<float>(n%10)); a.setParameterValue(rowId(n,"target"),static_cast<float>(n==16?kBaselineTargets:n*5)); a.setParameterValue(rowId(n,"depth"),(n%2?1.f:-1.f)*0.0625f*static_cast<float>(n)); }
        const auto want=routing(a); require(want[15].target==kBaselineTargets,"setup: destination 84 not stored");
        juce::MemoryBlock saved; a.getStateInformation(saved);
        { Processor b; b.setStateInformation(saved.getData(),static_cast<int>(saved.getSize())); requireRouting(routing(b),want,"schema 3 round trip"); }
        const auto xml=juce::AudioProcessor::getXmlFromBinary(saved.getData(),static_cast<int>(saved.getSize())); require(xml!=nullptr,"state xml"); require(xml->getIntAttribute("schema")==3,"saved schema is not 3");
        for(int schema:{2,1}) { auto copy=*xml; copy.setAttribute("schema",schema); juce::MemoryBlock block; juce::AudioProcessor::copyXmlToBinary(copy,block);
            Processor b; b.setStateInformation(block.getData(),static_cast<int>(block.getSize())); requireRouting(routing(b),want,"schema "+std::to_string(schema)+" stamped copy"); }
    });
    test("invariant","b4 out-of-range stored destination index is clamped to a valid option (no crash, no wrap)",[] {
        Processor a; a.setParameterValue("modulation.1.target",5); juce::MemoryBlock saved; a.getStateInformation(saved);
        auto xml=juce::AudioProcessor::getXmlFromBinary(saved.getData(),static_cast<int>(saved.getSize())); require(xml!=nullptr,"xml");
        auto tree=juce::ValueTree::fromXml(*xml); auto child=tree.getChildWithProperty("id","modulation.1.target"); require(child.isValid(),"no row");
        child.setProperty("value",9999.0,nullptr); auto edited=tree.createXml(); juce::MemoryBlock block; juce::AudioProcessor::copyXmlToBinary(*edited,block);
        Processor b; b.setStateInformation(block.getData(),static_cast<int>(block.getSize()));
        const int got=static_cast<int>(b.parameterValue("modulation.1.target")); require(got==mod::targetCount,"clamped to "+std::to_string(got)+", want "+std::to_string(mod::targetCount));
    });

    // ================================ (c) extreme depth, parameterised by targetCount =========================
    test("invariant","c1 engine: every destination x sources 1..9 x depth +-1/+-2 renders finite and |x|<=8",[] {
        double worst=0; int renders=0;
        for(int t=1;t<=mod::targetCount;++t) for(int src=1;src<mod::sourceCount;++src) for(float d:{-1.f,1.f,-2.f,2.f}) {
            auto prm=parallelGraph(t%3); prm.modulation[0]={src,t,d}; if(src==5||src==9) prm.modulation[1]={src,t,-d};
            const auto out=engineRender(prm,(src==6)?1.f:0.f); ++renders;
            require(finiteAll(out),"non-finite output: destination "+std::to_string(t)+" source "+std::to_string(src)+" depth "+num(d));
            worst=std::max(worst,peakAll(out)); require(peakAll(out)<=8.0+1e-6,"unbounded output "+num(peakAll(out))+": destination "+std::to_string(t)+" source "+std::to_string(src)+" depth "+num(d));
        }
        std::cout<<"  c1: destinations="<<mod::targetCount<<" renders="<<renders<<" worst peak="<<num(worst)<<std::endl;
    });
    test("invariant","c2 plugin: every destination x {LFO pulse, Envelope, Chaos} x depth +-1 renders finite and bounded",[] {
        const auto in=noiseSine(kPluginFrames); double worst=0; int renders=0;
        for(int t=1;t<=mod::targetCount;++t) for(int src:{1,5,9}) for(float d:{-1.f,1.f}) {
            const bool slot=(t<=kBaselineTargets);
            // Source/FM destinations (85..): enable the engine of the destination's domain so the modulation has something to act on.
            Setup setup=[=](Processor& p) {
                if(slot) slotGraph(p); else { neutralGraph(p); put(p,"global.mix",1); }
                if(!slot) { put(p,"fm.enabled",1); put(p,"source.engine",static_cast<float>(1+t%2)); put(p,"source.enabled",1); }
                if(!kSynth) put(p,"global.mode",1);
                lfoRow(p,1,src,t,d); };
            const auto out=render(setup,in,notes()); ++renders;
            require(finiteAll(out),"non-finite output: destination "+std::to_string(t)+" source "+std::to_string(src)+" depth "+num(d));
            worst=std::max(worst,peakAll(out)); require(peakAll(out)<=kBound,"peak "+num(peakAll(out))+" > "+num(kBound)+": destination "+std::to_string(t)+" source "+std::to_string(src)+" depth "+num(d));
        }
        std::cout<<"  c2: destinations="<<mod::targetCount<<" renders="<<renders<<" worst peak="<<num(worst)<<std::endl;
    });

    // ================================ (d) validity ===========================================================
    test("invariant","d1 engine: modulation of destination t equals a direct write of its slot parameter, and is audible (t=1..84)",[] {
        // Constant source = Velocity (=1), so amount = depth. Output with the modulation row must match the output with the slot parameter written directly.
        std::vector<int> inert; double worstMismatch=0;
        for(int t=1;t<=kBaselineTargets;++t) {
            bool audible=false;
            for(int rotation=0;rotation<3;++rotation) for(float d:{-1.f,1.f}) {
                auto base=parallelGraph(rotation); const auto reference=engineRender(base,1.f);
                auto modulated=base; modulated.modulation[0]={6,t,d}; auto direct=base; applyDirect(direct,t,d);
                const auto m=engineRender(modulated,1.f),w=engineRender(direct,1.f);
                const double mismatch=maxAbsDiff(m,w); worstMismatch=std::max(worstMismatch,mismatch);
                require(mismatch<=1e-5,"destination "+std::to_string(t)+" (rotation "+std::to_string(rotation)+", depth "+num(d)+"): modulation differs from the direct write by "+num(mismatch));
                if(maxAbsDiff(w,reference)>1e-4) { audible=true; require(maxAbsDiff(m,reference)>1e-4,"destination "+std::to_string(t)+": direct write is audible but the modulation changed nothing"); }
            }
            if(!audible) inert.push_back(t);
        }
        std::string list; for(int t:inert) list+=std::to_string(t)+" ";
        std::cout<<"  d1: worst |modulation - direct write| = "<<num(worstMismatch)<<"; destinations whose direct write is inaudible with the 3 module rotations: "<<(list.empty()?"none":list)<<std::endl;
        require(inert.empty(),"destinations with no observable effect in this configuration: "+list);
    });
    test("invariant","d2 engine: destinations beyond 84 (none on 2.2.0) change the output when modulated by a constant source, else must be listed",[] {
        static const std::set<int> knownUnobservable{};   // new engine-level destinations without an observable (empty on purpose: list them here with the reason)
        for(int t=kBaselineTargets+1;t<=mod::targetCount;++t) {
            if(knownUnobservable.count(t)) continue;
            auto base=parallelGraph(0); const auto reference=engineRender(base,1.f); bool changed=false;
            for(float d:{-1.f,1.f}) { auto m=base; m.modulation[0]={6,t,d}; changed=changed||maxAbsDiff(engineRender(m,1.f),reference)>1e-4; }
            require(changed,"graph-engine destination "+std::to_string(t)+" has no observable effect");
        }
    });
    // oracle self-check + observability of the draft destinations (plugin level)
    for(const auto& d:drafts()) {
        const std::string label="destination "+std::to_string(d.index)+" ("+d.parameter+")";
        test("invariant","d3 oracle sensitive (baseline-valid): writing "+std::string(d.parameter)+" to full travel changes the output ["+label+"]",[d] {
            const auto in=noiseSine(kPluginFrames); const auto setup=domainSetup(d.domain);
            const auto ref=render(setup,in,notes()); require(finiteAll(ref)&&peakAll(ref)>1e-3,"reference render is silent: the configuration cannot observe this destination");
            double best=0; for(float amount:{-1.f,1.f}) { const auto w=render([&](Processor& p) { setup(p); directWrite(p,d.parameter,amount); },in,notes()); require(finiteAll(w),"non-finite"); best=std::max(best,maxAbsDiff(w,ref)); }
            require(best>1e-4,"writing the parameter changes nothing audible (max diff "+num(best)+") - configuration cannot observe it");
        });
        test("new","d4 modulation changes the output exactly when a direct write does, and matches it ["+label+"]",[d] {
            requireDestinationExists(d.index);
            const auto in=noiseSine(kPluginFrames); const auto setup=domainSetup(d.domain);
            // Source = Velocity would need MIDI velocity coupling; use LFO 1 (pulse, +-1) at rate 6 Hz: compare against depth 0 with the same row.
            const auto zero=render([&](Processor& p) { setup(p); lfoRow(p,1,1,d.index,0.f); },in,notes()); require(finiteAll(zero),"non-finite");
            double best=0; for(float depth:{-1.f,1.f}) {
                const auto m=render([&](Processor& p) { setup(p); lfoRow(p,1,1,d.index,depth); },in,notes()); require(finiteAll(m),"non-finite output at depth "+num(depth)); best=std::max(best,maxAbsDiff(m,zero)); }
            require(best>1e-4,"modulating to depth +-1 changed nothing audible (max diff "+num(best)+")");
        });
        if(d.domain==PhaseGeometry||d.domain==ChaosEngine) test("new","d5 engine mismatch is silent and safe (no effect, finite) ["+label+"]",[d] {
            requireDestinationExists(d.index);
            const auto in=noiseSine(kPluginFrames); const int other=(d.domain==PhaseGeometry)?2:1; const auto setup=domainSetup(d.domain,other);
            const auto zero=render([&](Processor& p) { setup(p); lfoRow(p,1,1,d.index,0.f); },in,notes()); require(finiteAll(zero)&&peakAll(zero)>1e-3,"reference render silent");
            for(float depth:{-1.f,1.f}) { const auto m=render([&](Processor& p) { setup(p); lfoRow(p,1,1,d.index,depth); },in,notes());
                require(finiteAll(m),"non-finite output"); require(maxAbsDiff(m,zero)<=1e-6,"destination of the inactive Source engine changed the output by "+num(maxAbsDiff(m,zero))); }
        });
        test("new","d6 extreme depth +-1 renders finite and bounded, for every source LFO/Envelope/Chaos ["+label+"]",[d] {
            requireDestinationExists(d.index);
            const auto in=noiseSine(kPluginFrames); const auto setup=domainSetup(d.domain);
            for(int src:{1,5,9}) for(float depth:{-1.f,1.f}) { const auto m=render([&](Processor& p) { setup(p); lfoRow(p,1,src,d.index,depth); },in,notes());
                require(finiteAll(m),"non-finite output (source "+std::to_string(src)+", depth "+num(depth)+")"); require(peakAll(m)<=kBound,"peak "+num(peakAll(m))+" > "+num(kBound)); }
        });
        test("new","d7 stored destination index survives a schema 3 save/load ["+label+"]",[d] {
            requireDestinationExists(d.index);
            Processor a; a.setParameterValue("modulation.3.source",2); a.setParameterValue("modulation.3.target",static_cast<float>(d.index)); a.setParameterValue("modulation.3.depth",-0.5f);
            juce::MemoryBlock saved; a.getStateInformation(saved); Processor b; b.setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
            require(static_cast<int>(b.parameterValue("modulation.3.target"))==d.index,"index changed to "+num(b.parameterValue("modulation.3.target")));
            require(static_cast<int>(b.parameterValue("modulation.3.source"))==2,"source changed"); require(std::abs(b.parameterValue("modulation.3.depth")+0.5f)<1e-6,"depth changed");
        });
    }

    // ================================ draft 2.3.0 table ===========================================================
    test("new","n1 option count is 113 (None + 112) in all 16 rows; mod::targetCount == 112",[] {
        require(mod::targetCount==kDraftTargets,"mod::targetCount is "+std::to_string(mod::targetCount)+", draft says 112");
        Processor p; for(int n=1;n<=mod::modulationCount;++n) require(optionCount(p,rowId(n,"target"))==kDraftTargets+1,"row "+std::to_string(n)+" has "+std::to_string(optionCount(p,rowId(n,"target")))+" options, draft says 113");
    });
    test("new","n2 options 1..84 keep the frozen names; 85..101 start with 'Source / ', 102..112 with 'FM / ', all <= 24 chars, unique",[] {
        const auto names=currentTargetNames(); require(names.size()==kDraftTargets+1,"names.size()="+std::to_string(names.size())+", expected 113");
        const auto frozen=loadFrozen(); for(size_t i=0;i<frozen.targets.size();++i) require(names[static_cast<int>(i)].toStdString()==frozen.targets[i],"destination "+std::to_string(i)+" changed");
        for(int i=kBaselineTargets+1;i<=kDraftTargets;++i) { const std::string n=names[i].toStdString(); const std::string prefix=(i<=101)?"Source / ":"FM / ";
            require(n.compare(0,prefix.size(),prefix)==0,"destination "+std::to_string(i)+" is '"+n+"', expected prefix '"+prefix+"'");
            require(n.size()<=24,"destination "+std::to_string(i)+" label '"+n+"' has "+std::to_string(n.size())+" chars (> 24)"); }
        std::set<std::string> unique; for(const auto& n:names) unique.insert(n.toStdString()); require(unique.size()==static_cast<size_t>(names.size()),"duplicate destination names");
    });    test("new","n3 parameter layout unchanged: only options were appended (parameter IDs and total count)",[] {
        require(aether::totalParameterCount==aether::sourceOffset+aether::sourceParameterCount,"layout"); Processor p;
        require(p.state.getParameter("modulation.16.target")!=nullptr,"row 16 id");
    });

    std::cout<<"RESULT ["<<selectedGroup<<"] "<<passed<<" passed, "<<failed<<" failed ("<<(kSynth?"synth":"fx")<<" form)"<<std::endl;
    return failed==0?0:1;
}