#pragma once
#include <array>
#include <memory>

namespace aether::modular {
inline constexpr int slotCount=12, algorithmCount=32, sourceCount=10, modulationCount=16, targetCount=84;
enum class ModuleType : int {
    Thru, Spectral, Modal, Colour, Space, PrismPitch, GrainCloud, Wavefolder,
    WdfDiode, Resonator, String, Ladder, FrequencyShift, RingMod, Comb,
    Diffuser, LoFi, Tape, SpectralFreeze, Chaos, Chorus, Count
};
struct MacroInfo {
    const char* name; const char* unit;
    float minimum=0,maximum=1,defaultValue=0.5f;
};
struct ModuleInfo {
    const char* name; const char* category; const char* description; const char* provenance;
    std::array<MacroInfo,4> macros;
};
const ModuleInfo& moduleInfo(ModuleType) noexcept;
const char* algorithmName(int) noexcept;
const char* sourceName(int) noexcept;
struct Slot {
    ModuleType type=ModuleType::Thru;
    bool enabled=true;
    float mix=1,levelDb=0,pan=0;
    std::array<float,4> macro{0.5f,0.5f,0.5f,0.5f};
};
struct Lfo { float rate=0.25f,phase=0; int shape=0; bool sync=false; };
struct Modulation { int source=0,target=0; float depth=0; };
struct Parameters {
    std::array<Slot,slotCount> slots{};
    std::array<float,slotCount> input{},output{};
    // connection[destination][source]; forward arcs current sample, self/reverse arcs prior sample.
    std::array<std::array<float,slotCount>,slotCount> connection{};
    std::array<Lfo,4> lfo{};
    std::array<Modulation,modulationCount> modulation{};
    float feedback=0.65f,trimDb=0;
    double tempo=120;
};
// Clears only connections/input/output; slot choices/macros/modulation remain unchanged.
void applyAlgorithm(int algorithm,Parameters&) noexcept;
// Fills the chosen slot's type + four normalized defaults; leaves routing unchanged.
void assignModule(Parameters&,int slot,ModuleType) noexcept;

class GraphEngine {
public:
    GraphEngine(); ~GraphEngine();
    GraphEngine(const GraphEngine&)=delete; GraphEngine& operator=(const GraphEngine&)=delete;
    void prepare(double sampleRate,int maximumBlockSize);
    void reset() noexcept;
    void setParameters(const Parameters&) noexcept;
    void setPerformance(float velocity,float pitchHz) noexcept;
    void process(float* left,float* right,const float* sidechain,int samples) noexcept;
    std::array<float,slotCount> nodeLevels() const noexcept;
    std::array<float,sourceCount> sourceLevels() const noexcept;
    int intrinsicLatencySamples(int slot) const noexcept;
private:
    struct Impl; std::unique_ptr<Impl> impl;
};
}
