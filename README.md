# AETHER Micro 1.0.0

A native five-stage creative audio processor and MIDI instrument, built from the product research report in this repository. Windows x64 VST3 FX, VST3 Synth and standalone applications. No activation, telemetry, external samples or runtime downloads.

**中文使用说明：[docs/USER_GUIDE.zh-CN.md](docs/USER_GUIDE.zh-CN.md)**

## Signal path

Spectral resonance control → coupled modal resonators → antialiased colour → dispersive feedback space → stereo/master output. The first four stages have 24 serial orders; master remains last. FX, monophonic audio resynthesis and 8-voice MIDI synthesis share the same engine. Factory presets, `.aether` JSON files, DAW recall, A/B comparison, global/stage bypass, external FX sidechain, tempo-scaled LFO, native spectrum editor and compact parameter view are included.

## Install

Extract the Windows release ZIP. Keep each `.vst3` directory intact and copy both directories from `VST3/` to `%LOCALAPPDATA%\Programs\Common\VST3`, or run the extracted package's `install.ps1`. Add this location to your DAW's VST3 search paths if needed and rescan. For all users, copy to `C:\Program Files\Common Files\VST3` with the appropriate Windows permissions. Close the DAW before upgrading. The installer preserves an existing bundle as a timestamped backup.

Use **AETHER Micro** on an audio track. Use **AETHER Micro Synth** on a MIDI/instrument track; select Prism Keys or Copper Strings and play MIDI notes. Standalone applications have an audio/MIDI device selector. Audio input is initially muted by JUCE in the FX standalone to avoid immediate microphone feedback; configure your device and routing before enabling it. DAW versions follow host routing.

## Build from source

Requirements: CMake 3.22+, Git, Visual Studio 2022 C++ Desktop tools and Windows SDK. From PowerShell:

```powershell
./scripts/build.ps1 -BuildDirectory build -Jobs 4
# Optional existing JUCE source (exactly 8.0.12):
./scripts/build.ps1 -JucePath C:/SDKs/JUCE
```

Without `JucePath`, CMake downloads the SHA-256-checked JUCE source archive at commit `501c07674e1ad693085a7e7c398f205c2677f5da` (reports version 8.0.12). A developer command prompt with Ninja is also supported:

```powershell
./scripts/build.ps1 -BuildDirectory build-ninja -Generator Ninja -JucePath C:/SDKs/JUCE
# Or provide -NinjaPath C:/path/ninja.exe -VcVarsPath C:/path/vcvars64.bat
```

Outputs are under `build/AetherMicro_artefacts/Release` and `build/AetherMicroSynth_artefacts/Release`. Run `AetherRender out/audio` for sample WAVs and a benchmark. Pure DSP builds without JUCE with `-DAETHER_BUILD_PLUGINS=OFF`; `ctest --test-dir build -C Release --output-on-failure` runs behavioral tests. The Windows CI definition is supplied; this local release does not claim a completed remote CI run.

## Scope and licensing

See [requirement coverage](docs/REQUIREMENTS.md) and [actual validation](docs/VALIDATION.md). The report's proposed trained neural models, WGPU/Rust host, CLAP, cross-process mesh and published size/CPU/SNR targets are research items. This release contains original deterministic DSP, a JUCE C++ host and native vector UI. It does not load NAM/AIDA-X models or claim a measured true-peak limiter, neural inference or commercial hardware emulation. macOS/Linux binaries are not included or validated.

Independent DSP library: MIT (`libs/aether-dsp/LICENSE`). Complete plugin: **AGPL-3.0-or-later**, because JUCE 8's open-source option is AGPLv3. This differs from the research report's GPL-only proposal. See `LICENSE` and `THIRD_PARTY_NOTICES.md`; a separate JUCE commercial license is not included. The release includes corresponding source and pinned build dependencies.
