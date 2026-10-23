# Atlas graph vendor patches

Upstream source and license notices remain in each vendor directory. These are local modifications to the pinned revisions in the dependency manifest, not upstream releases.

- `DaisySP/Source/Control/phasor.cpp`: wrap on `>= 2*pi` so exact-cycle phases cannot produce the out-of-range lookup value 1. Behavioral regression: a Nyquist-rate phasor must stay in `[0,1)`.
- `DaisySP/Source/Sampling/granularplayer.cpp`: use modulo for sample indices, including the exact end index; bound the 256-element envelope lookup against floating-point endpoint rounding. Granular capture and freeze tests cover repeated buffers and release.
- `signalsmith-stretch/signalsmith-stretch.h`: add `reseed(long)` forwarding to the configured random engine. Atlas calls it before `reset()` so frozen spectral phase randomization repeats deterministically, without reconstructing/allocating FFT state. The frozen-reset regression failed before this change and passes after it.
- `DaisySP/Source/Effects/chorus.cpp`: initialize `lfo_freq_` before `Init` calls `SetLfoFreq`, which reads the previous direction. This removes the uninitialized read and restores a deterministic direction on every initialization. A regression comparing audio after different previous LFO histories failed before this fix. Tests also construct engines over positive/negative poisoned storage and compare their audio.

Atlas applies Chorus delay before depth because upstream `SetLfoDepth` scales amplitude by the current delay. An audio regression verifies that changing pan does not silently change the unaffected channel's modulation trajectory. The earlier claim that assigning `ChorusEngine{}` initialized its scalar fields was incorrect: the upstream class has a user-provided empty constructor. The wrapper now calls the patched `Init` directly.

DaisySP String uses its zero-nonlinearity branch, avoiding the upstream dispersion branch's global `rand()`; the exposed excitation macro controls the actual input saturation instead.

- `DaisySP/Source/Filters/ladder.cpp`: guard the GCC-specific unroll-loops function attribute so the same selected source compiles under MSVC; no DSP equation change.
