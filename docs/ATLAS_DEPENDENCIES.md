# Atlas open-source research and dependency manifest

Research date2026-10-23. Upstream source was actually downloaded for integration, preserving all original licenses and notices. "Contemporary" here describes maintained and relevant techniques; no unsupported claim of being the globally newest or objectively best algorithm is made.

| Library | Pinned commit | License | Intended integrated role |
|---|---|---|---|
| Signalsmith Stretch1.3.2 | 88061fd4eda56a2353c5d65c74aa3f4ccf76f9da | MIT | Polyphonic spectral pitch/formant transformation |
| Signalsmith Linear | 7bb1c8f36794d6980080ce817c91f8da8fe56a66 | MIT | FFT/STFT infrastructure used byStretch |
| DaisySP | 2c72eaf9eac5fc0dca1919d65d606da907832618 | MIT, retaining individual provenance | GranularPlayer, wavefolding, resonator/string, ladder,decimation,chorus |
| chowdsp_wdf | 43bcd295e8403de913f9966f4fd7ff9d17c9f1a5 | BSD-3-Clause | Actual wave-digital diode/circuit processor |
| Instrument Serif | downloadedGoogleFonts/ofl/instrumentserif | SIL OFL1.1 | Surreal editorialdisplay typography |
| DM Mono | downloadedGoogleFonts/ofl/dmmono | SIL OFL1.1 | Parameter labels andtechnicalvalues |

Sources: https://github.com/Signalsmith-Audio/signalsmith-stretch ; https://github.com/Signalsmith-Audio/linear ; https://github.com/electro-smith/DaisySP ; https://github.com/Chowdhury-DSP/chowdsp_wdf ; https://github.com/google/fonts .

Full vendoredsource in third_party, fontlicenses inassets/fonts. ArchiveSHA-256 manifest will be copiedfromtmp/research-v2/dependencies.json into source before release. Modules must not advertise a library integration until actual code uses that library and tests demonstrate its sound processing.
