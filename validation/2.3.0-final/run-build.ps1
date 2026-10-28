# Same steps as scripts/build.ps1 (check-version, configure, build) WITHOUT its trailing ctest (per run plan: ctest not run on this branch). Ninja + portable MSVC, Release, fresh build dir.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Set-Location $root
& (Join-Path $root 'scripts\check-version.ps1'); "CHECKVERSION_EXIT=$LASTEXITCODE"
$vc = 'C:\SDKs\AetherBuildTools\VC\Auxiliary\Build\vcvars64.bat'
$envOut = & cmd.exe /d /s /c "`"call `"$vc`" >nul && set`""
foreach ($l in $envOut) { if ($l -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') } }
$ninja = 'C:/SDKs/AetherBuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
if (Test-Path build-230final) { throw 'build-230final already exists' }
& cmake -S . -B build-230final -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$ninja" -DJUCE_PATH=C:/SDKs/JUCE
"CONFIGURE_EXIT=$LASTEXITCODE"
if ($LASTEXITCODE -ne 0) { exit 1 }
& cmake --build build-230final --config Release --parallel 4
"BUILD_EXIT=$LASTEXITCODE"
exit $LASTEXITCODE