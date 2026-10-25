Set-Location C:\Users\24168\Documents\Projects\wt-adapt-230
. C:\Users\24168\Documents\Projects\wt-integration-210\validation\delay-fix\env.ps1
$V='validation\adapt-230'
cmake --build build-adapt-230 --target AetherFixtureTests AetherResynthSplitFx AetherResynthSplitSynth *> "$V\ctest-prebuild.log"
ctest --test-dir build-adapt-230 -R "aether_fixtures|aether_split_unchanged|aether_resynth_split" --output-on-failure *> "$V\ctest-existing.log"
ctest --test-dir build-adapt-230 -R "aether_modtargets" --output-on-failure *> "$V\ctest-modtargets.log"
'DONE' | Out-File "$V\ctest-done.flag"
