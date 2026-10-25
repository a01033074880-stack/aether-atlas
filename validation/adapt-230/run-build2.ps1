$ErrorActionPreference='Continue'
Set-Location C:\Users\24168\Documents\Projects\wt-adapt-230
. C:\Users\24168\Documents\Projects\wt-integration-210\validation\delay-fix\env.ps1
$V='validation\adapt-230'
Get-Date -Format o | Out-File "$V\build2-stdout.log"
cmake -S . -B build-adapt-230 -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH=C:\SDKs\JUCE *>> "$V\build2-stdout.log"
"configure exit=$LASTEXITCODE" | Out-File "$V\build2-stdout.log" -Append
cmake --build build-adapt-230 --target AetherModContractFx AetherModContractSynth *>> "$V\build2-stdout.log"
"build exit=$LASTEXITCODE" | Out-File "$V\build2-stdout.log" -Append
Get-Date -Format o | Out-File "$V\build2-stdout.log" -Append
'DONE' | Out-File "$V\build2-done.flag"
