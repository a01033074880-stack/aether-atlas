#include <aether/Engine.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
namespace {
constexpr int rate=48000;
void word(std::ostream& s,uint32_t v,int bytes) { for(int i=0;i<bytes;++i) s.put(static_cast<char>((v>>(8*i))&255)); }
void wav(const std::filesystem::path& file,const std::vector<float>& l,const std::vector<float>& r) {
    std::ofstream s(file,std::ios::binary); const auto size=static_cast<uint32_t>(l.size()*4);
    s.write("RIFF",4); word(s,36+size,4); s.write("WAVEfmt ",8); word(s,16,4); word(s,1,2); word(s,2,2);
    word(s,rate,4); word(s,rate*4,4); word(s,4,2); word(s,16,2); s.write("data",4); word(s,size,4);
    for(size_t i=0;i<l.size();++i) for(float value:{l[i],r[i]}) word(s,static_cast<uint16_t>(static_cast<int16_t>(std::clamp(value,-1.f,1.f)*32767.f)),2);
    if(!s) throw std::runtime_error("Could not write WAV");
}
}
int main(int argc,char** argv) {
    try {
        const std::filesystem::path dir=argc>1?argv[1]:"out/audio"; std::filesystem::create_directories(dir);
        std::vector<float> l(rate*8),r(l.size());
        for(size_t i=0;i<l.size();++i) {
            const auto t=static_cast<double>(i)/rate; const auto beat=std::fmod(t,0.5);
            const auto pluck=std::exp(-beat*13.0)*(std::sin(t*6.28318530718*220)+0.4*std::sin(t*6.28318530718*880));
            const auto pulse=std::exp(-beat*60.0)*std::sin(6.28318530718*(70*t+0.8*std::exp(-beat*40.0)));
            l[i]=static_cast<float>(0.19*pluck+0.16*pulse); r[i]=static_cast<float>(0.18*pluck+0.16*pulse);
        }
        wav(dir/"01-Source.wav",l,r);
        aether::Engine fx; aether::Parameters p; p.driveDb=9; p.modalMix=0.18f; p.modalMaterial=1; p.spaceMix=0.28f; p.spaceDecay=3.5f;
        fx.setParameters(p); fx.prepare(rate,256);
        for(size_t i=0;i<l.size();i+=256) fx.process(l.data()+i,r.data()+i,nullptr,static_cast<int>(std::min<size_t>(256,l.size()-i)));
        wav(dir/"02-Prism-FX.wav",l,r);
        std::fill(l.begin(),l.end(),0); std::fill(r.begin(),r.end(),0);
        aether::Engine synth; p.mode=2; p.modalMix=0.35f; p.driveDb=3; p.modalDecay=2.8f; synth.setParameters(p); synth.prepare(rate,256);
        const int notes[]={48,55,60,64,67,72,67,64};
        for(int second=0;second<8;++second) {
            synth.allNotesOff(); synth.noteOn(notes[second],0.7f);
            for(int offset=0;offset<rate;offset+=256) {
                if(offset>=rate/2) synth.noteOff(notes[second]);
                synth.process(l.data()+second*rate+offset,r.data()+second*rate+offset,nullptr,std::min(256,rate-offset));
            }
        }
        wav(dir/"03-Prism-Keys.wav",l,r);
        std::cout<<"Rendered source, processed FX and MIDI instrument: 48 kHz stereo PCM16, 8 seconds each.\n";
        // Timing is outside the real-time engine; this measures one core's processing budget.
        for(int block:{64,256,1024}) {
            aether::Engine bench; aether::Parameters bp; bench.setParameters(bp); bench.prepare(rate,block);
            std::vector<float> bl(block),br(block); double elapsed=0; const int iterations=rate*10/block;
            for(int j=0;j<iterations;++j) {
                for(int i=0;i<block;++i) { bl[i]=0.2f*std::sin(0.04f*static_cast<float>(i+j*block)); br[i]=bl[i]; }
                const auto start=std::chrono::steady_clock::now(); bench.process(bl.data(),br.data(),nullptr,block);
                elapsed+=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
            }
            std::cout<<"BENCH default stereo 48000 Hz block="<<block<<" audio_s="<<static_cast<double>(iterations*block)/rate<<" process_s="<<elapsed<<" core_budget_percent="<<elapsed/(static_cast<double>(iterations*block)/rate)*100<<'\n';
        }
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
