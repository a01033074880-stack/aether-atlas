param([string]$BuildDir='build-repro',[string[]]$Targets=@('AetherReproTool','AetherFixtureTests'),[string[]]$Extra=@(),[string]$Config='Release')
$ErrorActionPreference='Continue'
. "$PSScriptRoot\env.ps1"
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
"# configure $BuildDir $Config extra=[$Extra] $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') Asia/Shanghai"
& cmake -S . -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_MAKE_PROGRAM=C:/SDKs/AetherBuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe" -DJUCE_PATH=C:/SDKs/JUCE @Extra
"CONFIGURE_EXIT=$LASTEXITCODE"
if($LASTEXITCODE -ne 0){ exit $LASTEXITCODE }
foreach($t in $Targets){ & cmake --build $BuildDir --target $t --parallel 8; "BUILD_EXIT[$t]=$LASTEXITCODE" }
"# done $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
