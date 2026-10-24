# 2.0.1 integration release-candidate validation (branch integration/2.0.1-rc)

One validation round, produced on 2026-10-24 (Asia/Shanghai) at commit `159bd60e35290129e17cd496eede947a4c86f2f2`
(CMake project version 2.0.1) in worktree `wt-integration`, fresh build directory `build-integ`
(Ninja + portable MSVC via vcvars64.bat, JUCE C:/SDKs/JUCE, Release, `-Jobs 4`).
The branch is `chore/version-2.0.1` + merge of `ui/min-size-fix` (b1f3010), plus this branch's script fixes.
Logs from before an earlier interrupted attempt (and from dist/ or the 2.0.0 period) were discarded, not reused.
The release package is built from the commit that contains these logs, so RELEASE.json `sourceCommit` equals the branch tip.

| File | Command | Result |
|---|---|---|
| check-version.log | `scripts\check-version.ps1` | exit 0, 5 allowlisted code literals |
| build.log | `scripts\build.ps1 -BuildDirectory build-integ -JucePath C:/SDKs/JUCE -Generator Ninja -NinjaPath <ninja.exe> -VcVarsPath <vcvars64.bat> -Jobs 4` (check-version, configure, build, ctest) | exit 0, ctest 4/4 |
| ctest.log | `ctest --test-dir build-integ -C Release --output-on-failure` (separate run) | exit 0, 4/4 passed |
| pluginval-fx.log | pluginval 1.0.4 `--strictness-level 5 --validate "AETHER Micro.vst3"` | exit 0, SUCCESS, reports v2.0.1 |
| pluginval-synth.log | same on `AETHER Micro Synth.vst3` | exit 0, SUCCESS, reports v2.0.1 |
| plugin-tests.log | `AetherPluginTests.exe out\atlas-screenshots` (also exports the 36 factory presets and previews packaged under presets/ and Preview/) | exit 0, 25 passed, 0 failed |
| render-benchmark.log | `AetherAtlasRender.exe out\atlas-audio` (audio examples + render benchmark) | exit 0 |

Notes: build.log was written with PowerShell's default encoding, so compiler warnings with Chinese text may look garbled.
Known leftover: tools/RenderAtlas.cpp still writes `"version": "2.0.0"` into the generated docs/Atlas-module-catalog.json
(allowlisted in check-version.ps1); Source/PluginProcessor.cpp and PresetManager.cpp keep informational `"version":"2.0.0"` metadata.

Not covered: DAW manual test, Steinberg validator, remote CI, Visual Studio generator path (no VS 2022 on this machine).