# 2.3.0-final validation summary (integration/2.3.0-final, source commit 325090d)
- build (run-build.ps1, Ninja Release, fresh build-230final): CONFIGURE_EXIT=0, BUILD_EXIT=0 (build-release.log)
- check-version after build: exit 0 (check-version-postbuild.log)
- pluginval 1.0.4 strictness 5 --validate-in-process --skip-gui-tests: Fx exit 0, Synth exit 0 (pluginval-fx.log / pluginval-synth.log)
  - Micro.vst3_*.txt / Synth.vst3_*.txt are from a first invocation with an unquoted path (invalid call, "No types found"); not a plugin result.
- package: scripts/package.ps1 run with PowerShell 7 (PowerShell 5 lacks [IO.Path]::GetRelativePath; package-attempt1-powershell5-failed.log), exit 0
- NOT run on this branch: ctest, AetherRandInvariants, heap poison, 36-preset compare, I-class integration tests.
- Known: RELEASE.json written by package.ps1 still says modulationDestinations 84 / schema 3 (hard-coded in the script, not updated).