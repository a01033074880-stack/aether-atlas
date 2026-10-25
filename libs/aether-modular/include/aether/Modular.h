#pragma once
#include <array>
#include <memory>

namespace aether::modular {
inline constexpr int slotCount=12, algorithmCount=32, sourceCount=10, modulationCount=16;
// 2.3.0: destinations 85..112 do not belong to the graph. The engine only sums sources*depth of those rows per control tick
// (extendedModulation) and leaves their meaning (Source / FM parameters) to the host application.
// Destinations: 0 = none, 1..nodeTargetCount = node x 7 (unchanged since 2.0), then extendedTargetCount host-defined ones.
// targetCount is the highest destination index (the option list has targetCount+1 entries).
inline constexpr int nodeTargetCount=84, extendedTargetCount=28, extendedTargetFirst=nodeTargetCount+1, targetCount=nodeTargetCount+extendedTargetCount, maxTarget=targetCount;
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
    int freezeMacro=-1;
    float freezeThreshold=0.5f;
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
    // Sum of sources*depth over the rows that point at destinations 85..112 (index = target-85), as of the latest control
    // tick; all zero before the first tick and after reset(). It changes only inside process(), at a control tick.
    const std::array<float,extendedTargetCount>& extendedModulation() const noexcept;
    // The values extendedModulation() held before the latest tick (zero after reset()). A host that wants a lag that does not
    // depend on where it cuts process() calls uses extendedModulationPrevious() for samples after a tick inside the same control
    // period and extendedModulation() for the first sample of a period (samplesUntilControl() == 0).
    const std::array<float,extendedTargetCount>& extendedModulationPrevious() const noexcept;
    // Samples until the control tick that happens inside process(): 0 = the next processed sample ticks. Ticks come every
    // 16 samples from the first sample after reset(), independent of how process() is cut into blocks.
    int samplesUntilControl() const noexcept;
private:
    struct Impl; std::unique_ptr<Impl> impl;
};
}
