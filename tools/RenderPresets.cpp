// Renders every factory preset through the real AetherAudioProcessor and writes raw
// float32 (little endian, channel-interleaved L R) files. Used to prove that a change
// leaves all factory presets bit-identical: render before, render after, compare.
//
//   AetherRenderPresets <output-directory> [block-size=480] [fresh] [clamp]
//
// Events are scheduled at absolute sample positions, so the rendered signal and the MIDI
// timing are identical for every block size (the default 480 reproduces the original tool).
// "fresh" constructs a new processor for every preset instead of reusing one instance.
#include "PluginProcessor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
  juce::ScopedJuceInitialiser_GUI gui;
  if (argc < 2) { std::fprintf(stderr, "usage: AetherRenderPresets <output-directory> [block-size] [fresh]\n"); return 2; }
  juce::File out(juce::String::fromUTF8(argv[1]));
  out.createDirectory();
  constexpr int rate = 48000, totalSamples = 144000; // 3 s
  const int block = argc > 2 ? std::atoi(argv[2]) : 480;
  bool fresh = false;
  for (int i = 3; i < argc; ++i) fresh = fresh || std::strcmp(argv[i], "fresh") == 0;
  if (block < 1 || block > 8192) { std::fprintf(stderr, "bad block size\n"); return 2; }
  struct Event { int sample; bool on; int note; float velocity; };
  const Event events[] = {{13, true, 60, 0.8f}, {24000, true, 67, 0.6f}, {57600, false, 60, 0}, {96000, false, 67, 0}};
  // The mode restriction per plugin type (2.2.0) is off here by default so the tool keeps rendering every stored mode; "clamp" switches it on.
  bool clamp = false;
  for (int i = 3; i < argc; ++i) clamp = clamp || std::strcmp(argv[i], "clamp") == 0;
  auto make = [&] { auto p = std::make_unique<AetherAudioProcessor>(); if (!clamp) p->setModeClamp(AetherAudioProcessor::ModeClamp::none); p->prepareToPlay(rate, block); return p; };
  auto p = make();
  std::uint32_t noise = 12345u;
  const int presets = p->getNumPrograms();
  for (int k = 0; k < presets; ++k) {
    if (fresh && k > 0) p = make();
    p->loadFactoryPreset(k);
    p->reset();
    std::vector<float> data;
    data.reserve(static_cast<std::size_t>(2) * totalSamples);
    juce::AudioBuffer<float> b(2, block);
    for (int start = 0; start < totalSamples; start += block) {
      const int count = std::min(block, totalSamples - start);
      b.setSize(2, count, false, false, true);
      juce::MidiBuffer midi;
      for (const auto& e : events) if (e.sample >= start && e.sample < start + count)
        midi.addEvent(e.on ? juce::MidiMessage::noteOn(1, e.note, e.velocity) : juce::MidiMessage::noteOff(1, e.note), e.sample - start);
      for (int i = 0; i < count; ++i) {
        noise = noise * 1664525u + 1013904223u;
        const float n = (static_cast<float>(noise >> 8) / 8388608.f - 1.f) * 0.05f;
        const float t = static_cast<float>(i + start);
        b.setSample(0, i, 0.2f * std::sin(t * 0.05f) + n);
        b.setSample(1, i, 0.2f * std::sin(t * 0.07f) - n);
      }
      p->processBlock(b, midi);
      for (int i = 0; i < count; ++i) { data.push_back(b.getSample(0, i)); data.push_back(b.getSample(1, i)); }
    }
    auto file = out.getChildFile(juce::String::formatted("%02d.f32", k));
    file.deleteFile();
    juce::FileOutputStream stream(file);
    if (!stream.openedOk()) { std::fprintf(stderr, "cannot write %s\n", file.getFullPathName().toRawUTF8()); return 1; }
    stream.write(data.data(), data.size() * sizeof(float));
    std::printf("%02d %s\n", k, p->currentPresetName().toRawUTF8());
  }
  std::printf("rendered %d presets (block %d%s)\n", presets, block, fresh ? ", fresh instance per preset" : "");
  return 0;
}