#pragma once
#include <array>
#include <memory>

namespace aether::fm {
inline constexpr int operatorCount=6,algorithmCount=32,voiceCount=16;
struct Operator {
    float ratio=1,level=0.7f,carrier=1,detune=0,attack=1,release=1;
};
struct Algorithm {
    const char* name;
    std::array<std::array<float,operatorCount>,operatorCount> matrix{};
    std::array<float,operatorCount> carriers{};
};
const Algorithm& algorithm(int index) noexcept;
struct Parameters {
    bool enabled=true,custom=false;
    int algorithm=0;
    float index=2,morph=0,feedback=0.1f;
    float attack=0.008f,decay=0.4f,sustain=0.7f,release=0.5f;
    float gain=0.65f,spread=0.25f,transpose=0;
    std::array<Operator,operatorCount> operators{};
    std::array<std::array<float,operatorCount>,operatorCount> matrix{};
};
class Synth {
public:
    Synth(); ~Synth();
    Synth(const Synth&)=delete; Synth& operator=(const Synth&)=delete;
    void prepare(double sampleRate,int maximumBlockSize);
    void reset() noexcept;
    void setParameters(const Parameters&) noexcept;
    void noteOn(int note,float velocity,int channel=1) noexcept;
    void noteOff(int note,int channel=1) noexcept;
    void sustain(bool down,int channel=1) noexcept;
    void pitchBend(float semitones,int channel=1) noexcept;
    void allNotesOff() noexcept;
    // A separate continuous monophonic FM voice, in addition to MIDI voices. envelope 0 disables it.
    void setAudioExcitation(float pitchHz,float envelope) noexcept;
    // Replaces both channels. Distinct stereo pointers, no heap/locks/I/O after prepare.
    void process(float* left,float* right,int samples) noexcept;
    int activeVoices() const noexcept;
private:
    struct Impl; std::unique_ptr<Impl> impl;
};
}
