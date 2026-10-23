# AETHER Micro 1.0.0 — local validation

Date: 2026-10-23 (Asia/Shanghai). Platform: Windows x64, Intel Core i9-13900H (14 physical cores / 20 logical processors). MSVC 19.44.35229, CMake 4.3.2, Ninja, Windows SDK 10.0.26100.0. JUCE source revision `501c07674e1ad693085a7e7c398f205c2677f5da` reports version 8.0.12; bundled VST3 metadata reports SDK 3.8.0.

## Built and exercised

- Release x64 FX VST3, Synth VST3, FX standalone and Synth standalone successfully compiled and linked.
- 46 pure-DSP behavioral assertions pass under MSVC and independently under GCC 14.3.0 with bounds assertions. Covers all five audio stages, serial stage pairs/permutations, finite output, silence, MIDI voices/sustain/bend/release/stealing, exact sample-block invariance, modal decay, space/freeze, limiter/dither, reset and rate changes. A replacement-new counter verifies no ordinary heap allocation in engine process/update/MIDI/reset after preparation.
- 10 real processor integration scenarios pass: parameter contract, global bypass, XML DAW state/corruption, every factory preset, JSON preset validation, A/B state persistence, mono/reprepare/rates, sample-offset MIDI, external FX sidechain/no direct leakage, and repeated editor creation/destruction with rendered screenshots.
- pluginval **1.0.4, strictness 5** reports **SUCCESS** for both actual VST3 bundles. Includes 44.1/48/96 kHz audio processing, 64/128/256/512/1024 block sizes, parameter/state/automation/editor and bus tests. No disabled tests or skip-GUI flags. pluginval's optional external Steinberg validator was not supplied and is explicitly skipped in the log; no claim of that separate validator passing.
- Native editor renders inspected at 1080×760, 1280×900 and minimum 900×680, plus compact view. Fixed bottom clipping and numeric formatting. FFT trace uses actual generated audio; PNG outputs checked for a single valid image rather than appended old captures.
- `dumpbin /dependents` lists Windows system DLLs only. Static C++ runtime; no JUCE, VCRUNTIME or third-party audio/model DLL installation is required. No GPU/WebView asset/runtime package is bundled.

## Size and timing

Actual FX VST3 module is approximately **4.99 MB** (decimal), plus a small module manifest. A `.vst3` is delivered as a directory bundle. The portable package also includes Synth, standalones, source dependencies, docs and audio; its ZIP size is not the single-module size.

`AetherRender` supplies fresh 0.2-amplitude sine input for every default-preset stereo block at 48 kHz and times only Engine::process from outside the callback. One recorded local Release run:

| Block | Audio processed | Processing wall time | One-core realtime budget |
|---|---:|---:|---:|
| 64 | 10.000 s | 0.262564 s | 2.63% |
| 256 | 10.000 s | 0.259793 s | 2.60% |
| 1024 | 9.984 s | 0.230081 s | 2.30% |

These are short local microbenchmarks, subject to CPU scheduling, power state and workload. They are not DAW CPU-meter results, all-stage maximum-polyphony results, p99 deadlines or guarantees on other machines. The earlier behavioral-test loop feeds processed output back repeatedly and measured around 5%; it is a different workload. No startup-time, RAM, SNR/alias-rejection, LUFS or reconstructed true-peak figure has been established. Report targets are not substituted for measured values.

Three 8-second stereo PCM16/48 kHz renders are supplied: generated source, processed Prism FX and MIDI Prism Keys. They validate renderability and give a convenient audition reference. No subjective listening evaluation is asserted.

## Reproducible evidence and limitations

The release `Validation` directory includes CTest, DSP, processor, pluginval, benchmark and binary-dependency logs. Tests run through the real engine/processor and built VST3 loader. No specific commercial DAW, macOS, Linux, ARM64, CLAP or external audio hardware was tested. Source CI configuration is provided but has not been run remotely in this session.

The local external JUCE checkout already contained one `<cstring>` include addition. The source archive includes its exact patch at `third_party/juce-local-cstring.patch`; the untouched upstream dependency archive is SHA-256 checked (`66bcd380ba444da6a9bb98b7471727c542232c27647c1ec22f47158353c186dc`). Apply the patch to the extracted dependency to exactly match the local source used for this build. This modifies header inclusion, not a JUCE algorithm. The existing external SDK checkout was preserved.

Known design limits: first-order ADAA introduces a fractional phase/time response without host compensation; no lookahead/true-peak limiter. Material/order/bypass changes and space-size integer delay changes are discrete and can click under abrupt automation. Freeze excludes new excitation in the wet feedback path; partial dry mix still passes live audio. Pitch tracking is for monophonic sources. Dither injects TPDF noise but does not integer-quantize the floating-point host stream. See REQUIREMENTS.md for the research roadmap boundary.
