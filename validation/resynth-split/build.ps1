param(
    [string[]]$Targets = @('AetherResynthSplitFx','AetherResynthSplitSynth'),
    [string]$BuildDirectory = 'build-split',
    [string]$Log = 'validation\resynth-split\build.log'
)
# Builds with the same toolchain as the 2.1.0 integration builds (Ninja, MSVC 14.44, JUCE 8.0.12 at C:\SDKs\JUCE, Release).
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot | Split-Path -Parent
Set-Location $root
$Targets = $Targets -split ','
$vc = 'C:\SDKs\AetherBuildTools\VC\Auxiliary\Build\vcvars64.bat'
$ninja = 'C:\SDKs\AetherBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$envLines = & cmd.exe /d /s /c "`"call `"$vc`" >nul && set`""
foreach ($l in $envLines) { if ($l -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') } }
"=== configure $(Get-Date -Format o)" | Tee-Object -FilePath $Log
& cmake -S . -B $BuildDirectory -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=$ninja" -DJUCE_PATH=C:/SDKs/JUCE 2>&1 | Tee-Object -FilePath $Log -Append
"CONFIGURE_EXIT=$LASTEXITCODE" | Tee-Object -FilePath $Log -Append
"=== build $($Targets -join ' ') $(Get-Date -Format o)" | Tee-Object -FilePath $Log -Append
& cmake --build $BuildDirectory --target @Targets --parallel 12 2>&1 | Tee-Object -FilePath $Log -Append
"BUILD_EXIT=$LASTEXITCODE" | Tee-Object -FilePath $Log -Append
"=== done $(Get-Date -Format o)" | Tee-Object -FilePath $Log -Append

