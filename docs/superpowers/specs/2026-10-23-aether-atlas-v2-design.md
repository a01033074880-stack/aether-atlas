# AETHER Micro / Atlas 2.0.0 design

The user requests a substantial expansion beyond v1: unusual contemporary open-source effects actually integrated, a far richer signal chain with FM-style selectable routing algorithms, extensive modulation, and a fully reconstructed surrealist UI. Autonomous implementation is explicitly authorized by the original request and its continuation; do not stop at mockups or shrink success to a smaller compatible subset.

## Product result

Deliver a versioned Windows x64 2.0.0 FX/Synth VST3 pair and standalones, source including pinned third-party code, 24 or more usable presets, routing/FM diagrams, complete updated Chinese guide, real audio demonstrations, actual plugin validation, screenshots of every new workspace and package hashes. Keep v1.0.0 release/tag intact and support loading its states/presets through a legacy path.

## Sound architecture

12 independently assignable processing slots. Each can load any of 20 real effects plus Thru, including repeated copies. 32 named connection algorithms cover serial, parallel, fan, tree, diamond, crossfeed and delayed-feedback networks. A custom signed 12x12 matrix supports patch-cable editing, per-slot input/output sends, stereo pan, mix and level. Master/output controls remain available. Forward connections use the current sample; self/backward arcs explicitly use previous-sample output and bounded global feedback. Normalize dangerous fan-in and bound feedback; no unrestricted algebraic loops.

Effects: Spectral, Modal, Colour, Space, Prism Pitch, Grain Cloud, Wavefolder, WDF Diode, Resonator, String, Ladder, Frequency Shift, Ring Mod, Comb, Diffuser, LoFi, Tape, Spectral Freeze, Chaos and Chorus. Every macro must affect actual DSP and have truthful metadata/ranges/provenance. Import and compile real library code, not only imitate names.

Pinned upstream code researched on 2026-10-23: Signalsmith Stretch 1.3.2 commit88061fd4eda56a2353c5d65c74aa3f4ccf76f9da (MIT), Signalsmith Linear commit7bb1c8f36794d6980080ce817c91f8da8fe56a66 (MIT), DaisySP commit2c72eaf9eac5fc0dca1919d65d606da907832618 (MIT including source notices), chowdsp_wdf commit43bcd295e8403de913f9966f4fd7ff9d17c9f1a5 (BSD-3-Clause). Use latest fetched code as evidence, but do not claim objectively 'world's most advanced'. Full upstream source/notices remain vendored. Spectral and granular processors have intrinsic texture delay; expose/document it honestly instead of claiming phase-transparent zero-latency pitch shifting. Global host PDC policy must be documented and tested.

## FM and resynthesis

A true six-operator phase-modulation synthesizer with 16 MIDI voices, 32 selectable algorithms, algorithm morph, index, feedback, global ADSR, per-operator ratio/level/output/detune/envelope scales and an editable 6x6 modulation matrix. Support channel bend/sustain and sample offsets. Self/recursive modulation uses previous operator output and bounded phase input. Gate/oversample or otherwise limit extreme ultrasonic operators, document measured quality honestly.

Audio Resynth can excite the FM source from tracked pitch/envelope, adding an alternative to the legacy harmonic/noise voice. MIDI and audio-driven modes share the graph effects. Existing original v1 Engine remains for state compatibility and input analysis/source support. Sixteen assignment rows route four LFOs, input envelope, MIDI velocity, pitch, random hold and bounded chaos into 84 slot destinations (four macros, mix, level, pan across12 slots). LFO sync is cycles/beat; update per sample/control period without allocation.

## UI: surrealist instrument, usable patching

Working title Atlas / The Impossible Instrument. Ivory paper, ink-black technical marks, terracotta/cobalt accents, suspended spheres and architectural arches, long sculptural shadows; serif title and mono technical labels. Original composition, not a replica of a living/protected artist's image. Native JUCE vector graphics; original design board PNG and written visual philosophy. Fetch and embed OFL Instrument Serif and DM Mono with notices. Available aesthetic skills are frontend-design/canvas-design; no aesthetic MCP or image generation tool was exposed in this session, so do not invent such a call or claim generated imagery came from it.

Replace five-card layout with left searchable categorized module library, central 12-node patch canvas with live meters/cables, right selected-module inspector, and clear global control strip. Main workspaces Patch / FM / Motion; built-in algorithm thumbnails, custom cable creation/removal/gain, slot assignment and enable/mix/level/pan/four macros. FM view six operators, selectable algorithm diagram and editable matrix, operator envelope controls. Motion view four sources plus16 explicit assignment rows and live values. Minimal/all-parameter access remains. Default 1440x900, minimum1180x780; all important text/control geometry must be verified by actual rendered UI and interaction tests.

## Host/state/realtime

Preserve all existing39 parameter IDs and order. New IDs use node.1.*, route.1.2 (source1 -> destination2), lfo.1.*, modulation.1.*, fm.*. Values are APVTS-owned/cached atomics; no strings, new/delete, lock, filesystem or GUI in audio callback. Allocate all algorithm states in prepare so type/routing automation never lazily allocates. Guard finite values; bound freeze/feedback; reset histories deterministically. Parameter text conversions must roundtrip.

Schema2/current version2.0.0; accept schema1 and force graph/FM disabled for faithful v1 behavior. Store all new matrices and selections as actual parameter state, not only graphics. A/B captures new fields. Presets state checks sanitize all new values. Plan changes must be recorded in progress ledger; accepted user objective remains the authority.

## Verification and release evidence

Baseline v1 CTest2/2 passes before changes. Add meaningful tests-first graph/FМ suites: every module responds and stays finite, pitch frequency shift/granulation/freeze actual signals, no allocations after prepare, sample-rate/block invariance, all32 routing patterns/matrices and feedback stability, repeated copies and tails,16-voice MIDI+source timing+matrix/morph, mod assignments affect targets. Processor tests exercise schema1 migration, schema2 full matrix roundtrip, preset/A-B, all modules and graph host automation, buses and short events. Render screenshots at minimum/default/large acrossPatch/FM/Motion/Minimal and simulate core UI actions. Run pluginval on both actual final bundles with GUI and no disabled tests. Include open-source license/provenance audit and actual performance/size, preserving previous releases.
