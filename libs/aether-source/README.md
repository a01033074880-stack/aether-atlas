# aether-source

Polyphonic source voices that sit next to `aether::fm::Synth` and feed the same
`aether::modular::GraphEngine`. MIT licensed (see `LICENSE`).

| Class | Voice | Idea |
| --- | --- | --- |
| `PhaseGeometrySource` | Phase Geometry Voice | ramp -> phase multiplier -> VPS breakpoint map -> CZ-style phase distortion (+ resonance window) -> cosine / Walsh table |
| `ChaosSource` | Chaos Voice | GENDYN: breakpoint amplitudes and durations follow random walks with mirror barriers, one polygon per period |

## Interface

`aether::source::Source` is the common interface: `prepare`, `reset`, `noteOn`, `noteOff`,
`sustain`, `pitchBend`, `allNotesOff`, `process(left, right, samples)`, `activeVoices`,
`lastVelocity`, `lastPitchHz`. Each concrete class also has `setParameters(const Params&)`.
All parameters are sanitised (NaN/inf/out of range -> clamped or defaulted).

* 16 voices, fixed arrays, no heap allocation after `prepare` (checked by the tests with a counting `operator new`).
* Deterministic: PCG32, seeded from `CommonParameters::seed` and the voice age. No clocks, no global state.
* Output does not depend on the host block size: all smoothing, envelopes and oscillator state advance per sample.
* `antiAlias = true` runs each voice at 4x and decimates with a 12th order Butterworth low-pass (RBJ biquads, corner 0.40 of the host rate).
* Walsh steps are band-limited with first order ADAA (`PhaseGeometryParameters::walshBandLimit = 2`, default): the Walsh function is replaced by its box average over the phase travelled in one oversampled tick, using a precomputed running-sum table (`Tables::walshCum`). Mode 0 is the plain table lookup and mode 1 a 2-point polyBLEP; both stay available for A/B measurement. Sequencies whose lowest partial (about sequency/2 x f0) lies above Nyquist cannot be represented; ADAA lowpasses them instead of letting them fold back. Measured numbers are in `tests/SourceTests.cpp` output.
* Changing parameters while playing (host automation): `setParameters()` is safe on the audio thread (no allocation) and takes effect at the call, i.e. at a block boundary. `seed` is read **only when a note starts**: held voices keep the seed they were started with, new notes use the current value; nothing is reset. Toggling `antiAlias` changes the oversampling factor, re-tunes the running voices (phase and envelope continue) and **clears the two decimation filters** (the DC blockers keep their state); this produces a short, measurable transient: measured with a held note (test "seed and antiAlias changes" in `tests/SourceTests.cpp`), the largest sample-to-sample step in the 8 samples after the switch was 0.011..0.104 (peak level 0.2..0.24; steady-state largest step 0.07..0.16), i.e. a step of the same order as the waveform itself, not a click beyond it; the test bounds it to < 4x peak.
* `process` writes stereo into `left`/`right`; connect it to `GraphEngine::process` and call
  `GraphEngine::setPerformance(source.lastVelocity(), source.lastPitchHz())` for the velocity and pitch modulation sources.

## Provenance

* VPS: implemented from equation 6 of J. Kleimola, V. Lazzarini, J. Timoney, V. Valimaki, "Vector Phaseshaping Synthesis", DAFx-11 (2011). No code taken.
* Phase distortion shapes: original piecewise-linear maps in the style of Casio's CZ series. No firmware or third-party code.
* Walsh functions: Gray code + bit reversal of the Hadamard index (standard construction).
* GENDYN: S. Serra, "Stochastic Composition and Stochastic Timbre: GENDY3 by Iannis Xenakis" (1993); S. Hoffmann (2000) for the amplitude/duration walk with reflecting barriers. Distribution choices follow the idea (not the code) of jeremycg/coalescent. Written from scratch.
* PCG32: M. O'Neill, PCG-XSH-RR (public algorithm); splitmix64 (public domain).
