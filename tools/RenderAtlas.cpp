#include <aether/Modular.h>
#include <aether/FmSynth.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <vector>
namespace {
constexpr int rate=48000;
void word(std::ostream& s,std::uint32_t v,int bytes){for(int i=0;i<bytes;++i)s.put(static_cast<char>((v>>(8*i))&255));}
void wav(const std::filesystem::path& path,const std::vector<float>& l,const std::vector<float>& r){
    std::ofstream s(path,std::ios::binary);const auto n=static_cast<std::uint32_t>(l.size()*4);
    s.write("RIFF",4);word(s,36+n,4);s.write("WAVEfmt ",8);word(s,16,4);word(s,1,2);word(s,2,2);word(s,rate,4);word(s,rate*4,4);word(s,4,2);word(s,16,2);s.write("data",4);word(s,n,4);
    double energy=0;float peak=0;
    for(size_t i=0;i<l.size();++i)for(float v:{l[i],r[i]}){if(!std::isfinite(v))throw std::runtime_error("Non-finite rendered audio");peak=std::max(peak,std::abs(v));energy+=v*v;word(s,static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(v,-1.f,1.f)*32767)),2);}
    if(!s)throw std::runtime_error("WAV write failed");std::cout<<path.filename().string()<<" samples="<<l.size()<<" peak="<<peak<<" rms="<<std::sqrt(energy/(2*l.size()))<<'\n';
}
void setup(aether::modular::Parameters& p,const std::vector<aether::modular::ModuleType>& modules,int algorithm){
    for(int i=0;i<12;++i){aether::modular::assignModule(p,i,i<static_cast<int>(modules.size())?modules[static_cast<size_t>(i)]:aether::modular::ModuleType::Thru);p.slots[static_cast<size_t>(i)].mix=i<static_cast<int>(modules.size())?0.5f:1;}
    aether::modular::applyAlgorithm(algorithm,p);p.trimDb=-3;
}
}
int main(int argc,char** argv){
    try{
        const auto dir=std::filesystem::path(argc>1?argv[1]:"out/atlas-audio");std::filesystem::create_directories(dir);
        std::ofstream catalog(dir/"Atlas-module-catalog.json");catalog<<"{\n  \"version\": \"2.0.0\",\n  \"modules\": [\n";
        for(int i=0;i<static_cast<int>(aether::modular::ModuleType::Count);++i) {
            const auto& info=aether::modular::moduleInfo(static_cast<aether::modular::ModuleType>(i));
            catalog<<"    {\"type\": "<<i<<", \"name\": "<<std::quoted(info.name)<<", \"category\": "<<std::quoted(info.category)<<", \"provenance\": "<<std::quoted(info.provenance)<<", \"macros\": [";
            for(int m=0;m<4;++m) {const auto& macro=info.macros[static_cast<size_t>(m)];catalog<<"{\"name\": "<<std::quoted(macro.name)<<", \"unit\": "<<std::quoted(macro.unit)<<", \"min\": "<<macro.minimum<<", \"max\": "<<macro.maximum<<", \"default_normalized\": "<<macro.defaultValue<<"}"<<(m<3?", ":"");}
            catalog<<"]}"<<(i+1<static_cast<int>(aether::modular::ModuleType::Count)?",":"")<<'\n';
        }
        catalog<<"  ],\n  \"audio_algorithms\": [";
        for(int i=0;i<32;++i)catalog<<std::quoted(aether::modular::algorithmName(i))<<(i<31?", ":"");
        catalog<<"],\n  \"fm_algorithms\": [";for(int i=0;i<32;++i)catalog<<std::quoted(aether::fm::algorithm(i).name)<<(i<31?", ":"");catalog<<"]\n}\n";
        std::vector<float> sourceL(rate*8),sourceR(sourceL.size());
        for(size_t i=0;i<sourceL.size();++i){double t=static_cast<double>(i)/rate,b=std::fmod(t,0.5);sourceL[i]=static_cast<float>(.22*std::exp(-b*9)*(std::sin(t*1382.3007676)+.35*std::sin(t*4146.9023)));sourceR[i]=sourceL[i]*.93f;}
        wav(dir/"01-Atlas-source.wav",sourceL,sourceR);
        using T=aether::modular::ModuleType;
        const std::vector<std::vector<T>> patches{{T::WdfDiode,T::Resonator,T::Chorus,T::Space},{T::GrainCloud,T::Ladder,T::Diffuser,T::Space},{T::PrismPitch,T::FrequencyShift,T::Tape,T::Space},{T::Wavefolder,T::Chaos,T::Comb,T::Diffuser},{T::SpectralFreeze,T::PrismPitch,T::Space}};
        const char* names[]={"02-Diode-garden.wav","03-Grain-cathedral.wav","04-Prism-sidebands.wav","05-Feedback-collisions.wav","06-Frozen-architecture.wav"};
        for(size_t k=0;k<patches.size();++k){
            auto l=sourceL,r=sourceR;aether::modular::GraphEngine graph;aether::modular::Parameters p;setup(p,patches[k],static_cast<int>(k));graph.prepare(rate,128);graph.setParameters(p);
            for(size_t i=0;i<l.size();i+=128){
                if(k==4 && i>=static_cast<size_t>(rate*2)){p.slots[0].macro[2]=1;graph.setParameters(p);}
                graph.process(l.data()+i,r.data()+i,nullptr,static_cast<int>(std::min<size_t>(128,l.size()-i)));
            }wav(dir/names[k],l,r);
        }
        std::vector<float> l(sourceL.size()),r(l.size());aether::fm::Synth fm;aether::fm::Parameters fp;fp.algorithm=18;fp.index=3.5f;fp.release=1.2f;fp.sustain=.3f;const float ratios[]={1,2,3.5f,4,0.5f,7};for(int i=0;i<6;++i)fp.operators[static_cast<size_t>(i)].ratio=ratios[i];fm.prepare(rate,128);fm.setParameters(fp);
        const int notes[]={48,55,60,64,67,72,67,60};
        for(int second=0;second<8;++second){fm.noteOn(notes[second],0.7f);for(int offset=0;offset<rate;offset+=128){if(offset>=rate/2)fm.noteOff(notes[second]);fm.process(l.data()+second*rate+offset,r.data()+second*rate+offset,std::min(128,rate-offset));}}
        wav(dir/"07-FM-inverted-bells.wav",l,r);
        // Continuous audio-driven FM demonstrates Resynth source control separately from effects.
        fm.reset();fp.algorithm=6;fp.index=2;fm.setParameters(fp);
        for(size_t i=0;i<l.size();i+=128){const float t=static_cast<float>(i)/rate;fm.setAudioExcitation(220.f*std::exp2(.5f*std::sin(t)),.35f+.2f*std::sin(t*3));fm.process(l.data()+i,r.data()+i,static_cast<int>(std::min<size_t>(128,l.size()-i)));}
        wav(dir/"08-Audio-driven-FM.wav",l,r);
        for(int algorithm:{0,3,14,25}){
            aether::modular::GraphEngine graph;aether::modular::Parameters p;setup(p,{T::Spectral,T::WdfDiode,T::Resonator,T::Colour,T::Comb,T::Ladder,T::Chorus,T::Tape,T::Diffuser,T::Space,T::LoFi,T::RingMod},algorithm);graph.prepare(rate,64);graph.setParameters(p);
            std::vector<float> bl(64),br(64);double seconds=0;
            for(int k=0;k<1500;++k){for(int i=0;i<64;++i)bl[static_cast<size_t>(i)]=br[static_cast<size_t>(i)]=.1f*std::sin(.03f*static_cast<float>(i+k*64));const auto start=std::chrono::steady_clock::now();graph.process(bl.data(),br.data(),nullptr,64);seconds+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();}
            std::cout<<"BENCH 12node graph="<<algorithm<<" stereo48000 block64 audio_s=2 process_s="<<seconds<<" core_budget_percent="<<seconds*50<<'\n';
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
