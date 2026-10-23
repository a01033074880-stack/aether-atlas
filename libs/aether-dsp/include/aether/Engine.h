#pragma once
#include <array>
#include <memory>
namespace aether
{
struct Parameters
{
    float inputDb = 0, outputDb = 0, mix = 1;
    int mode = 0, order = 0;
    bool enabled[5] = {true, true, true, true, true};
    float spectralAmount = 0.35f, spectralThreshold = -24, spectralFocus = 3500, transientProtect = 0.65f;
    float modalMix = 0, modalDecay = 1.2f, modalTune = 0, modalBrightness = 0.5f;
    int modalMaterial = 0;
    float driveDb = 0, driveTone = 0.5f, driveBias = 0;
    int driveModel = 0;
    float spaceMix = 0.12f, spaceSize = 0.5f, spaceDecay = 2.5f, spaceDamping = 0.5f, dispersion = 0.2f;
    bool freeze = false;
    float masterWidth = 1, masterCeiling = -0.3f, masterRelease = 100;
    int dither = 0;
    float modRate = 0.5f, modDepth = 0;
    int modTarget = 0, modShape = 0;
    bool modSync = false;
    double tempo = 120;
};
// Audio-thread-owned state. Publish meter snapshots in the host wrapper for UI readers.
// Only construction and prepare allocate. No block-size assumption or processing latency.
class Engine
{
  public:
    Engine();
    ~Engine();
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    // prepare supports 8-384 kHz; invalid rates use a safe fallback.
    void prepare(double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void setParameters(const Parameters &) noexcept;
    void process(float *left, float *right, const float *sidechain, int samples) noexcept;
    void noteOn(int note, float velocity, int channel = 1) noexcept;
    void noteOff(int note, int channel = 1) noexcept;
    void sustain(bool down, int channel = 1) noexcept;
    void pitchBend(float semitones, int channel = 1) noexcept;
    void allNotesOff() noexcept;
    std::array<float, 24> bandLevels() const noexcept;
    float pitchHz() const noexcept;
    float gainReductionDb() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace aether
