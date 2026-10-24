// Renders every factory preset through the real AetherAudioProcessor and writes raw
// float32 (little endian, channel-interleaved L R) files. Used to prove that a change
// leaves all factory presets bit-identical: render before, render after, compare.
#include "PluginProcessor.h"
#include <cstdio>

int main(int argc, char** argv) {
  juce::ScopedJuceInitialiser_GUI gui;
  if (argc < 2) { std::fprintf(stderr, "usage: AetherRenderPresets <output-directory>\n"); return 2; }
  juce::File out(juce::String::fromUTF8(argv[1]));
  out.createDirectory();
  constexpr int rate = 48000, block = 480, blocks = 300; // 3 s
  AetherAudioProcessor p;
  p.prepareToPlay(rate, block);
  std::uint32_t noise = 12345u;
  for (int k = 0; k < p.getNumPrograms(); ++k) {
    p.loadFactoryPreset(k);
    p.reset();
    std::vector<float> data;
    data.reserve(static_cast<std::size_t>(2 * block * blocks));
    juce::AudioBuffer<float> b(2, block);
    for (int blockIndex = 0; blockIndex < blocks; ++blockIndex) {
      juce::MidiBuffer midi;
      if (blockIndex == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 13);
      if (blockIndex == 50) midi.addEvent(juce::MidiMessage::noteOn(1, 67, 0.6f), 0);
      if (blockIndex == 120) midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
      if (blockIndex == 200) midi.addEvent(juce::MidiMessage::noteOff(1, 67), 0);
      for (int i = 0; i < block; ++i) {
        noise = noise * 1664525u + 1013904223u;
        const float n = (static_cast<float>(noise >> 8) / 8388608.f - 1.f) * 0.05f;
        const float t = static_cast<float>(i + blockIndex * block);
        b.setSample(0, i, 0.2f * std::sin(t * 0.05f) + n);
        b.setSample(1, i, 0.2f * std::sin(t * 0.07f) - n);
      }
      p.processBlock(b, midi);
      for (int i = 0; i < block; ++i) { data.push_back(b.getSample(0, i)); data.push_back(b.getSample(1, i)); }
    }
    auto file = out.getChildFile(juce::String::formatted("%02d.f32", k));
    file.deleteFile();
    juce::FileOutputStream stream(file);
    if (!stream.openedOk()) { std::fprintf(stderr, "cannot write %s\n", file.getFullPathName().toRawUTF8()); return 1; }
    stream.write(data.data(), data.size() * sizeof(float));
    std::printf("%02d %s\n", k, p.currentPresetName().toRawUTF8());
  }
  std::printf("rendered %d presets\n", p.getNumPrograms());
  return 0;
}