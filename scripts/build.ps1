param(
    [string]$BuildDirectory = 'build',
    [string]$JucePath = '',
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [int]$Jobs = 4,
    [ValidateSet('Visual Studio 17 2022','Ninja')][string]$Generator = 'Visual Studio 17 2022',
    [string]$NinjaPath = '',
    [string]$VcVarsPath = ''
)
$ErrorActionPreference = 'Stop'
$aetherRoot = Split-Path -Parent $PSScriptRoot
Push-Location $aetherRoot
try {
    if ($VcVarsPath) {
        if (-not (Test-Path -LiteralPath $VcVarsPath)) { throw 'vcvars64.bat path does not exist.' }
        $aetherEnvironment = & cmd.exe /d /s /c "`"call `"$VcVarsPath`" >nul && set`""
        foreach ($aetherLine in $aetherEnvironment) {
            if ($aetherLine -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') }
        }
    }
    $aetherConfigure = @('-S','.', '-B',$BuildDirectory,'-G',$Generator)
    if ($Generator -eq 'Ninja') {
        $aetherConfigure += "-DCMAKE_BUILD_TYPE=$Configuration"
        if ($NinjaPath) { $aetherConfigure += "-DCMAKE_MAKE_PROGRAM=$NinjaPath" }
    } else { $aetherConfigure += @('-A','x64') }
    if ($JucePath) { $aetherConfigure += "-DJUCE_PATH=$JucePath" }
    & cmake @aetherConfigure
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $BuildDirectory --config $Configuration --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
} finally { Pop-Location }
