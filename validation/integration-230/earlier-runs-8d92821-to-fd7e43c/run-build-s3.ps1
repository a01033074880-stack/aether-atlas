Set-Location C:\Users\24168\Documents\Projects\wt-integration-230
. C:\Users\24168\Documents\Projects\wt-integration-210\validation\delay-fix\env.ps1
$V='validation\integration-230'
Get-Date -Format o | Out-File "$V\s3-build.log"
"HEAD=$(git rev-parse --short HEAD)" | Out-File "$V\s3-build.log" -Append
cmake --build build-integ-230 --parallel 4 *>> "$V\s3-build.log"
"build exit=$LASTEXITCODE" | Out-File "$V\s3-build.log" -Append
Get-Date -Format o | Out-File "$V\s3-build.log" -Append
Get-ChildItem build-integ-230 -Recurse -Include AetherReproTool.exe,AetherRenderPresets.exe,'AETHER Micro.vst3','AETHER Micro Synth.vst3',PluginProcessor.cpp.obj,*ResynthSplitTests.cpp.obj,*ModContract*.exe,*ModTargets*.exe | ForEach-Object { "$($_.FullName) | $($_.LastWriteTime.ToString('o'))" } | Out-File "$V\s3-build-artefacts.txt" -Encoding ascii
'DONE' | Out-File "$V\s3-build-done.flag"
