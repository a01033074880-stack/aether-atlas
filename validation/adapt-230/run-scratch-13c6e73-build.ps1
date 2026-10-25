$ErrorActionPreference='Continue'
Set-Location C:\Users\24168\Documents\Projects\wt-scratch-13c6e73
. C:\Users\24168\Documents\Projects\wt-integration-210\validation\delay-fix\env.ps1
$V='C:\Users\24168\Documents\Projects\wt-adapt-230\validation\adapt-230'
Get-Date -Format o | Out-File "$V\scratch-13c6e73-build.log"
cmake -S . -B build-scratch -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=C:\SDKs\JUCE *>> "$V\scratch-13c6e73-build.log"
"configure exit=$LASTEXITCODE" | Out-File "$V\scratch-13c6e73-build.log" -Append
cmake --build build-scratch --target AetherModContractFx AetherModContractSynth *>> "$V\scratch-13c6e73-build.log"
"build exit=$LASTEXITCODE" | Out-File "$V\scratch-13c6e73-build.log" -Append
'DONE' | Out-File "$V\scratch-13c6e73-build.done"
