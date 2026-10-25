// 2.3.0 modulation destinations 85..112 (Source / FM parameters): plugin-level behavioural tests.
//
// Built twice from this one file (JucePlugin_IsSynth is a compile-time macro):
//   AetherModTargetsFx     links "AETHER Micro"       (effect,  JucePlugin_IsSynth == 0, mode 1 = Resynth is used)
//   AetherModTargetsSynth  links "AETHER Micro Synth" (synth,   JucePlugin_IsSynth == 1, mode 2 = Synth)
// Specification under test (see docs/modulation-destinations.md):
//   * modulation.N.target has 113 options, 85..112 appended; the stored value is the option index.
//   * amount = source * depth, summed over rows, applied in the destination parameter's normalised domain and clamped,
//     on top of the host parameter value (never written back). Graph control ticks every 16 samples.
//   * a patch without any (source != 0, target > 84, depth != 0) row renders exactly as before (the old code path).
// Groups: --group all|list ;  other modes: --bench (CPU, prints a table, not a test).
#include "PluginProcessor.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef AETHER_EXPECT_SYNTH
#error "AETHER_EXPECT_SYNTH must be defined by the build (0 = effect plugin, 1 = synth plugin)"
#endif

// ---- global allocation counter (same method as tests/SourceTests.cpp / ModularTests.cpp) ----
namespace { std::atomic<bool> trackAllocations{false}; std::atomic<size_t> allocations{0}; }
void* operator new(std::size_t n) {
    if(trackAllocations.load(std::memory_order_relaxed)) allocations.fetch_add(1,std::memory_order_relaxed);
    if(void* p=std::malloc(n?n:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
void operator delete[](void* p,std::size_t) noexcept { std::free(p); }
#ifdef _MSC_VER
void* operator new(std::size_t n,std::align_val_t a) {
    if(trackAllocations.load(std::memory_order_relaxed)) allocations.fetch_add(1,std::memory_order_relaxed);
    if(void* p=_aligned_malloc(n?n:1,static_cast<size_t>(a))) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n,std::align_val_t a) { return ::operator new(n,a); }
void operator delete(void* p,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p,std::size_t,std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p,std::size_t,std::align_val_t) noexcept { _aligned_free(p); }
#endif

namespace {
using Processor=AetherAudioProcessor;
constexpr bool kSynth=(JucePlugin_IsSynth!=0);
constexpr double kRate=48000.0;
int passed=0,failed=0;
std::string selectedGroup="all";
void require(bool ok,const std::string& why) { if(!ok) throw std::runtime_error(why); }
template<class F> void test(const char* name,F f) {
    if(selectedGroup=="list") { std::cout<<name<<'\n'; return; }
    try { f(); ++passed; std::cout<<"PASS "<<name<<std::endl; }
    catch(const std::exception& error) { ++failed; std::cout<<"FAIL "<<name<<": "<<error.what()<<std::endl; }
}
std::string num(double v) { char b[48]; std::snprintf(b,sizeof b,"%.6g",v); return b; }

struct Audio { std::vector<float> l,r; };
struct MidiEvent { int at; juce::MidiMessage message; };
using Setup=std::function<void(Processor&)>;
void put(Processor& p,const juce::String& id,float v) { p.setParameterValue(id,v); }

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
        {30000,juce::MidiMessage::noteOff(1,60)},{40000,juce::MidiMessage::noteOff(1,67)}};
    return script;
}
// Notes at awkward positions, so that the events fall in the middle of blocks and of 16-sample control periods.
const std::vector<MidiEvent>& denseNotes() {
    static const std::vector<MidiEvent> script{
        {1000,juce::MidiMessage::noteOn(1,60,0.8f)},{1003,juce::MidiMessage::noteOn(1,64,0.5f)},{5001,juce::MidiMessage::noteOn(1,72,0.9f)},
        {12000,juce::MidiMessage::noteOn(1,67,0.6f)},{20007,juce::MidiMessage::noteOff(1,64)},{20010,juce::MidiMessage::noteOn(1,55,0.7f)},
        {30000,juce::MidiMessage::noteOff(1,60)},{33333,juce::MidiMessage::noteOff(1,72)},{40000,juce::MidiMessage::noteOff(1,67)},{40001,juce::MidiMessage::noteOff(1,55)}};
    return script;
}
bool lastParametersUntouched=true;   // set by render(): every host parameter has the same raw value after the render as after setup
Audio render(const Setup& setup,const Audio& in,const std::vector<MidiEvent>& midiScript,int block=256) {
    Processor p; p.prepareToPlay(kRate,block); setup(p);
    std::vector<float> before; for(const auto& spec:aether::parameterSpecs()) before.push_back(p.parameterValue(spec.id));
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
    lastParametersUntouched=true; size_t i=0;
    for(const auto& spec:aether::parameterSpecs()) if(p.parameterValue(spec.id)!=before[i++]) lastParametersUntouched=false;
    return out;
}
bool finiteAll(const Audio& a) { for(size_t i=0;i<a.l.size();++i) if(!std::isfinite(a.l[i])||!std::isfinite(a.r[i])) return false; return true; }
double maxAbsDiff(const Audio& a,const Audio& b) {
    double d=0; for(size_t i=0;i<a.l.size();++i) d=std::max({d,double(std::abs(a.l[i]-b.l[i])),double(std::abs(a.r[i]-b.r[i]))}); return d;
}
double peak(const Audio& a) { double m=0; for(size_t i=0;i<a.l.size();++i) m=std::max({m,double(std::abs(a.l[i])),double(std::abs(a.r[i]))}); return m; }
bool identical(const Audio& a,const Audio& b) {
    return a.l.size()==b.l.size() && std::memcmp(a.l.data(),b.l.data(),a.l.size()*sizeof(float))==0 && std::memcmp(a.r.data(),b.r.data(),a.r.size()*sizeof(float))==0;
}

// ---- scenarios ----
enum Engine { NoSource=0,PhaseGeometry=1,Chaos=2 };
struct Row { int source,target; float depth; };
// Graph on, FM and/or one Source engine on, LFO 1 = 7 Hz pulse (+1/-1), LFO 2 = triangle. Base values chosen so that the
// Phase Geometry / Chaos parameters are audible when they move.
Setup scenario(int engine,bool fm,std::vector<Row> rows,bool hot=false) {
    return [=](Processor& p) {
        put(p,"global.mode",kSynth?2.f:1.f); put(p,"global.mix",1); put(p,"graph.enabled",1); put(p,"fm.enabled",fm?1.f:0.f);
        put(p,"source.engine",static_cast<float>(engine)); put(p,"source.enabled",engine>0?1.f:0.f);
        put(p,"lfo.1.shape",3); put(p,"lfo.1.rate",7); put(p,"lfo.2.shape",1); put(p,"lfo.2.rate",3.1f);
        put(p,"source.pg.walshmix",0.5f); put(p,"source.pg.czshape",1); put(p,"source.pg.czenvelope",0.3f); put(p,"source.pg.czvelocity",0.3f);
        put(p,"source.pg.vpsratio",3);
        if(hot) { put(p,"source.gain",1); put(p,"fm.gain",1); put(p,"fm.index",12); put(p,"fm.feedback",0.95f); put(p,"source.chaos.momentum",0.95f); put(p,"source.pg.vpsratio",16); }
        int n=1; for(const auto& row:rows) {
            const juce::String id="modulation."+juce::String(n++)+".";
            put(p,id+"source",static_cast<float>(row.source)); put(p,id+"target",static_cast<float>(row.target)); put(p,id+"depth",row.depth);
        }
    };
}
const char* const kIds[28]={"source.gain","source.spread","source.transpose","source.pg.sequency","source.pg.walshmix","source.pg.czdepth",
    "source.pg.czenvelope","source.pg.czvelocity","source.pg.vpsd","source.pg.vpsv","source.pg.vpsratio","source.pg.phaserandom",
    "source.chaos.ampstep","source.chaos.durstep","source.chaos.durrange","source.chaos.momentum","source.chaos.ampbarrier",
    "fm.index","fm.morph","fm.feedback","fm.gain","fm.spread","fm.op.1.level","fm.op.2.level","fm.op.3.level","fm.op.4.level","fm.op.5.level","fm.op.6.level"};
constexpr int kFirst=85;
std::vector<Row> richRows() { return {{1,88,1.f},{1,90,0.6f},{2,102,0.5f},{1,107,-1.f},{2,97,0.8f},{1,86,0.7f}}; }
std::vector<Row> extremeRows(int variant) {
    std::vector<Row> rows;
    for(int i=0;i<16;++i) rows.push_back({1+(i%9),kFirst+((i*(variant==0?1:5)+(variant==0?0:3))%28),(i%2)?1.f:-1.f});
    return rows;
}

// ---- tests ----
void constantsAndNames() {
    require(aether::modulationTargetCount==113 && aether::modulationExtendedCount==28,"constants");
    const auto names=aether::modulationTargetNames();
    require(names.size()==113,"option list has 113 entries, got "+std::to_string(names.size()));
    require(names[0]=="None","index 0 is None");
    const char* const expectedNames[28]={"Source / Level","Source / Spread","Source / Transpose","Source / Walsh seq","Source / Walsh mix",
        "Source / CZ depth","Source / CZ env","Source / CZ velocity","Source / VPS D","Source / VPS V",
        "Source / VPS ratio","Source / Phase random","Source / Amp step","Source / Dur step","Source / Dur range","Source / Momentum",
        "Source / Amp barrier","FM / Mod index","FM / Morph","FM / Feedback","FM / Voice level","FM / Spread",
        "FM / Op 1 level","FM / Op 2 level","FM / Op 3 level","FM / Op 4 level","FM / Op 5 level","FM / Op 6 level"};
    for(int k=0;k<28;++k) {
        require(names[kFirst+k]==expectedNames[k],"name of destination "+std::to_string(kFirst+k)+": "+names[kFirst+k].toStdString());
        require(names[kFirst+k].startsWith("Source / ")||names[kFirst+k].startsWith("FM / "),"UI grouping prefix of destination "+std::to_string(kFirst+k));
        require(names[kFirst+k].length()<=24,"name of destination "+std::to_string(kFirst+k)+" longer than 24 characters");
        const auto& spec=aether::parameterSpecs()[static_cast<size_t>(aether::modulationExtendedParameter[static_cast<size_t>(k)])];
        require(spec.id==kIds[k],"destination "+std::to_string(kFirst+k)+" maps to "+spec.id.toStdString()+", expected "+kIds[k]);
    }
    // the parameter itself: choice with 113 options, raw index round-trips for 84 and 112
    Processor p;
    for(int n=1;n<=16;++n) {
        const auto id="modulation."+juce::String(n)+".target";
        put(p,id,112); require(p.parameterValue(id)==112.f,"raw 112 round trip");
        put(p,id,85); require(p.parameterValue(id)==85.f,"raw 85 round trip");
        put(p,id,84); require(p.parameterValue(id)==84.f,"raw 84 round trip");
    }
    require(aether::parameterSpecs().size()==static_cast<size_t>(aether::totalParameterCount),"parameter count unchanged");
}
void effective() {
    const auto in=noiseSine(48000);
    double minDiff=1e9; int minDest=0;
    for(int k=0;k<28;++k) {
        const int dest=kFirst+k;
        const bool isFm=k>=17,isPg=k>=3&&k<=11,isChaos=k>=12&&k<=16;
        struct Config { int engine; bool fm; } applicable[2]={{isFm?NoSource:(isChaos?Chaos:PhaseGeometry),isFm},{NoSource,false}};
        const int configs=(k<3)?2:1;
        if(k<3) { applicable[0]={PhaseGeometry,false}; applicable[1]={Chaos,false}; }
        for(int c=0;c<configs;++c) {
            const auto cfg=applicable[c];
            const auto ref=render(scenario(cfg.engine,cfg.fm,{{1,dest,0.f}}),in,notes(),480);
            require(peak(ref)>1e-3,"reference for "+std::string(kIds[k])+" is silent");
            for(const float depth:{1.f,-1.f}) {
                const auto mod=render(scenario(cfg.engine,cfg.fm,{{1,dest,depth}}),in,notes(),480);
                require(lastParametersUntouched,"host parameters were written back for destination "+std::to_string(dest));
                require(finiteAll(mod),"non finite output for destination "+std::to_string(dest));
                const double d=maxAbsDiff(mod,ref);
                if(d<minDiff) { minDiff=d; minDest=dest; }
                require(d>1.0e-4,std::string("destination ")+std::to_string(dest)+" ("+kIds[k]+") depth "+num(depth)+" engine "+std::to_string(cfg.engine)+" fm "+std::to_string(cfg.fm)+": max diff "+num(d)+" too small");
            }
        }
        // not applicable: the target engine is off -> bit-identical to the unmodulated render
        struct Off { int engine; bool fm; } notApplicable[2]; int offCount=0;
        if(isFm) { notApplicable[offCount++]={PhaseGeometry,false}; }
        else {
            notApplicable[offCount++]={NoSource,true};
            if(isPg) notApplicable[offCount++]={Chaos,false};
            if(isChaos) notApplicable[offCount++]={PhaseGeometry,false};
        }
        for(int c=0;c<offCount;++c) {
            const auto ref=render(scenario(notApplicable[c].engine,notApplicable[c].fm,{{1,dest,0.f}}),in,notes(),480);
            for(const float depth:{1.f,-1.f}) {
                const auto mod=render(scenario(notApplicable[c].engine,notApplicable[c].fm,{{1,dest,depth}}),in,notes(),480);
                require(identical(mod,ref),"destination "+std::to_string(dest)+" must not change anything with engine "+std::to_string(notApplicable[c].engine)+" fm "+std::to_string(notApplicable[c].fm)+" (diff "+num(maxAbsDiff(mod,ref))+")");
            }
        }
    }
    std::cout<<"    smallest effect over all 28 destinations: max |diff| "<<minDiff<<" at destination "<<minDest<<std::endl;
}
void depthZeroAndNoSourceIgnored() {
    const auto in=noiseSine(24000);
    for(int engine:{PhaseGeometry,Chaos}) {
        const auto ref=render(scenario(engine,true,{}),in,notes(),480);
        require(identical(render(scenario(engine,true,{{1,90,0.f},{2,102,0.f}}),in,notes(),480),ref),"depth 0 rows change nothing");
        require(identical(render(scenario(engine,true,{{0,90,1.f},{0,102,1.f}}),in,notes(),480),ref),"source None rows change nothing");
    }
}
void extremes() {
    const auto in=noiseSine(24000); double worst=0;
    for(int engine:{PhaseGeometry,Chaos}) for(int variant=0;variant<2;++variant) for(bool hot:{false,true}) for(int block:{1,7,64,480,512}) {
        const auto out=render(scenario(engine,true,extremeRows(variant),hot),in,notes(),block);
        const std::string what="engine "+std::to_string(engine)+" variant "+std::to_string(variant)+(hot?" hot":"")+" block "+std::to_string(block);
        require(finiteAll(out),"non finite output, "+what);
        require(lastParametersUntouched,"parameters written back, "+what);
        const double pk=peak(out); worst=std::max(worst,pk);
        require(pk<16.0,"output not bounded ("+num(pk)+"), "+what);
    }
    // FM only / Source only too
    for(int block:{1,64,512}) {
        require(finiteAll(render(scenario(NoSource,true,extremeRows(0),true),in,notes(),block)),"FM only extreme");
        require(finiteAll(render(scenario(PhaseGeometry,false,extremeRows(1),true),in,notes(),block)),"Source only extreme");
    }
    std::cout<<"    largest peak over the extreme matrix: "<<worst<<std::endl;
}
void blockIndependence() {
    const auto in=noiseSine(48000);
    struct Case { const char* name; Setup setup; };
    const std::vector<Case> cases{{"PG + FM, rich rows",scenario(PhaseGeometry,true,richRows())},{"Chaos + FM, rich rows",scenario(Chaos,true,richRows())},
        {"PG + FM, 16 extreme rows",scenario(PhaseGeometry,true,extremeRows(0),true)},{"FM only",scenario(NoSource,true,richRows())},{"PG only",scenario(PhaseGeometry,false,richRows())}};
    for(const auto& c:cases) for(const auto* script:{&notes(),&denseNotes()}) {
        const auto ref=render(c.setup,in,*script,512);
        require(peak(ref)>1e-3,std::string(c.name)+": reference silent");
        for(int block:{1,7,64,333}) {
            const auto out=render(c.setup,in,*script,block);
            require(identical(out,ref),std::string(c.name)+(script==&notes()?" notes":" dense MIDI")+": block "+std::to_string(block)+" differs from block 512 by "+num(maxAbsDiff(out,ref)));
        }
    }
    // the modulation really is active in those cases (otherwise independence would be trivial)
    require(maxAbsDiff(render(scenario(PhaseGeometry,true,richRows()),in,notes(),64),render(scenario(PhaseGeometry,true,{}),in,notes(),64))>1e-3,"rich rows change the render");
}
void determinism() {
    const auto in=noiseSine(48000);
    for(int engine:{NoSource,PhaseGeometry,Chaos}) {
        const auto a=render(scenario(engine,true,richRows()),in,denseNotes(),480),b=render(scenario(engine,true,richRows()),in,denseNotes(),480);
        require(identical(a,b),"two renders differ, engine "+std::to_string(engine));
    }
}
void tailUnaffected() {
    Processor a,b; scenario(PhaseGeometry,true,{})(a); scenario(PhaseGeometry,true,extremeRows(0),true)(b);
    require(a.getTailLengthSeconds()==b.getTailLengthSeconds(),"tail length does not depend on destinations 85..112");
}
size_t countAllocations(const Setup& setup,int blocks) {
    constexpr int block=480;
    Processor p; p.prepareToPlay(kRate,block); setup(p);
    juce::AudioBuffer<float> buffer(2,block); juce::MidiBuffer midi;
    const auto in=noiseSine(block*4);
    auto run=[&](int b) {
        const int off=(b%4)*block;
        for(int i=0;i<block;++i) { buffer.setSample(0,i,in.l[static_cast<size_t>(off+i)]); buffer.setSample(1,i,in.r[static_cast<size_t>(off+i)]); }
        midi.clear();
        if(b%7==0) midi.addEvent(juce::MidiMessage::noteOn(1,48+(b%24),0.7f),100);
        if(b%7==3) midi.addEvent(juce::MidiMessage::noteOff(1,48+((b-3)%24)),300);
        p.processBlock(buffer,midi);
    };
    for(int b=0;b<60;++b) run(b);                    // warm-up outside the window
    allocations=0; trackAllocations=true;
    for(int b=60;b<60+blocks;++b) run(b);
    trackAllocations=false;
    return allocations.load();
}
void noAllocation() {
    for(int engine:{NoSource,PhaseGeometry,Chaos}) {
        const auto extended=countAllocations(scenario(engine,true,extremeRows(1),true),400);
        const auto legacyRows=countAllocations(scenario(engine,true,{{1,3,1.f},{2,10,0.5f}}),400);
        std::cout<<"    engine "<<engine<<": allocations in 400 blocks - new path "<<extended<<", old path "<<legacyRows<<std::endl;
        require(extended==0,"allocations on the new path, engine "+std::to_string(engine)+": "+std::to_string(extended));
        require(legacyRows==0,"allocations on the old path, engine "+std::to_string(engine)+": "+std::to_string(legacyRows));
    }
}
// ---- CPU ----
double timeScenario(const Setup& setup,int voices) {
    constexpr int block=480,warm=100,blocks=1500;
    const auto in=noiseSine(block*8);
    double best=1e9;
    for(int repeat=0;repeat<3;++repeat) {
        Processor p; p.prepareToPlay(kRate,block); setup(p);
        juce::AudioBuffer<float> buffer(2,block); juce::MidiBuffer midi;
        auto run=[&](int b) {
            const int off=(b%8)*block;
            for(int i=0;i<block;++i) { buffer.setSample(0,i,in.l[static_cast<size_t>(off+i)]); buffer.setSample(1,i,in.r[static_cast<size_t>(off+i)]); }
            midi.clear();
            if(b==0) for(int v=0;v<voices;++v) midi.addEvent(juce::MidiMessage::noteOn(1,40+v*2,0.7f),v);
            p.processBlock(buffer,midi);
        };
        for(int b=0;b<warm;++b) run(b);
        const auto t0=std::chrono::steady_clock::now();
        for(int b=warm;b<warm+blocks;++b) run(b);
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
        best=std::min(best,100.0*seconds/(blocks*block/kRate));
    }
    return best;
}
// ---- saved state: schema 3 unless a motion row uses destination 85..112, then 4 (state XML and .json presets) ----
int stateSchemaOf(Processor& p) {
    juce::MemoryBlock b; p.getStateInformation(b);
    const auto xml=juce::AudioProcessor::getXmlFromBinary(b.getData(),static_cast<int>(b.getSize())); require(xml!=nullptr,"state is not xml");
    return xml->getIntAttribute("schema");
}
juce::MemoryBlock stateStamped(Processor& p,int schema) {
    juce::MemoryBlock b; p.getStateInformation(b);
    auto xml=juce::AudioProcessor::getXmlFromBinary(b.getData(),static_cast<int>(b.getSize())); require(xml!=nullptr,"state is not xml");
    xml->setAttribute("schema",schema); juce::MemoryBlock out; juce::AudioProcessor::copyXmlToBinary(*xml,out); return out;
}
juce::var presetVar(Processor& p) {
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("aether-modtargets-schema",".aether");
    require(p.savePreset(file).wasOk(),"savePreset failed"); juce::var root; const auto r=juce::JSON::parse(file.loadFileAsString(),root); file.deleteFile();
    require(r.wasOk() && root.isObject(),"saved preset is not json"); return root;
}
bool presetLoads(Processor& p,juce::var root,int schema) {
    root.getDynamicObject()->setProperty("schema",schema);
    const auto file=juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("aether-modtargets-schema",".aether");
    file.replaceWithText(juce::JSON::toString(root,false)); const bool ok=p.loadPreset(file).wasOk(); file.deleteFile(); return ok;
}
void savedSchema() {
    { Processor p; require(stateSchemaOf(p)==3,"default state is not schema 3"); require(static_cast<int>(presetVar(p)["schema"])==3,"default preset is not schema 3"); }
    for(int target:{0,1,40,84}) {
        Processor p; put(p,"modulation.1.target",static_cast<float>(target)); put(p,"modulation.1.source",3); put(p,"modulation.1.depth",0.5f);
        require(stateSchemaOf(p)==3,"state with target "+std::to_string(target)+" is not schema 3");
        require(static_cast<int>(presetVar(p)["schema"])==3,"preset with target "+std::to_string(target)+" is not schema 3");
    }
    for(int row:{1,9,16}) for(int target:{85,86,101,102,112}) {
        Processor p; put(p,"modulation."+juce::String(row)+".target",static_cast<float>(target));
        put(p,"modulation."+juce::String(row)+".source",2); put(p,"modulation."+juce::String(row)+".depth",-0.25f);
        const std::string what="row "+std::to_string(row)+" target "+std::to_string(target);
        require(stateSchemaOf(p)==4,"state with "+what+" is not schema 4");
        const auto preset=presetVar(p); require(static_cast<int>(preset["schema"])==4,"preset with "+what+" is not schema 4");
        { Processor q; const auto state=stateStamped(p,4); q.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
          require(static_cast<int>(q.parameterValue("modulation."+juce::String(row)+".target"))==target,"state round trip lost "+what);
          require(std::abs(q.parameterValue("modulation."+juce::String(row)+".depth")+0.25f)<1.0e-5f,"state round trip lost depth of "+what);
          require(stateSchemaOf(q)==4,"re-saved state is not schema 4 for "+what); }
        { Processor q; require(presetLoads(q,preset,4),"schema 4 preset refused: "+what);
          require(static_cast<int>(q.parameterValue("modulation."+juce::String(row)+".target"))==target,"preset round trip lost "+what); }
    }
    // The A/B snapshot counts too: the other slot may hold a destination 85..112 while the live one does not, and the other way round.
    { Processor p; put(p,"modulation.2.target",100); p.toggleAB(); put(p,"modulation.2.target",0);
      require(stateSchemaOf(p)==4,"state whose A/B snapshot uses 85..112 is not schema 4");
      p.toggleAB(); put(p,"modulation.2.target",0); p.toggleAB(); put(p,"modulation.2.target",0);
      require(stateSchemaOf(p)==3,"state without any destination 85..112 is not schema 3 after toggling"); }
    { Processor p; put(p,"modulation.2.target",100); p.toggleAB(); require(stateSchemaOf(p)==4,"live and snapshot both use 85..112"); }
    // Reading: schema 1..4 accepted, 0 / 5 / 999 / -1 refused without changing anything.
    { Processor src; put(src,"colour.drive",3); put(src,"modulation.3.target",100); put(src,"modulation.3.source",2);
      const auto preset=presetVar(src);
      for(int schema=-1;schema<=999;schema=schema==5?999:schema+1) {
          const bool accepted=schema>=1&&schema<=4;
          { Processor q; put(q,"colour.drive",17); put(q,"modulation.3.target",5);
            const auto state=stateStamped(src,schema); q.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
            if(accepted) require(q.parameterValue("colour.drive")==3 && static_cast<int>(q.parameterValue("modulation.3.target"))==100,"state schema "+std::to_string(schema)+" not read");
            else require(q.parameterValue("colour.drive")==17 && static_cast<int>(q.parameterValue("modulation.3.target"))==5,"state schema "+std::to_string(schema)+" was accepted"); }
          { Processor q; put(q,"colour.drive",17); put(q,"modulation.3.target",5); const bool loaded=presetLoads(q,preset,schema);
            require(loaded==accepted,"preset schema "+std::to_string(schema)+(accepted?" refused":" accepted"));
            if(accepted) require(q.parameterValue("colour.drive")==3 && static_cast<int>(q.parameterValue("modulation.3.target"))==100,"preset schema "+std::to_string(schema)+" not read");
            else require(q.parameterValue("colour.drive")==17 && static_cast<int>(q.parameterValue("modulation.3.target"))==5,"refused preset schema "+std::to_string(schema)+" changed parameters"); }
      } }
}int bench() {
    struct Case { const char* name; int engine; bool fm; std::vector<int> targets; };
    const std::vector<Case> cases{{"Phase Geometry only",PhaseGeometry,false,{85,86,88,89,90,91,93,95}},{"Chaos only",Chaos,false,{85,86,97,98,99,100,101,87}},
        {"FM only",NoSource,true,{102,103,104,105,107,108,109,110}},{"Phase Geometry + FM",PhaseGeometry,true,{85,88,90,93,102,103,104,107}},{"Chaos + FM",Chaos,true,{85,97,99,100,102,103,104,107}}};
    std::cout<<"form "<<(kSynth?"Synth":"Fx")<<", 16 held notes, 480-sample blocks, 48 kHz, best of 3 x 15 s; value = % of one core in real time\n";
    for(const auto& c:cases) {
        std::vector<Row> oldRows,newRows;
        for(size_t i=0;i<c.targets.size();++i) { oldRows.push_back({static_cast<int>(1+i%4),static_cast<int>(3+7*i),0.3f}); newRows.push_back({static_cast<int>(1+i%4),c.targets[i],0.3f}); }
        const double none=timeScenario(scenario(c.engine,c.fm,{}),16),old=timeScenario(scenario(c.engine,c.fm,oldRows),16),ext=timeScenario(scenario(c.engine,c.fm,newRows),16);
        std::printf("  %-22s no rows %6.2f%%   8 rows on nodes 1..84 %6.2f%%   8 rows on 85..112 %6.2f%%   (new vs none %+.2f%%, x%.2f)\n",c.name,none,old,ext,ext-none,ext/none);
    }
    return 0;
}
} // namespace

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI juceInit;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--group" && i+1<argc) selectedGroup=argv[++i];
        else if(arg=="--list") selectedGroup="list";
        else if(arg=="--bench") return bench();
    }
    if(kSynth!=(AETHER_EXPECT_SYNTH!=0)) { std::cout<<"FAIL wrong plugin form linked\n"; return 2; }
    test("constants, names and destination->parameter table",constantsAndNames);
    test("each of 85..112 is effective, not applicable combinations unchanged, no write-back",effective);
    test("depth 0 / source None rows change nothing",depthZeroAndNoSourceIgnored);
    test("extreme: 16 rows, +-1, all sources, both engines + FM, several block sizes",extremes);
    test("block size independence 1/7/64/333/512 with mid-block MIDI (bit identical)",blockIndependence);
    test("determinism (bit identical)",determinism);
    test("tail length independent of destinations 85..112",tailUnaffected);
    test("zero allocation after prepare (new and old path)",noAllocation);
    test("saved state: schema 3, or 4 when a row uses 85..112; reads 1..4, refuses 0/5",savedSchema);
    if(selectedGroup=="list") return 0;
    std::cout<<(kSynth?"[Synth form] ":"[Fx form] ")<<passed<<" passed, "<<failed<<" failed\n";
    return failed==0?0:1;
}
