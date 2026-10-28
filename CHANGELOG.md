# Changelog

## Unreleased — planned for 2.3.0 (DRAFT, no release date)

Draft, phase A only. The CMake/plugin version is still 2.2.0; nothing below is released. (The heading deliberately carries no `x.y.z` number: `scripts/check-version.ps1` treats the first `## x.y.z` heading as the current version.)

- Modulation destinations (owner: sound engineer, branch `feat/mod-targets-230`, merged on `integration/2.3.0-rc`): `modulation.N.target` gains 28 destinations numbered 85..112 (option count 85 -> 113). 85-87 Source Level / Spread / Transpose (any Source engine), 88-96 Phase Geometry (Walsh seq, Walsh mix, CZ depth, CZ env, CZ velocity, VPS D, VPS V, VPS ratio, Phase random), 97-101 Chaos (Amp step, Dur step, Dur range, Momentum, Amp barrier), 102-106 FM (Mod index, Morph, Feedback, Voice level, Spread), 107-112 FM Op 1..Op 6 Level. Menu names are `Source / ...` and `FM / ...` (at most 24 characters), set in `modulationTargetNames()`; the modulated parameter of each number is `modulationExtendedParameter` in `Source/Parameters.h`. Existing numbers (1..84) and parameter IDs are unchanged. Full table: `docs/modulation-destinations.md` and the user guide section "新增调制目的地".
- Modulation semantics: like the old destinations, the new ones only run while `graph.enabled` is on; depth 1 = full parameter travel in the normalized/skew domain; the host parameter value stays the baseline and is never written back; result clamped to the parameter range; modulation updates every 16 samples and the destinations lag the sources by at most one 16-sample tick; a destination whose engine is not running (for example a Phase Geometry destination while the engine is Chaos, or an FM destination with FM off) is silent without an error and the render is bit-identical to the unmodulated one.
- Saved state: a save in which a motion row targets 85..112 (state, A/B snapshot, preset JSON) is stamped schema 4; every other save is still schema 3, byte for byte as 2.2.0. 2.3.0 reads schemas 1..4 and refuses 0, 5+ and corrupt schema 4 without changing state. A 2.2.0 build refuses schema 4 (state unchanged) — checked with a probe on a build whose source equals 991e493, not in a real host.
- UI hints and per-edition hiding: planned, scope TBD (owner: UI work, not part of this branch).
- Docs (USER_GUIDE.zh-CN.md): new Source page (Phase Geometry / Chaos parameters), plugin mode split with the fallback rule for old saved modes, Resynth-driven Source, install location `C:\Program Files\Common Files\VST3` (administrator rights; remove or replace old versions first), the final list of modulation destinations 85..112 with names, parameter IDs and engines, and the host parameter count of the All params page corrected from 467 to 495 (467 earlier parameters + 28 Source layer parameters, `totalParameterCount`).
- Installer: `scripts/install.ps1` is unchanged on this branch (default destination is still `%LOCALAPPDATA%\Programs\Common\VST3`; `-Destination` selects another directory). Whether the default changes in 2.3.0: TBD.
- Tests (new): `tests/ModDestinationContractTests.cpp`, built per plugin form as AetherModContractFx / AetherModContractSynth, with ctest entries `aether_modtargets_invariant` (guards the frozen 2.2.0 destination table, old-save routing, extreme depth; module rotation covers every destination <= 84), `aether_modtargets_new` (destinations 85..112 observed at processor level) and `aether_modtargets_schema4` (saved-state schema plan: schema 4 only for saves with a destination > 84, schema 1..4 readable, schema 5 / corrupt schema 4 refused). The frozen tables live in `tests/fixtures/mod-targets/` (`targets-v2.2.0.txt`, and per form `saved-v2.2.0-fx.txt` / `saved-v2.2.0-synth.txt` = canonical saved state of every fixture as written by 2.2.0). Truncated schema 4 saves: case `s4c` of `aether_modtargets_schema4` cuts a schema 4 save by 3 bytes; JUCE's `getXmlFromBinary` / `XmlDocument` alone would still parse such a blob, so the state loader checks the binary header, declared length, payload length plus terminating NUL and the AetherState tree before anything is merged, and a truncated or malformed save is refused as a whole with the running state untouched (`s4c` and `s4d` pass in the integration run on fd7e43c).
- Compatibility note: because `modulation.N.target` is a choice parameter, the saved value is the option index, so saves with destinations <= 84 keep their routing; the normalized value a host sees for automation changes when the option count grows (index / (count - 1)).

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
