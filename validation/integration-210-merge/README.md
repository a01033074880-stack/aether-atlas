# 2.1.0 integration candidate: merge of ui/source-panel (branch integration/2.1.0-rc)

Produced 2026-10-24 (Asia/Shanghai) in worktree `wt-integration-210`, branch `integration/2.1.0-rc`, created from
`integration/2.0.1-rc` HEAD `6fb9288`. Merge commit `6db0ddf` = `git merge --no-ff ui/source-panel` (tip `334bc9f`; merge-base with
integration/2.0.1-rc was `b1f3010` = ui/min-size-fix, which 2.0.1-rc had already merged). ui/source-panel already contains feat/source-layer
up to `d81dc66` (later commits on feat/source-layer, e.g. 4434831, are NOT included). No conflicts; no files edited by hand.
CMake project version is still 2.0.1 (version bump and packaging are deliberately not done here).
All logs below were produced by this round on commit `6db0ddf` (validated tree = merge commit; the logs are committed on top of it). No old logs reused.
The known render non-reproducibility (libs/aether-modular/src/ModuleState.h Delay::read out-of-bounds read) is NOT fixed here;
aether_fixtures (branch test/state-fixtures) is NOT part of this branch.

| File | Command | Result |
|---|---|---|
| check-version.log | `powershell -File scripts\check-version.ps1` | exit 0, 2.0.1 consistent, 5 allowlisted '2.0.0' literals |
| build.log | `scripts\build.ps1 -BuildDirectory build-integ-210 -JucePath C:/SDKs/JUCE -Generator Ninja -NinjaPath <ninja.exe> -VcVarsPath <vcvars64.bat> -Jobs 4` (fresh dir, Release; includes check-version, configure, build, ctest) | exit 0, 269 ninja steps, 0 errors, ctest 5/5 |
| ctest.log | `ctest --test-dir build-integ-210 -C Release --output-on-failure` (separate run) | exit 0, 5/5 passed: aether_dsp, aether_fm, aether_modular, aether_source, aether_plugin |
| pluginval-aether-micro-vst3.log | pluginval 1.0.4 `--strictness-level 5 --validate "AETHER Micro.vst3"` via Start-Process -Wait | exit 0, SUCCESS |
| pluginval-aether-micro-synth-vst3.log | same on `AETHER Micro Synth.vst3` | exit 0, SUCCESS |
| plugin-tests.log | `AetherPluginTests.exe atlas-screenshots` (cwd build-integ-210\plugin-tests-out) | exit 0, 29 passed, 0 failed |
| diff-stat-vs-2.0.1-rc.txt, merge-introduced-files.txt, diff-files-not-PluginEditor.txt | `git diff --stat / --name-status / --name-only 6fb9288 HEAD -- . ':!validation'` | see below |
| artefact-times.txt | LastWriteTime of built exe/vst3 | VST3 binaries 21:51:53 (Micro) / 21:51:56 (Micro Synth) |

Only two AETHER VST3 bundles are produced by the build (AETHER Micro, AETHER Micro Synth); both were validated.
Build exe times (Asia/Shanghai, 2026-10-24): AETHER Micro.vst3 21:51:53, AETHER Micro Synth.vst3 21:51:56, AetherPluginTests.exe 21:51:57.

## Diff vs integration/2.0.1-rc (excluding validation/)
23 files, +2278/-50. No changes under scripts/, docs/, packaging, or version strings. CMakeLists.txt changes only add the aether_source library,
AetherSourceTests/AetherSourceBench/AetherRenderPresets targets (no version change). Non-PluginEditor changes are the source layer itself
(Source/AtlasParameters.cpp, Parameters.*, PluginProcessor.*, PresetManager.cpp, libs/aether-source/*, THIRD_PARTY_NOTICES.md) and tests/tools
(tests/PluginTests.cpp, tests/SourceTests.cpp, tools/BenchSource.cpp, RenderPresets.cpp, RenderUiSnapshots.cpp, zero_diff_report.py).
PluginEditor.cpp/.h changed (Source tab).

## Notes
- check-version.ps1 allowlist reasons still say "schema (1..2)"; the code now accepts schema 1..3. Reason text is stale (the script itself passes); fix with the version change.
- Source/PluginProcessor.cpp line numbers for the allowlisted literals moved (27, 355, 387) but check-version matches them as allowed.
- Compiler warnings: 251 warning lines in build.log, 0 errors (JUCE deprecation warnings in PluginEditor.cpp getScaleFactor, C4100 in PluginProcessor.cpp etc.; not analysed in detail).

## Not verified
DAW loading, listening, real mouse interaction with the Source tab, Steinberg validator, remote CI, installer/package (not built). Render non-reproducibility
of graph/FM paths (Delay::read) is unfixed, so the 36-preset zero-diff and bit-exactness of graph/FM paths are not asserted by anything in this validation.
