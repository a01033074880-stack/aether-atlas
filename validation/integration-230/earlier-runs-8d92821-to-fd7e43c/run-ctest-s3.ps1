Set-Location C:\Users\24168\Documents\Projects\wt-integration-230
. C:\Users\24168\Documents\Projects\wt-integration-210\validation\delay-fix\env.ps1
$V='validation\integration-230'
Get-Date -Format o | Out-File "$V\s3-ctest-all.log"
"HEAD=$(git rev-parse --short HEAD)" | Out-File "$V\s3-ctest-all.log" -Append
ctest --test-dir build-integ-230 -C Release --output-on-failure --timeout 5400 *>> "$V\s3-ctest-all.log"
"ctest exit=$LASTEXITCODE" | Out-File "$V\s3-ctest-all.log" -Append
Get-Date -Format o | Out-File "$V\s3-ctest-all.log" -Append
'DONE' | Out-File "$V\s3-ctest-done.flag"
