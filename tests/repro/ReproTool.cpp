// Investigation tool for the graph/FM-path render non-reproducibility (branch test/render-repro-investigation).
// Not a fix and not a regression test. Usage (all output on stdout):
//   AetherReproTool plugin <sampleSubstr> [--block N] [--count N] [--set id=value]... [--nomidi] [--noinput] [--warm K]
//        in-process: render N times, compare each run with run 0
//   AetherReproTool dump <sampleSubstr> --out file [--block N] [--set ...] [--nomidi] [--noinput]
//        one render in this (fresh) process -> raw float file (L then R)
//   AetherReproTool cmp a.bin b.bin          first difference between two dumps
//   AetherReproTool graphmod <typeIndex|all> [--block N] [--count N] [--fresh-dump file]
//        GraphEngine alone, slot 0 = module type, in/out gain 1, in-process repeat
//   AetherReproTool fm [--block N] [--count N]   fm::Synth alone
// Environment: AETHER_HEAPFILL=<hex byte>|rand  fills every operator-new block on allocation
//              AETHER_HEAPFILL_FREE=1           also fills blocks on delete
//              AETHER_HEAPSEED=<n>              seed for the 'rand' fill
#include "PluginProcessor.h"
#include <aether/FmSynth.h>
#include <aether/Modular.h>
#include <juce_cryptography/juce_cryptography.h>
#define _CRT_SECURE_NO_WARNINGS 1
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <map>
#include <new>
#include <string>
#include <malloc.h>

// ---------------------------------------------------------------- heap poison
namespace heappoison {
int fillByte=-1; bool fillRandom=false, fillFree=false; unsigned long long rng=88172645463325252ull; bool inited=false;
void init() {
    inited=true;
    if(const char* v=std::getenv("AETHER_HEAPFILL")) {
        if(std::strcmp(v,"rand")==0) fillRandom=true; else fillByte=static_cast<int>(std::strtol(v,nullptr,16))&0xff;
    }
    if(const char* v=std::getenv("AETHER_HEAPFILL_FREE")) fillFree=v[0]=='1';
    if(const char* v=std::getenv("AETHER_HEAPSEED")) rng^=static_cast<unsigned long long>(std::strtoll(v,nullptr,10)+1)*0x9E3779B97F4A7C15ull;
}
void fill(void* p,size_t n) {
    if(!inited) init();
    if(!p) return;
    if(fillRandom) { auto* b=static_cast<unsigned char*>(p); for(size_t i=0;i<n;++i) { rng^=rng<<13; rng^=rng>>7; rng^=rng<<17; b[i]=static_cast<unsigned char>(rng>>32); } }
    else if(fillByte>=0) std::memset(p,fillByte,n);
}
void* allocate(size_t n,size_t align) {
    void* p=align>16?_aligned_malloc(n?n:1,align):std::malloc(n?n:1);
    if(!p) throw std::bad_alloc();
    fill(p,n); return p;
}
void release(void* p,size_t align) {
    if(!p) return;
    if(!inited) init();
    if(fillFree) { const size_t n=align>16?_aligned_msize(p,align,0):_msize(p); fill(p,n); }
    if(align>16) _aligned_free(p); else std::free(p);
}
}
void* operator new(size_t n) { return heappoison::allocate(n,0); }
void* operator new[](size_t n) { return heappoison::allocate(n,0); }
void* operator new(size_t n,const std::nothrow_t&) noexcept { try { return heappoison::allocate(n,0); } catch(...) { return nullptr; } }
void* operator new[](size_t n,const std::nothrow_t&) noexcept { try { return heappoison::allocate(n,0); } catch(...) { return nullptr; } }
void* operator new(size_t n,std::align_val_t a) { return heappoison::allocate(n,static_cast<size_t>(a)); }
void* operator new[](size_t n,std::align_val_t a) { return heappoison::allocate(n,static_cast<size_t>(a)); }
void operator delete(void* p) noexcept { heappoison::release(p,0); }
void operator delete[](void* p) noexcept { heappoison::release(p,0); }
void operator delete(void* p,size_t) noexcept { heappoison::release(p,0); }
void operator delete[](void* p,size_t) noexcept { heappoison::release(p,0); }
void operator delete(void* p,std::align_val_t a) noexcept { heappoison::release(p,static_cast<size_t>(a)); }
void operator delete[](void* p,std::align_val_t a) noexcept { heappoison::release(p,static_cast<size_t>(a)); }
void operator delete(void* p,size_t,std::align_val_t a) noexcept { heappoison::release(p,static_cast<size_t>(a)); }
void operator delete[](void* p,size_t,std::align_val_t a) noexcept { heappoison::release(p,static_cast<size_t>(a)); }

namespace {
constexpr double kRate=48000.0;
constexpr int kFrames=96000;
struct Audio { std::vector<float> l,r; };
struct Options {
    int block=256,count=10,warm=0; bool noMidi=false,noInput=false; std::string out; std::vector<std::pair<std::string,float>> sets;
};
juce::File fixtureDir() { return juce::File(AETHER_SOURCE_DIR).getChildFile("tests").getChildFile("fixtures"); }
struct Sample { juce::String id; bool isPreset; juce::File file; };
std::vector<Sample> collectSamples() {
    std::vector<Sample> out;
    auto gather=[&](const char* folder,const char* pattern,bool isPreset,const char* prefix) {
        std::vector<juce::File> files;
        for(const auto& f:fixtureDir().getChildFile(folder).findChildFiles(juce::File::findFiles,false,pattern)) files.push_back(f);
        std::sort(files.begin(),files.end(),[](const juce::File& a,const juce::File& b){return a.getFileName()<b.getFileName();});
        for(const auto& f:files) out.push_back({juce::String(prefix)+f.getFileNameWithoutExtension(),isPreset,f});
    };
    gather("presets-v1.0.0","*.aether",true,"preset-v1.0.0:");
    gather("state-v2.0.1","*.bin",false,"state-v2.0.1:");
    return out;
}
Sample findSample(const std::string& needle) {
    for(const auto& s:collectSamples()) if(s.id.contains(needle)) return s;
    std::cerr<<"no sample matches "<<needle<<'\n'; std::exit(2);
}
juce::String loadSample(AetherAudioProcessor& p,const Sample& s) {
    if(s.isPreset) { const auto r=p.loadPreset(s.file); return r.wasOk()?juce::String():r.getErrorMessage(); }
    juce::MemoryBlock data; if(!s.file.loadFileAsData(data)||data.getSize()==0) return "cannot read state file";
    p.setStateInformation(data.getData(),static_cast<int>(data.getSize())); return {};
}
const Audio& testInput() {
    static const Audio input=[] {
        Audio a; a.l.resize(kFrames); a.r.resize(kFrames);
        juce::Random rng(20261024);
        for(int i=0;i<kFrames;++i) {
            const double t=i/kRate;
            a.l[static_cast<size_t>(i)]=static_cast<float>(0.20*std::sin(2*juce::MathConstants<double>::pi*220*t)+0.05*(rng.nextDouble()*2-1));
            a.r[static_cast<size_t>(i)]=static_cast<float>(0.20*std::sin(2*juce::MathConstants<double>::pi*330*t)+0.05*(rng.nextDouble()*2-1));
        }
        return a;
    }();
    return input;
}
const std::vector<std::pair<int,juce::MidiMessage>>& midiScript() {
    static const std::vector<std::pair<int,juce::MidiMessage>> script{
        {1000,juce::MidiMessage::noteOn(1,60,0.8f)},{12000,juce::MidiMessage::noteOn(1,67,0.6f)},
        {30000,juce::MidiMessage::noteOff(1,60)},{40000,juce::MidiMessage::noteOff(1,67)}};
    return script;
}
Audio renderPlugin(const Sample& s,const Options& o) {
    AetherAudioProcessor p; p.prepareToPlay(kRate,o.block);
    const auto error=loadSample(p,s); if(error.isNotEmpty()) { std::cerr<<"load failed: "<<error<<'\n'; std::exit(2); }
    for(const auto& kv:o.sets) p.setParameterValue(kv.first,kv.second);
    Audio out; out.l.resize(kFrames); out.r.resize(kFrames);
    const auto& in=testInput();
    for(int offset=0;offset<kFrames;offset+=o.block) {
        const int n=std::min(o.block,kFrames-offset);
        juce::AudioBuffer<float> buffer(2,n); juce::MidiBuffer midi;
        if(o.noInput) buffer.clear(); else { std::copy_n(in.l.data()+offset,n,buffer.getWritePointer(0)); std::copy_n(in.r.data()+offset,n,buffer.getWritePointer(1)); }
        if(!o.noMidi) for(const auto& event:midiScript()) if(event.first>=offset && event.first<offset+n) midi.addEvent(event.second,event.first-offset);
        p.processBlock(buffer,midi);
        std::copy_n(buffer.getReadPointer(0),n,out.l.data()+offset); std::copy_n(buffer.getReadPointer(1),n,out.r.data()+offset);
    }
    return out;
}
std::string sha(const Audio& a) {
    juce::MemoryBlock bytes; bytes.append(a.l.data(),a.l.size()*sizeof(float)); bytes.append(a.r.data(),a.r.size()*sizeof(float));
    return juce::SHA256(bytes.getData(),bytes.getSize()).toHexString().substring(0,12).toStdString();
}
struct Diff { double maxdiff=0; size_t first=0,count=0,maxAt=0; int channel=0; bool any=false; float a=0,b=0; };
Diff compare(const Audio& x,const Audio& y) {
    Diff d;
    for(size_t i=0;i<x.l.size();++i) for(int c=0;c<2;++c) {
        const float p=c?x.r[i]:x.l[i], q=c?y.r[i]:y.l[i];
        if(std::memcmp(&p,&q,sizeof(float))==0) continue;
        if(!d.any) { d.any=true; d.first=i; d.channel=c; d.a=p; d.b=q; }
        ++d.count; const double v=std::abs(double(p)-double(q)); if(!(v<=d.maxdiff)) { d.maxdiff=v; d.maxAt=i; }
    }
    return d;
}
void printDiff(const char* label,const Diff& d) {
    if(!d.any) { std::cout<<label<<" identical\n"; return; }
    std::cout<<label<<" DIFF first="<<d.first<<" ("<<d.first*1000.0/kRate<<" ms) ch="<<(d.channel?"R":"L")<<" values "<<d.a<<" vs "<<d.b<<" ndiff="<<d.count<<" maxdiff="<<d.maxdiff<<" at="<<d.maxAt<<'\n';
}
void saveAudio(const Audio& a,const std::string& path) {
    FILE* f=std::fopen(path.c_str(),"wb"); if(!f) { std::cerr<<"cannot write "<<path<<'\n'; std::exit(2); }
    std::fwrite(a.l.data(),sizeof(float),a.l.size(),f); std::fwrite(a.r.data(),sizeof(float),a.r.size(),f); std::fclose(f);
}
Audio loadAudio(const std::string& path) {
    Audio a; a.l.resize(kFrames); a.r.resize(kFrames);
    FILE* f=std::fopen(path.c_str(),"rb"); if(!f) { std::cerr<<"cannot read "<<path<<'\n'; std::exit(2); }
    if(std::fread(a.l.data(),sizeof(float),kFrames,f)!=size_t(kFrames)||std::fread(a.r.data(),sizeof(float),kFrames,f)!=size_t(kFrames)) { std::cerr<<"short file "<<path<<'\n'; std::exit(2); }
    std::fclose(f); return a;
}
// -------- standalone GraphEngine / fm::Synth renders
Audio renderGraphModule(int type,int block) {
    aether::modular::GraphEngine g; g.prepare(kRate,block);
    aether::modular::Parameters p; aether::modular::assignModule(p,0,static_cast<aether::modular::ModuleType>(type));
    p.input[0]=1; p.output[0]=1; g.setParameters(p); g.setPerformance(0.8f,220.f);
    Audio out; out.l.resize(kFrames); out.r.resize(kFrames); const auto& in=testInput();
    for(int offset=0;offset<kFrames;offset+=block) {
        const int n=std::min(block,kFrames-offset);
        std::copy_n(in.l.data()+offset,n,out.l.data()+offset); std::copy_n(in.r.data()+offset,n,out.r.data()+offset);
        g.process(out.l.data()+offset,out.r.data()+offset,nullptr,n);
    }
    return out;
}
Audio renderFm(int block) {
    aether::fm::Synth s; s.prepare(kRate,block); aether::fm::Parameters p; p.enabled=true; s.setParameters(p);
    Audio out; out.l.resize(kFrames); out.r.resize(kFrames);
    for(int offset=0;offset<kFrames;offset+=block) {
        const int n=std::min(block,kFrames-offset);
        for(const auto& event:midiScript()) if(event.first>=offset && event.first<offset+n) { if(event.second.isNoteOn()) s.noteOn(event.second.getNoteNumber(),event.second.getFloatVelocity()); else s.noteOff(event.second.getNoteNumber()); }
        s.process(out.l.data()+offset,out.r.data()+offset,n);
    }
    return out;
}
template<class F> void repeat(const char* label,int count,F render) {
    const auto first=render(); std::cout<<label<<" run0 sha="<<sha(first)<<'\n';
    int differing=0; std::map<std::string,int> hashes; hashes[sha(first)]=1;
    for(int k=1;k<count;++k) {
        const auto again=render(); const auto d=compare(first,again); hashes[sha(again)]++;
        if(d.any) { ++differing; std::cout<<label<<" run"<<k<<" sha="<<sha(again)<<' '; printDiff("vs run0:",d); }
    }
    std::cout<<label<<" SUMMARY runs="<<count<<" differing_from_run0="<<differing<<" distinct_hashes="<<hashes.size()<<'\n';
}
}

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    std::vector<std::string> args(argv+1,argv+argc);
    if(args.empty()) { std::cerr<<"see header comment\n"; return 2; }
    Options o; std::vector<std::string> pos;
    for(size_t i=1;i<args.size();++i) {
        const auto& a=args[i];
        if(a=="--block") o.block=std::atoi(args[++i].c_str());
        else if(a=="--count") o.count=std::atoi(args[++i].c_str());
        else if(a=="--warm") o.warm=std::atoi(args[++i].c_str());
        else if(a=="--nomidi") o.noMidi=true;
        else if(a=="--noinput") o.noInput=true;
        else if(a=="--out") o.out=args[++i];
        else if(a=="--set") { const auto eq=args[++i].find('='); const auto& kv=args[i]; o.sets.push_back({kv.substr(0,eq),static_cast<float>(std::atof(kv.c_str()+eq+1))}); }
        else pos.push_back(a);
    }
    const auto& mode=args[0];
    if(mode=="plugin" && !pos.empty()) {
        const auto s=findSample(pos[0]);
        std::cout<<"sample "<<s.id<<" block "<<o.block<<" count "<<o.count<<" heapfill-env "<<(std::getenv("AETHER_HEAPFILL")?std::getenv("AETHER_HEAPFILL"):"none")<<'\n';
        for(int w=0;w<o.warm;++w) renderPlugin(collectSamples()[static_cast<size_t>(w)%collectSamples().size()],o);
        repeat(s.id.toRawUTF8(),o.count,[&] { return renderPlugin(s,o); });
        return 0;
    }
    if(mode=="dump" && !pos.empty() && !o.out.empty()) {
        const auto a=renderPlugin(findSample(pos[0]),o); saveAudio(a,o.out); std::cout<<"sha="<<sha(a)<<'\n'; return 0;
    }
    if(mode=="cmp" && pos.size()>=1 && args.size()>=3) { printDiff("cmp:",compare(loadAudio(args[1]),loadAudio(args[2]))); return 0; }
    if(mode=="graphmod") {
        const std::string which=pos.empty()?"all":pos[0];
        if(!o.out.empty()) { saveAudio(renderGraphModule(std::atoi(which.c_str()),o.block),o.out); std::cout<<"saved\n"; return 0; }
        const int last=static_cast<int>(aether::modular::ModuleType::Count);
        for(int t=0;t<last;++t) {
            if(which!="all" && std::atoi(which.c_str())!=t) continue;
            const std::string label=std::string("graphmod ")+std::to_string(t)+" "+aether::modular::moduleInfo(static_cast<aether::modular::ModuleType>(t)).name;
            repeat(label.c_str(),o.count,[&] { return renderGraphModule(t,o.block); });
        }
        return 0;
    }
    if(mode=="seq") {
        // seq <fwd|rev|shuffle:SEED> [--count rounds] [--block N] [--only substr,substr]: render the samples one after another in ONE process; print sha per render
        const std::string order=pos.empty()?"fwd":pos[0];
        auto samples=collectSamples();
        if(order=="rev") std::reverse(samples.begin(),samples.end());
        else if(order.rfind("shuffle:",0)==0) { unsigned long long r=std::strtoull(order.c_str()+8,nullptr,10)*2654435761ull+12345; for(size_t i=samples.size()-1;i>0;--i) { r^=r<<13; r^=r>>7; r^=r<<17; std::swap(samples[i],samples[r%(i+1)]); } }
        for(int round=0;round<o.count;++round) for(const auto& s:samples) {
            const auto a=renderPlugin(s,o); std::cout<<"round "<<round<<" "<<s.id<<" sha="<<sha(a)<<'\n';
            if(!o.out.empty()) saveAudio(a,o.out+"."+std::to_string(round)+"."+s.id.toStdString()+".bin");
        }
        return 0;
    }    if(mode=="spacecalc") {
        // Re-evaluates, with the same float expressions as libs/aether-modular/src/ModuleState.h, whether Delay::read can index data[size] for the Space module's 16 delay lines.
        const auto& info=aether::modular::moduleInfo(aether::modular::ModuleType::Space);
        const float macroDefault=pos.empty()?0.4f:static_cast<float>(std::atof(pos[0].c_str()));
        const float sr=48000.f; const int size=int(sr*.26f)+4;
        const float v0=info.macros[0].minimum+macroDefault*(info.macros[0].maximum-info.macros[0].minimum);
        static constexpr float ratios[]{.493f,.527f,.563f,.601f,.643f,.677f,.719f,.751f,.787f,.823f,.859f,.887f,.919f,.947f,.977f,1.f};
        std::cout<<"size macro="<<macroDefault<<" v0="<<v0<<" ms; delay buffer size="<<size<<"\n";
        int bad=0;
        for(int i=0;i<16;++i) {
            const float rt=v0*sr*.001f*ratios[i];
            const float delay=std::clamp(rt,1.f,float(size-2));
            for(int write=0;write<size;++write) {
                float p=float(write)-delay; if(p<0) p+=float(size);
                const int idx=int(p);
                if(idx<0||idx>=size) { ++bad; std::cout<<"line "<<i<<" reverbTime="<<std::setprecision(10)<<rt<<" write="<<write<<" p="<<p<<" index="<<idx<<" (size="<<size<<") -> data["<<idx<<"] is out of bounds\n"; }
            }
        }
        std::cout<<"out-of-bounds (line,write) combinations: "<<bad<<'\n'; return 0;
    }    if(mode=="fm" && !o.out.empty()) { saveAudio(renderFm(o.block),o.out); std::cout<<"saved\n"; return 0; }
    if(mode=="fm") { repeat("fm::Synth alone",o.count,[&] { return renderFm(o.block); }); return 0; }
    std::cerr<<"bad arguments\n"; return 2;
}
