# AETHER Micro — Atlas 2.0.1

**An impossible instrument for spectral material, physical bodies and phase architecture.** Windows x64 FX/Synth VST3 and standalone apps. The former five-stage chain is now a twelve-node patchable system with a six-operator FM source.

![Atlas patch workspace](out/atlas-screenshots/Atlas-workspace-0.png)

- Scale the entire interface from the top control at **50%, 75% or 100%**. This changes editor geometry only; audio, parameters and host state remain unchanged.

## What you can do

- Put any of **20 effects plus Thru** into **12 repeatable slots**. Choose **32 audio-routing algorithms** or edit a signed 12×12 matrix with serial, parallel, branching and delayed-feedback connections. Each slot has four meaningful macros, mix, level, pan and input/output sends.
- Build sounds with **six FM/PM operators, 16 MIDI voices, 32 algorithms**, editable 6×6 modulation, algorithm morph, ADSR and per-operator ratio/level/carrier/detune/envelope scales. Monophonic audio-driven FM follows incoming pitch and loudness.
- Route four LFOs, input envelope, MIDI velocity, pitch, random hold and chaos through **16 assignment rows to 84 destinations**.
- Explore actual **Signalsmith Stretch/Linear**, **DaisySP** and **chowdsp_wdf** algorithms: pitch/formants, live granular capture/freeze, WDF diode circuits, wavefolding, physical resonators/string, ladder, Hz shifting, combs and spectral freeze. Pinned code, licenses and local safety fixes are included.
- Work in native **Patch / FM / Motion** workspaces: searchable material library, algorithm gallery, cable canvas and selected-module inspector. Original surreal vector composition and embedded OFL fonts; no WebView, cloud, activation or external samples.
- Use **36 factory presets**, JSON `.aether`, A/B and DAW state recall. The first 39 parameter IDs remain stable. Schema-1 states/presets select the legacy processing path.

See the [Chinese guide](docs/USER_GUIDE.zh-CN.md), [feature/provenance matrix](docs/REQUIREMENTS.md), [dependency research](docs/ATLAS_DEPENDENCIES.md) and [actual validation](docs/VALIDATION.md). The v1.0.0 ZIP/tag remains intact; its docs are archived in `docs/archive/v1`.

## Install

Extract `dist/AetherAtlas-2.0.1-Windows-x64.zip`. Run the extracted `install.ps1`, or copy both complete `.vst3` directories from `VST3/` to `%LOCALAPPDATA%\Programs\Common\VST3`. Add this location to your DAW search paths if needed, then rescan. Close the DAW before upgrading. The script preserves old bundles as timestamped backups. A system-wide location is `C:\Program Files\Common Files\VST3`, with suitable permissions.

**AETHER Micro** goes on an audio/FX track; choose FX or Resynth and feed audio/sidechain. **AETHER Micro Synth** goes on a MIDI/instrument track and has no main audio input. Standalones are under `Standalone/`; configure their device selector. JUCE initially mutes input in the FX standalone to avoid immediate microphone feedback.

## Build

CMake 3.22+, Git, Visual Studio 2022 C++ tools and Windows SDK:

```powershell
./scripts/build.ps1 -BuildDirectory build -Jobs 4
./scripts/build.ps1 -JucePath C:/SDKs/JUCE
```

Without `JucePath`, CMake downloads a SHA-256-checked JUCE archive at commit `501c07674e1ad693085a7e7c398f205c2677f5da` (reports 8.0.12). DSP libraries/fonts are vendored. Apply `third_party/juce-local-cstring.patch` to match the original local JUCE include change; Atlas vendor changes are documented in `third_party/ATLAS_GRAPH_PATCHES.md`.

```powershell
# Developer environment with Ninja; optional -NinjaPath and -VcVarsPath supported
./scripts/build.ps1 -BuildDirectory build-ninja -Generator Ninja -JucePath C:/SDKs/JUCE
ctest --test-dir build-ninja -C Release --output-on-failure
./build-ninja/AetherAtlasRender.exe out/atlas-audio
```

Pure DSP builds use `-DAETHER_BUILD_PLUGINS=OFF`. Four CTest suites cover legacy, FM, modular graph and host/UI. Artifacts live under `AetherMicro_artefacts/Release` and `AetherMicroSynth_artefacts/Release`. A Windows CI workflow is supplied; local verification does not imply a remote CI run.

## Sound and performance boundaries

Pitch/formant and spectral freeze have STFT delay (measured 2560 samples at 48 kHz with default preparation). Frequency Shift has a 63-sample Hilbert delay. These are creative texture delays: **host PDC is zero and parallel paths are not automatically aligned**. TEXTURE displays the largest enabled single-node delay, not cumulative graph latency. Repeated spectral nodes can accumulate more delay. Use wet mix intentionally.

Six-operator FM uses fixed 4× processing and guarded ultrasonic operators. Extreme PM and nonlinear effects can still alias; no mathematically bandlimited or measured SNR guarantee. Sixteen FM voices alone can consume about half a realtime core on the development CPU; twelve expensive effects add work. Use sensible presets and your DAW's actual load. The UI hides unavailable CPU data.

Gain/index/feedback have short FM ramps; topology, material, morph and some operator edits remain immediate and can click. Freeze/recursive patches may persist indefinitely; Panic clears them. Master is sample-peak safety, not reconstructed true-peak/LUFS compliance. No trained DDSP/S4D/NAM models are included.

## License

Original legacy DSP, FM and modular wrapper/routing code: MIT (`libs/*/LICENSE`). Upstream algorithms retain MIT/BSD notices. Complete JUCE-linked application: **AGPL-3.0-or-later**. Fonts: SIL OFL 1.1. See `LICENSE` and `THIRD_PARTY_NOTICES.md`. Corresponding project/vendor source and pinned JUCE source are supplied. No commercial JUCE license or upstream endorsement is conveyed.
