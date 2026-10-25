# Changelog

## 2.2.0 — 2026-10-25 (release candidate)

- Resynth + Source: in Resynth mode (AETHER Micro) the Source layer (Phase Geometry / Chaos) is now driven by the input audio: pitch (including transpose) follows the detected pitch, amplitude follows the input envelope. With no valid pitch the last pitch is kept (30 ms fade out, 4 ms fade in, no click). Source Off leaves the output bit-identical. MIDI notes still trigger Source voices in Resynth.
- Mode split by plugin type: AETHER Micro Synth (instrument) only offers and runs Synth; AETHER Micro (effect) only offers and runs FX and Resynth. The global.mode option list and parameter ID are unchanged; the mode is clamped in the processor (single read point) and the editor only lists the applicable items. Nothing is written back to the saved state.
- Old saves: in AETHER Micro a stored mode=2 (Synth) now falls back to FX; in AETHER Micro Synth a stored mode 0/1 is treated as Synth.
- Editor: Mode picker shows only the applicable items per edition; the Source tab is usable in Micro Resynth and shows an explicit 'Switch Mode to Resynth' notice in Micro FX.
- Expected golden change (tests/fixtures/golden/render-48000.json): exactly 4 archived-state samples whose stored mode is 2 now render as FX in AETHER Micro: preset-v1.0.0:Copper Strings, preset-v1.0.0:Prism Keys, state-v2.0.1:custom-graph-fm, state-v2.0.1:factory-11-Copper-Strings. All other entries are byte-identical; the previous file is kept as golden-old-80165fc.json.
- Tests: new aether_split_unchanged and aether_resynth_split (both plugin forms), new aether_plugin_synth_ui (UI test against the Synth edition; effect-only cases are skipped there with a printed reason).

## 2.1.0 — 2026-10-24

- Merged Delay::read wrap fix (out-of-bounds read at buffer wrap).
- Merged source layer (schema 3): Phase Geometry / Chaos engines, Input-gain follow, automatable seed/antialias.
- Merged Source panel UI and state/preset fixtures; golden renders updated after the delay fix.
- Schema 1..3 upgrade regression: archived v1 presets and v2.0.1 states load, reset source defaults, and re-save as schema 3.

## 2.0.1 — 2026-10-24

- Added native Atlas editor zoom control with 50%, 75% and 100% scales.
- Zoom changes editor geometry only; audio, parameters and host state remain unchanged.
- Added integration coverage and rendered zoom snapshots at all three scales.


## 2.0.0 — 2026-10-23

- Rebuilt the instrument as Atlas: 12 repeatable effect slots, 20 effect types plus Thru, 32 routing algorithms, signed custom patching, per-node mix/level/pan/input/output and four real macros.
- Integrated Signalsmith Stretch/Linear, DaisySP and chowdsp_wdf source with pinned revisions, notices and tested local safety fixes. Added spectral pitch/formant transformation, live granular capture/freeze, WDF diode circuit, wavefolder, physical resonators/string, ladder, frequency/ring shifting, comb, diffusion, LoFi, tape, spectral freeze, chaos and chorus.
- Added a true six-operator FM/PM engine with 16 MIDI voices, 32 algorithms, custom 6x6 routing, algorithm morph, per-operator ratios/levels/carriers/detune/envelope scales, ADSR, 4x internal processing and audio-driven resynthesis excitation.
- Added four LFOs, envelope/velocity/pitch/random/chaos sources and 16 signed modulation assignments addressing 84 slot destinations.
- Replaced the five-card interface with a surreal native Patch/FM/Motion workspace, searchable library, cable canvas, module inspector, algorithm gallery, FM matrix, help, and full parameter view. Embedded OFL Instrument Serif and DM Mono.
- Preserved the first 39 host parameter IDs and v1 schema import through the legacy graph/FM-disabled path. New schema 2 stores all routes, operator matrices, modulation and A/B snapshots.
- Added 24 Atlas/FM/Motion presets alongside 12 legacy factory sounds. Expanded DSP, FM, module, host, GUI and release validation.
- Fixed high-rate/long-time FM ADSR precision, smoothed gain/index/feedback automation, vendor granular bounds and deterministic spectral freeze reset.

Detailed final measurements and limitations belong to the versioned validation report; no unmeasured neural, true-peak, alias-rejection or globally latest-algorithm claim is made.

## 1.0.0 — 2026-10-23

- First complete Windows x64 VST3 FX and MIDI instrument release, with standalone applications.
- Five working processing stages, 24 serial permutations, mono/stereo FX with external sidechain.
- Adaptive 24-band resonance control, original coupled modal synthesis, eight-voice MIDI, monophonic harmonic/noise resynthesis, three antialiased colour curves, eight-line dispersive FDN with freeze, stereo width and sample-peak safety/dither.
- Native resizable editor with real spectrum, stage controls, presets, A/B, modulation, meters and compact mode.
- Versioned DAW state and JSON presets, factory sounds, portable installation, pure-DSP and host tests, audio renders and independent VST3 validation.
- MIT DSP and AGPL-3.0-or-later complete application; published roadmap coverage distinguishes research claims from shipped functionality.
