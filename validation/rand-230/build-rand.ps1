# Build + run the aether_rand engine tests (no JUCE code compiled: only targets AetherRandTests / AetherRandInvariants).
# Usage: powershell -File validation\rand-230\build-rand.ps1 [-Targets ...] ; Release, Ninja, portable MSVC.
param([string[]]$Targets = @('AetherRandTests','AetherRandInvariants'), [string]$BuildDir = 'build-rand-230')
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
Set-Location $root
$vc = 'C:\SDKs\AetherBuildTools\VC\Auxiliary\Build\vcvars64.bat'
$envOut = & cmd.exe /d /s /c "`"call `"$vc`" >nul && set`""
foreach ($l in $envOut) { if ($l -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') } }
$ninja = 'C:/SDKs/AetherBuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
if (-not (Test-Path "$BuildDir/CMakeCache.txt")) {
  & cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$ninja" -DJUCE_PATH=C:/SDKs/JUCE
  if ($LASTEXITCODE -ne 0) { throw 'configure failed' }
}
& cmake --build $BuildDir --config Release --target @Targets
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
