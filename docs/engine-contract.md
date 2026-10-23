> Historical v1 interface reference. For Atlas2 use [v2 contract](v2-contract.md) and current public headers.

# Engine contract, v1.0.0

Only standard C++20 is allowed in the DSP library. No JUCE dependency. Namespace `aether`.

```cpp
struct Parameters {
 float inputDb=0, outputDb=0, mix=1;
 int mode=0, order=0; // 0 FX, 1 resynth, 2 synth; lexicographic permutations of stages 0..3
 bool enabled[5]={true,true,true,true,true};
 float spectralAmount=0.35f, spectralThreshold=-24, spectralFocus=3500, transientProtect=0.65f;
 float modalMix=0, modalDecay=1.2f, modalTune=0, modalBrightness=0.5f; int modalMaterial=0;
 float driveDb=0, driveTone=0.5f, driveBias=0; int driveModel=0;
 float spaceMix=0.12f, spaceSize=0.5f, spaceDecay=2.5f, spaceDamping=0.5f, dispersion=0.2f; bool freeze=false;
 float masterWidth=1, masterCeiling=-0.3f, masterRelease=100; int dither=0; // off/16/24
 float modRate=0.5f, modDepth=0; int modTarget=0, modShape=0; // target none/drive/modal/space/focus; shape sine/triangle/sample-hold
 bool modSync=false; double tempo=120;
};
class Engine {
public:
 Engine(); ~Engine();
 void prepare(double sampleRate, int maxBlockSize);
 void reset() noexcept;
 void setParameters(const Parameters&) noexcept;
 void process(float* left, float* right, const float* sidechain, int samples) noexcept;
 void noteOn(int note, float velocity, int channel=1) noexcept;
 void noteOff(int note, int channel=1) noexcept;
 void sustain(bool down, int channel=1) noexcept;
 void pitchBend(float semitones, int channel=1) noexcept;
 void allNotesOff() noexcept;
 std::array<float,24> bandLevels() const noexcept; // linear amplitude
 float pitchHz() const noexcept;
 float gainReductionDb() const noexcept;
};
```

Implementation may use a private `std::unique_ptr<Impl>` allocated at construction/prepare only. No copying. Stereo pointers are distinct non-null; sidechain nullable mono excitation (external when provided; input-derived otherwise). All channel-1 MIDI defaults refer to channel 1. No allocation in reset, setParameters, process or note methods. reset clears filter/tail/voice/analyser histories. Parameters update once per host block (or event segment) with short sample ramps to avoid zipper noise. Full bypass handled in processor. Finite sanitization at input and output, stable bounded feedback and tail behavior. Silent input plus default FX preset must not generate voices.
