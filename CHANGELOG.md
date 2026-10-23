# Changelog

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
