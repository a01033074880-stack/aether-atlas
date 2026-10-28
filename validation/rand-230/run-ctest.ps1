# Run the rand-230 ctest items (plus the pre-existing aether_rand) from the already built build-rand-230; logs to validation\rand-230\logs.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Set-Location $root
$vc = 'C:\SDKs\AetherBuildTools\VC\Auxiliary\Build\vcvars64.bat'
$envOut = & cmd.exe /d /s /c "`"call `"$vc`" >nul && set`""
foreach ($l in $envOut) { if ($l -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') } }
$log = Join-Path $PSScriptRoot 'logs'
& ctest --test-dir build-rand-230 -C Release -N | Out-File "$log\ctest-list.log" -Encoding utf8
& ctest --test-dir build-rand-230 -C Release -R '^aether_rand$' --output-on-failure -V 2>&1 | Out-File "$log\ctest-aether_rand.log" -Encoding utf8
& ctest --test-dir build-rand-230 -C Release -L rand-230 --output-on-failure -V 2>&1 | Out-File "$log\ctest-rand-230.log" -Encoding utf8
