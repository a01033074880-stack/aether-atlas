# Product report coverage — 1.0.0

The report is an architectural research proposal with speculative performance comparisons. This matrix records the working shipped implementation, without treating unsupported targets as measurements.

| Report area | Shipped 1.0.0 | Remaining research / scope difference |
|---|---|---|
| Five DSP dimensions | Five real sound stages, independent bypass, 24 serial permutations | No arbitrary parallel or M/S DAG, 32-sample coefficient cadence rather than general SIMD scheduler |
| Psychoacoustic dynamics | 24 perceptually distributed bandpass detectors, level-adaptive resonance reduction, frequency focus and transient protection | Not a 2048-band CQT, true Bark masking model, orthogonal source separator, hand-drawn 24-node EQ or upward compressor |
| Audio resynthesis | Monophonic deterministic pitch/envelope tracker, harmonic-plus-noise source | No trained DDSP/MLP weights, learning, differentiable training pipeline or polyphonic pitch extraction |
| Modal synthesis | Two slightly detuned banks, 12 damped modes per side, four material ratios, audio/sidechain excitation | Not two 64-mode calibrated physical instruments; no six-operator PM matrix or wavetable import |
| MIDI | Eight voices, velocity, release, per-channel ±2-st bend, sustain, panic | MIDI 1.0 channel messages; no MIDI2/MPE note expression or CLAP poly modulation |
| Colour | Three original saturation curves, first-order antiderivative antialiasing, tone/bias/DC handling | Not WDF circuit solving, ADAA2, S4D, trained neural weights, NAM/AIDA-X loader or 32 cloned hardware models |
| Space | Eight-line damped Householder FDN, first-order allpass dispersion, long tail and stable freeze | Not 16-line FDN, Hermite/Bézier time warp, spectral phase freeze or extreme time stretch |
| Master | Mid/side width, linked stereo sample-peak safety, 16/24-bit-scale TPDF noise | Not a true-peak lookahead limiter, LUFS correction, shaped 9th-order dither or Airwindows ConsoleX encode/decode |
| Modulation | Sine/triangle/random LFO, five destination choices, tempo scaling | Not four MSEGs, drag-drop modulation matrix, Lorenz chaos or eight macros |
| Inter-instance mesh | Host external FX sidechain | No 64-slot cross-process shared memory or automatic conflict avoidance |
| Host | Separate native FX and Synth VST3, standalone apps, mono/stereo, APVTS automation, JSON presets/DAW recall | C++ JUCE wrapper replaces Rust/nih-plug; no CLAP, 64-channel/Atmos or MIDI2 |
| UI | Native resizable vector UI, actual output FFT, five cards, meters, all-parameter minimal view, A/B, presets | 30-Hz JUCE UI replaces WGPU/120FPS; no unsupported CPU meter, 3D grids or GPU claims |
| Realtime | Preallocated engine and fixed wrapper scratch, atomic meters/parameters, SPSC analyser, no engine heap allocation in steady state | Processing is not a formal hard-real-time proof; third-party host/framework behavior remains outside the pure engine |
| Delivery | Version 1.0.0 Windows x64 FX/Synth bundles, source, presets, docs, test/validator logs and samples | macOS/Linux/ARM64/CLAP releases and embedded ports not delivered |
| Metrics | Actual size and local timings recorded in VALIDATION.md | 3.8 MB, 1.4% CPU, 9.2 MB RAM, <6ms startup, 86dB SNR and 120FPS/0% UI CPU are unverified report targets |
| License | MIT independent DSP; AGPL-3.0-or-later complete application | JUCE8 open-source terms require AGPL, so report's GPL-only distribution proposal is changed |

The above are release boundaries, not hidden disabled controls. Every user-facing audio control affects the shipped DSP. Mode/source selection precedes serial effects so reordering stages acts on the generated source too. Global bypass passes the FX input through; dry/wet still precedes enabled Master and output level.
