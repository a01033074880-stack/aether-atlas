# Third-party software notices — Atlas 2.1.0

Copyright 2026 Aether Micro contributors. The original standalone DSP code under `libs/aether-dsp` is MIT. The complete JUCE-linked application is distributed under AGPL-3.0-or-later; the full license is in `LICENSE`.

## JUCE

JUCE **8.0.12**, source revision `501c07674e1ad693085a7e7c398f205c2677f5da`, https://github.com/juce-framework/JUCE/tree/501c07674e1ad693085a7e7c398f205c2677f5da.
Copyright Raw Material Software Limited. JUCE modules are dual licensed AGPLv3/commercial. This release uses the AGPLv3 option. No separate commercial JUCE license is conveyed. JUCE source and its original notices are included in the source dependency archive alongside the release. Do not use this project's MIT DSP license as a license for the JUCE application.

JUCE incorporates code under additional permissive licenses, including VST3 SDK (Steinberg), FreeType, HarfBuzz, libpng, zlib, SheenBidi, FLAC, Ogg/Vorbis and platform-specific components. The complete notices from the actual JUCE source tree are retained in `licenses/JUCE-LICENSE.md` and the included JUCE source archive. The VST3 SDK license from this checkout is included as `licenses/VST3-SDK-LICENSE.txt`. JUCE's disabled browser/cURL/ASIO modules are not enabled as optional runtime features.

## Development tools

MSVC, Windows SDK, CMake, Ninja and pluginval were used for build/validation. Their executables are not part of the plugin release. pluginval: https://github.com/Tracktion/pluginval. Commercial plugin code, NAM model weights, trained DDSP weights, Airwindows and RTNeural are not integrated. Atlas includes actual open-source code from the following libraries; their notices and original authorship remain intact.

## Signalsmith Stretch and Linear — MIT

Signalsmith Stretch 1.3.2: copyright Geraint Luff / Signalsmith Audio Ltd. Pinned revision `88061fd4eda56a2353c5d65c74aa3f4ccf76f9da` at https://github.com/Signalsmith-Audio/signalsmith-stretch . Used for actual spectral pitch/formant processing and spectral freeze. Signalsmith Linear pinned revision `7bb1c8f36794d6980080ce817c91f8da8fe56a66`, copyright Signalsmith Audio, https://github.com/Signalsmith-Audio/linear . Used by Stretch for FFT/STFT processing. Source and full MIT licenses are under `third_party/signalsmith-stretch` and `third_party/signalsmith-linear`.

Atlas locally adds `reseed(long)` to Stretch for deterministic frozen-phase reset; this is disclosed in `third_party/ATLAS_GRAPH_PATCHES.md` and is not represented as an upstream release.

## DaisySP — MIT

Pinned revision `2c72eaf9eac5fc0dca1919d65d606da907832618`, https://github.com/electro-smith/DaisySP . Copyright Electrosmith and individual contributors. The selected source includes GranularPlayer, Wavefolder, Resonator, String, LadderFilter, Decimator, Chorus and supporting modules. Original file notices acknowledge Vinícius Fernandes, Emilie Gillet, Nick Donaldson, Richard van Hoesel / Infrasonic Audio and others. Full notices are retained in source and `third_party/DaisySP/LICENSE` (including incorporated Plaits/Soundpipe MIT notices). DaisySP's optional LGPL submodule is not compiled.

Local changes guard a GCC-only ladder optimization attribute for MSVC, keep phasor phases below one cycle, and bound granular sample/envelope indices. See `third_party/ATLAS_GRAPH_PATCHES.md`. These are Atlas modifications to pinned upstream source.

## chowdsp_wdf — BSD-3-Clause

Copyright Chowdhury-DSP, pinned revision `43bcd295e8403de913f9966f4fd7ff9d17c9f1a5`, https://github.com/Chowdhury-DSP/chowdsp_wdf . Used for an actual resistive-source/capacitive-shunt/antiparallel-diode wave digital circuit. Full BSD-3-Clause license is `third_party/chowdsp_wdf/LICENSE`. Library name and attribution do not imply endorsement.

## Typography and artwork

Instrument Serif and DM Mono font files are from Google Fonts, licensed under SIL Open Font License 1.1. Full notices are in `assets/fonts/InstrumentSerif-OFL.txt` and `assets/fonts/DMMono-OFL.txt`. Fonts are embedded in the binary and remain in the corresponding source. Native vector artwork and the procedural Atlas design board are original project work, guided by the written Suspended Cartography philosophy; no third-party surrealist painting or commercial plugin artwork is reproduced.

## Corresponding source and license boundaries

The complete application remains AGPL-3.0-or-later through JUCE's open-source option. Original DSP/routing/FM wrapper code under `libs/` is MIT with separate LICENSE files. MIT/BSD upstream code retains its own copyright and license. A wrapper's MIT license does not replace the application AGPL or any upstream notice. The portable source archive includes all vendored DSP and font sources/notices; the separate JUCE source ZIP and local patch reproduce its dependency. `third_party/atlas-dependencies.json` records pinned upstream archive hashes.

## aether-source references (no third-party code)

libs/aether-source (MIT) is original code. It implements published algorithms from their papers: Vector Phaseshaping Synthesis (Kleimola, Lazzarini, Timoney, Valimaki, DAFx-11), GENDYN (Serra 1993, Hoffmann 2000), Walsh functions, PCG-XSH-RR (O'Neill) and splitmix64 (public domain). jeremycg/coalescent was read for ideas only; no code was copied. Casio, Xenakis and other names are used descriptively only.
