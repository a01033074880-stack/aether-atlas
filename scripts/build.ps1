param(
    [string]$BuildDirectory = 'build',
    [string]$JucePath = '',
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [int]$Jobs = 4
)
$ErrorActionPreference = 'Stop'
$aetherRoot = Split-Path -Parent $PSScriptRoot
Push-Location $aetherRoot
try {
    $aetherConfigure = @('-S','.', '-B',$BuildDirectory,'-G','Visual Studio 17 2022','-A','x64')
    if ($JucePath) { $aetherConfigure += "-DJUCE_PATH=$JucePath" }
    & cmake @aetherConfigure
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $BuildDirectory --config $Configuration --parallel $Jobs
    if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }
    & ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
} finally { Pop-Location }
