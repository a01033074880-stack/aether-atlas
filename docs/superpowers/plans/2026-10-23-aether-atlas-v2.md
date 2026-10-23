# AETHER Atlas 2.0.0 implementation plan

> For agentic workers: use subagent-driven-development for scoped implementation/review; dispatching-parallel-agents authorizes independent graph, FM and native editor tasks under fixed contracts. Do not spawn nested agents. User has authorized autonomous completion.

**Goal:** Complete the user's expanded module/routing/FM/surreal UI objective as a tested versioned2.0.0 delivery, preservingv1 source/release.

**Architecture:** Pure C++20 GraphEngine and FM Synth alongside original Engine; JUCE/APVTS host with stable legacyIDs and newgraph/FM/modulation parameters; native artistic editor and vendored open-sourceDSP.

**Tech stack:** JUCE pinned501c076, Signalsmith Stretch1.3.2+Linear, DaisySP, chowdsp_wdf, C++20, MSVC, CMake/Ninja, pluginval.

**Spec:** docs/superpowers/specs/2026-10-23-aether-atlas-v2-design.md

## Global constraints

- Version2.0.0, Windowsx64 FX/Synth/Standalone complete output.
- 12slots,20 real effect types plusThru,32 selectable audio-routing algorithms and custom12x12 routing.
- 6operators,16FMvoices,32FMalgorithms,custom6x6 matrix.
- 4LFOs,16assignment rows,10source choices includingNone,84slot destinations.
- Realtime process/update/MIDI/reset: no allocation/locks/I/O/UI. Prepare all mutable buffers.
- Existing39 IDs/order preserved; schema1 restores legacy path, schema2 includes newstate.
- Actual compiled third-party algorithm integration with full license notices; no fake provenance/control.
- Rebuilt surreal native UI with readable operational controls, actual screenshots and interaction evidence.

### Task1: Modular DSP and algorithm library

Own libs/aether-modular (except contractheader already established), tests/ModularTests.cpp. Consume libs/aether-modular/include/aether/Modular.h and docs/v2-contract.md. Use vendor code under third_party. Create test-first suite, record expected red, implement all20 types,metadata,32routing patterns, signededges,feedback and modulation. Direct C++ standalone tests; controllerCMake integration. Verify actualfrequency/tail/route effects, noallocation, variableblocks/rates/reset. Commit ownfiles only; write .superpowers/sdd/atlas-v2/graph-report.md. Independent review/fixes required.

### Task2: Six-operator synthesis

Own libs/aether-fm/src and tests/FmTests.cpp. Consume libs/aether-fm/include/aether/FmSynth.h. Test-first then implement32 genuinely different algorithms,custommatrix/morph,16voices,ADSR,operator controls,stereo,channelbend/sustain, audioexcitation,finite/alias-conscious processing. Noallocation afterprepare. Commit ownfiles; report .superpowers/sdd/atlas-v2/fm-report.md. Independent review required.

### Task3: Host integration and parameter/state surface

Controller owns Source/Parameters.*, PluginProcessor.*, PresetManager.cpp, CMakeLists/scripts/tests/PluginTests.cpp, assets/fonts and dependency manifests. Cache expanded parameter pointers, integrate graph/FM while retaining legacy sound path, expose UI API in docs/v2-contract.md. Add migrated/full-state roundtrip,graph/FM factorysounds,latency/tail policy, relevant externalDLL checks. Build all variants and run suites.

### Task4: Surreal native editor

Own Source/PluginEditor.*, optionalnewSource/editor/*, assets/atlas-design-board.png, docs/ATLAS_VISUAL_PHILOSOPHY.md. Consume docs/v2-contract.md, modular/FM publicheaders and availableprocessor API. Use frontend-design/canvas-design skills. Create real searchablemodulebrowser,editablepatchcanvas,inspector,FMmatrix/Motion rows andMinimal view. Original proceduralart and embeddedOFLfonts. Stable1-based parameterIDs; never mutateDSPdirectly. Compile/screenshot/interaction fixes; commit ownfiles and report .superpowers/sdd/atlas-v2/editor-report.md. Independent review required.

### Task5: Integrated verification and release

Update Chineseguide,feature/provenance matrices,changelog,licenses and phase2report. Render real FX/FM/resynth/spectral/granular demonstrations; runall relevanttests and pluginval againstbuiltbundles,inspectallUIworkspaces andruncoreinteraction scenarios. Benchmarkrepresentative12slot graphs and16voiceFM, recordmemory/latencyconstraints honestly. Make2.0.0portablepackage withsource/vendorfonts/evidence,hash/installtest,localtag. Final requirement-by-requirement audit before markinggoalcomplete. Preservefullobjective acrosscontinuations; nevercallpartialworkfinished.
