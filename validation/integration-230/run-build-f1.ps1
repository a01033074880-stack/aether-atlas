$ErrorActionPreference='Continue'
Set-Location C:\Users\24168\Documents\Projects\wt-integration-230
. C:\Users\24168\Documents\Projects\wt-integration-210\validation\delay-fix\env.ps1
$V='validation\integration-230'
Get-Date -Format o | Out-File "$V\f1-build.log"
"HEAD=$env:AETHER_HEAD" | Out-File "$V\f1-build.log" -Append
cmake --build build-integ-230 --parallel 4 *>> "$V\f1-build.log"
"build exit=$LASTEXITCODE" | Out-File "$V\f1-build.log" -Append
Get-Date -Format o | Out-File "$V\f1-build.log" -Append
Get-ChildItem build-integ-230 -Recurse -Include AetherReproTool.exe,AetherRenderPresets.exe,'AETHER Micro.vst3','AETHER Micro Synth.vst3',AetherModContract*.exe,AetherModTargets*.exe,AetherPluginTests*.exe,AetherFixtureTests.exe,AetherResynthSplit*.exe,*ResynthSplitTests.cpp.obj,PluginProcessor.cpp.obj,PluginEditor.cpp.obj | Where-Object { -not $_.PSIsContainer -or $_.Name -like '*.vst3' } | ForEach-Object { "$($_.FullName) | $($_.LastWriteTime.ToString('o'))" } | Out-File "$V\f1-build-artefacts.txt" -Encoding ascii
'DONE' | Out-File "$V\f1-build-done.flag"
