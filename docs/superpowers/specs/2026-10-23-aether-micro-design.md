# AETHER Micro 1.0.0 implementation specification

Source: `AETHER_Micro_VST3_Product_Design_Report.pdf`, all 14 pages. This is a new project. The user has explicitly authorized autonomous technical decisions and completion without intermediate approvals.

## Product and deliverable

A self-contained Windows x64 VST3 creative channel strip and companion MIDI instrument, with the same five-stage sound engine and custom resizable editor. Ship version 1.0.0, source, reproducible CMake build, factory presets, Chinese/English instructions, test evidence, release notes, SHA-256 checksums, and a portable ZIP. No cloud service, activation, telemetry, downloads at runtime, or external samples.

## Architecture decision

Use C++20 and JUCE 8.0.12 already available at `C:/SDKs/JUCE`, with CMake FetchContent fallback pinned to commit `501c07674e1ad693085a7e7c398f205c2677f5da` with an archive SHA-256. A pure standard-C++ MIT DSP library is separate from the AGPL-3.0-or-later JUCE plugin shell. JUCE replaces the report's proposed Rust/nih-plug/WGPU combination to obtain a maintainable, verifiable native Windows release. Source CMake also supports macOS/Linux, but those binaries must not be represented as tested or delivered.

Alternative considered: Rust/WGPU host would increase dependency and integration risk without supplied source or models. Direct VST3 SDK would require rebuilding mature host/state/editor glue. JUCE gives the fastest reliable native result while retaining a reusable DSP library.

## Sound engine

Five ordered domains: perceptual resonance control, coupled modal/harmonic synthesis, antialiased nonlinear colour, dispersive feedback space, and stereo/master output. The first four stages support all 24 serial permutations; master remains last. Independent stage bypass, global latency-free bypass, smoothed input/output/wet controls, mono/stereo input/output, optional mono/stereo external sidechain on FX, and sample-offset MIDI events.

1. Spectral: 24 perceptual bands, adaptive peak suppression, focus frequency, threshold and transient preservation. Must act on real signal, with no decorative DSP controls.
2. Modal: audio-driven coupled resonators with selectable String/Plate/Beam/Membrane material, tuning/decay/brightness/mix. FX/Resynth/Synth modes. MIDI eight-voice synthesis, sustain, pitch bend and all-notes-off. Harmonic-plus-noise resynthesis uses a deterministic pitch/envelope estimator, no claim of learned DDSP.
3. Colour: three original circuit-inspired saturation characters, smoothed drive/bias/tone and antiderivative antialiasing. No claims of trained neural inference, NAM import, original WDF component solving or commercial hardware clones.
4. Space: stable Householder FDN, damped tails, diffusion/dispersion and freeze. Freeze excludes new excitation and remains bounded.
5. Master: width, ceiling safety with linked stereo gain and release, optional TPDF 16/24-bit dither. This is a sample-peak safety stage, not an unmeasured true-peak or loudness compliance claim.

One LFO with selectable shape/rate/depth/target, host tempo sync, input/output/peak/CPU/pitch visual feedback, actual spectrum from audio, and panic/reset.

## Host and editor

APVTS parameters with immutable dotted IDs and schema version 1. Cache raw atomic pointers; do not allocate, lock, access UI, log or perform I/O in processing. Chunk arbitrary host blocks rather than resize scratch buffers in audio callbacks. Preallocate delay/resonator/analysis state in prepare. Clamp invalid/non-finite inputs and serialized values. Atomic UI metering, SPSC analysis samples. Editor may close at any time.

1080x760 default responsive dark technical instrument with mint/cyan spectrum, warm amber secondary highlights, large spectrum canvas, five colour-coded stage cards, top presets/mode/bypass/minimal controls, bottom modulation/global strip. Vector-only JUCE painting, system font, 30-Hz visual updates. Minimal mode offers compact labeled sliders and pauses spectrum analysis. Keyboard-accessible controls, double-click reset, tooltips, readable numeric values. A/B snapshot, factory/user .aether JSON preset save/load and DAW state recall. No webview or image asset dependency.

## Version scope and honest acceptance

The report is a research roadmap rather than existing validated implementation. The complete v1 release implements the operational product above. WGPU 120 FPS, trained DDSP/S4D models, NAM/AIDA-X model conversion, cross-process 64-track mesh, CLAP/MIDI 2.0 note expression, arbitrary DAG parallel/M-S routing, 64-channel Atmos and published 3.8-MB/1.4%-CPU/86-dB-SNR figures remain roadmap items. Document this explicitly in a requirement coverage matrix; do not fake them in UI or marketing. Report actual binary size and benchmark hardware/settings.

## Verification

Test-first pure DSP executable: transparent bypass, spectral attenuation, modal impulse decay, MIDI note/timing/release, nonlinear finite bounded response, space tail/freeze, dither, arbitrary block sizes and rates, non-finite input protection, no heap allocation in steady-state processing, and deterministic reset. Plugin integration executable: parameter ranges, state round trip/corruption handling, preset factory/user roundtrip, mono/stereo layout, bypass, repeated prepare/release, GUI snapshots and UI lifecycle. Build Release FX VST3, Synth VST3, standalone and tests. Validate actual built bundles using pluginval or a VST3 host test, and inspect actual rendered editor. No unsupported listening or DAW compatibility claims.
