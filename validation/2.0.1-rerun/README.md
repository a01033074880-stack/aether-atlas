# 2.0.1 fresh re-validation (branch chore/version-2.0.1)

All logs in this directory were produced on 2026-10-24 (Asia/Shanghai) from a fresh build
directory `build-qa` inside the worktree `wt-version`, at commit `57001d8b6d08c2cbaf07e0ee83c89250cc335d11`
(CMake project version 2.0.1). No log from `dist/` or from the 2.0.0 period is reused.

| File | What | Result |
|---|---|---|
| check-version-before.log | `scripts/check-version.ps1` before the doc fixes (base c7e081b) | exit 1, 15 inconsistencies (expected) |
| check-version-after.log | same script after the doc fixes (working tree, before commit) | exit 0, 6 allowlisted code literals |
| build-attempt1-vs2022-generator-FAILED.log | `build.ps1` default generator "Visual Studio 17 2022" | failed: no Visual Studio instance on this machine (environment, not code) |
| build-and-ctest.log | `build.ps1 -Generator Ninja -VcVarsPath ... -JucePath C:/SDKs/JUCE` (Release): check-version, configure, build, ctest | exit 0, ctest 4/4 passed (aether_dsp, aether_fm, aether_modular, aether_plugin) |
| pluginval-fx.log | pluginval 1.0.4, `--strictness-level 5 --validate` on the new AETHER Micro.vst3 | exit 0, SUCCESS, plugin reports v2.0.1 |
| pluginval-synth.log | same on AETHER Micro Synth.vst3 | exit 0, SUCCESS, plugin reports v2.0.1 |

Not covered: external Steinberg validator path was not supplied (pluginval's "vst3 validator" step is
only what pluginval itself runs); no DAW test; no remote CI run; the release ZIP in `dist/` was not rebuilt
(its Validation folder still holds 2.0.0-era logs except pluginval).