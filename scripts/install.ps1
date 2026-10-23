param([string]$Destination = (Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'))
$ErrorActionPreference = 'Stop'
$aetherPackageRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$aetherSource = Join-Path $aetherPackageRoot 'VST3'
if (-not (Test-Path -LiteralPath $aetherSource)) { throw 'Run the install.ps1 provided inside the extracted release package.' }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
foreach ($aetherBundle in Get-ChildItem -LiteralPath $aetherSource -Directory -Filter '*.vst3') {
    $aetherTarget = Join-Path $Destination $aetherBundle.Name
    if (Test-Path -LiteralPath $aetherTarget) {
        $aetherBackup = "$aetherTarget.backup-$(Get-Date -Format 'yyyyMMdd-HHmmss-fff')"
        Move-Item -LiteralPath $aetherTarget -Destination $aetherBackup
    }
    Copy-Item -LiteralPath $aetherBundle.FullName -Destination $aetherTarget -Recurse
    Write-Output "Installed: $aetherTarget"
}
Write-Output 'Rescan VST3 plugins in your DAW. Keep both complete .vst3 directories intact.'
