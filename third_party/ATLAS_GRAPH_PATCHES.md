# Atlas graph vendor patches

Upstream source and license notices remain in each vendor directory. These are local modifications to the pinned revisions in the dependency manifest, not upstream releases.

- `DaisySP/Source/Control/phasor.cpp`: wrap on `>= 2*pi` so exact-cycle phases cannot produce the out-of-range lookup value 1. Behavioral regression: a Nyquist-rate phasor must stay in `[0,1)`.
- `DaisySP/Source/Sampling/granularplayer.cpp`: use modulo for sample indices, including the exact end index; bound the 256-element envelope lookup against floating-point endpoint rounding. Granular capture and freeze tests cover repeated buffers and release.
- `signalsmith-stretch/signalsmith-stretch.h`: add `reseed(long)` forwarding to the configured random engine. Atlas calls it before `reset()` so frozen spectral phase randomization repeats deterministically, without reconstructing/allocating FFT state. The frozen-reset regression failed before this change and passes after it.

DaisySP ChorusEngine's `Init` retains the previous LFO direction. Atlas value-initializes the wrapper's ChorusEngine before `Init`, without modifying upstream chorus code. DaisySP String uses its zero-nonlinearity branch, avoiding the upstream dispersion branch's global `rand()`; the exposed excitation macro controls the actual input saturation instead.
