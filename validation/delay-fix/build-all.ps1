# Fresh configure + full build of every target in a new build dir (build-fixfix), Release. Usage: build-all.ps1 [-BuildDir build-fixfix]
param([string]$BuildDir='build-fixfix',[string]$Config='Release')
$ErrorActionPreference='Continue'
. "$PSScriptRoot\env.ps1"
Set-Location (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
"# configure $BuildDir $Config commit $(git rev-parse --short HEAD) $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') Asia/Shanghai"
if(Test-Path $BuildDir){ "# build dir already exists: refusing (fresh build required)"; exit 3 }
& cmake -S . -B $BuildDir -G Ninja "-DCMAKE_BUILD_TYPE=$Config" "-DCMAKE_MAKE_PROGRAM=C:/SDKs/AetherBuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe" -DJUCE_PATH=C:/SDKs/JUCE
"CONFIGURE_EXIT=$LASTEXITCODE"
if($LASTEXITCODE -ne 0){ exit $LASTEXITCODE }
& cmake --build $BuildDir --parallel 12
"BUILD_EXIT=$LASTEXITCODE"
"# done $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
exit $LASTEXITCODE
