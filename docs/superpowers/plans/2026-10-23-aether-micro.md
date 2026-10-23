# AETHER Micro implementation plan

> For agentic workers: Use superpowers:subagent-driven-development for isolated implementation tasks and requesting-code-review for independent review. User explicitly authorized autonomous completion.

**Goal:** Deliver AETHER Micro 1.0.0 as tested Windows x64 FX/Synth VST3 bundles and a portable versioned release.

**Architecture:** Pure C++20 DSP behind a JUCE APVTS processor/editor. Fixed storage and cached atomics in the audio path, standalone tests, native vector UI.

**Tech Stack:** C++20, JUCE 8.0.13, CMake, MSVC x64, CTest, pluginval.

**Spec:** `docs/superpowers/specs/2026-10-23-aether-micro-design.md`

## Global Constraints

- Version 1.0.0; Windows x64 deliverable.
- Pure DSP MIT; plugin shell GPL-3.0-or-later.
- No allocations, locks, UI, I/O or logging in DSP processing.
- Five genuine working DSP stages; 24 serial orders, master last.
- Immutable dotted parameter IDs; state schema 1.
- Research targets must be distinguished from verified v1 features.

### Task 1: Sound engine

Files: `libs/aether-dsp/include/aether/Engine.h`, `libs/aether-dsp/src/Engine.cpp`, `tests/DspTests.cpp`, `libs/aether-dsp/LICENSE`.

Interfaces: `aether::Parameters` and `aether::Engine` contract in `docs/engine-contract.md`. Standard library only. Tests compile standalone without JUCE.

- [ ] Write meaningful behavioral tests first; confirm missing/placeholder engine fails them.
- [ ] Implement prepare/reset/update/process and MIDI lifecycle, spectral/modal/colour/space/master/LFO.
- [ ] Run DSP tests with CMake/CTest or the available C++ compiler.
- [ ] Self-review numerical stability, arbitrary block/rate handling, zero allocation and release/reset behavior. Record evidence and commit only own files.
- [ ] Independent spec/quality review, address important findings.

### Task 2: Host integration and build

Files: `CMakeLists.txt`, `Source/Parameters.*`, `Source/PluginProcessor.*`, `Source/PresetManager.*`, `tests/PluginTests.cpp`, `scripts/build.ps1`.

Interfaces: processor public `state`, meter atomics, `readAnalysis`, `loadFactoryPreset`, `savePreset/loadPreset`, `toggleAB`, `requestPanic`. Contract in `docs/editor-contract.md`.

- [ ] Establish reproducible pinned JUCE/MSVC build, separate DSP tests.
- [ ] Write processor tests for transparent bypass, parameter/state/preset and layouts before implementation.
- [ ] Implement FX and Synth variants, sample-offset MIDI, fixed-size scratch, meters and preset API.
- [ ] Build Release VST3/Standalone/tests and run CTest.
- [ ] Review lifecycle, audio thread and serialization boundary.

### Task 3: Native editor

Files: `Source/PluginEditor.h`, `Source/PluginEditor.cpp`.

Interfaces: consumes `docs/editor-contract.md` and `Source/Parameters.h`. Own no processor or DSP files.

- [ ] Implement responsive vector spectrum editor, five stage cards, complete APVTS control access, preset/mode/order/A-B and global controls.
- [ ] Implement real spectrum analysis, efficient minimal mode, tooltips and keyboard accessibility.
- [ ] Build through controller, inspect rendered images, correct clipping and interaction defects.
- [ ] Independent spec/quality review and fixes.

### Task 4: Verification and release

Files: `README.md`, `docs/USER_GUIDE.zh-CN.md`, `docs/REQUIREMENTS.md`, `docs/VALIDATION.md`, `CHANGELOG.md`, `LICENSE`, `THIRD_PARTY_NOTICES.md`, `scripts/package.ps1`, `scripts/install.ps1`, `.github/workflows/build.yml`.

- [ ] Validate actual VST3 bundles; complete standalone integration tests, GUI images, DSP benchmark and sample renders.
- [ ] Package portable FX/Synth VST3, standalone, source archive, presets and guides under `dist/AetherMicro-1.0.0-Windows-x64`.
- [ ] Produce and verify SHA-256 manifest; test installer destination using temporary install root.
- [ ] Independent whole-project review; fix major findings, rerun affected validation.
- [ ] Commit version, tag v1.0.0; deliver local links and actual tested limitations.
