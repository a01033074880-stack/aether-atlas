param(
    [string]$BuildDirectory = 'build',
    [string]$Configuration = 'Release',
    [string]$JuceSourceZip = '',
    [string]$JucePath = '',
    [string]$ValidationDirectory = 'tmp/validation-v2',
    [string]$ScreenshotsDirectory = 'out/atlas-screenshots',
    [string]$AudioDirectory = 'out/atlas-audio'
)
$ErrorActionPreference = 'Stop'
$aetherRoot = Split-Path -Parent $PSScriptRoot
Push-Location $aetherRoot
try {
    $aetherVersion = '2.0.0'
    $aetherName = "AetherAtlas-$aetherVersion-Windows-x64"
    $aetherRelease = Join-Path $aetherRoot "dist/$aetherName"
    # Never replace a prior reviewable release silently.
    if (Test-Path -LiteralPath $aetherRelease) { throw "Release directory already exists: $aetherRelease" }
    foreach ($aetherDir in @('VST3','Standalone','presets','docs','licenses','Source','Validation','Preview','Audio')) {
        New-Item -ItemType Directory -Path (Join-Path $aetherRelease $aetherDir) -Force | Out-Null
    }
    foreach ($aetherVariant in @(@('AetherMicro','AETHER Micro'),@('AetherMicroSynth','AETHER Micro Synth'))) {
        $aetherArtifacts = Join-Path $BuildDirectory "$($aetherVariant[0])_artefacts/$Configuration"
        $aetherBundle = Join-Path $aetherArtifacts "VST3/$($aetherVariant[1]).vst3"
        $aetherExecutable = Join-Path $aetherArtifacts "Standalone/$($aetherVariant[1]).exe"
        if (-not (Test-Path -LiteralPath $aetherBundle) -or -not (Test-Path -LiteralPath $aetherExecutable)) { throw 'Build artifacts missing.' }
        Copy-Item -LiteralPath $aetherBundle -Destination (Join-Path $aetherRelease 'VST3') -Recurse
        Copy-Item -LiteralPath $aetherExecutable -Destination (Join-Path $aetherRelease 'Standalone')
    }
    # Import libraries/debug build products are not runtime bundle contents.
    Get-ChildItem -LiteralPath (Join-Path $aetherRelease 'VST3') -Recurse -File |
        Where-Object Extension -In '.lib','.exp','.pdb' | Remove-Item
    Copy-Item -LiteralPath 'scripts/install.ps1' -Destination $aetherRelease
    Copy-Item -LiteralPath 'README.md','CHANGELOG.md','LICENSE','THIRD_PARTY_NOTICES.md' -Destination $aetherRelease
    Get-ChildItem -LiteralPath docs -File -Filter '*.md' | Copy-Item -Destination (Join-Path $aetherRelease 'docs')
    Copy-Item -LiteralPath 'docs/archive' -Destination (Join-Path $aetherRelease 'docs') -Recurse
    Copy-Item -LiteralPath 'AETHER_Atlas_Phase_2_Development_Report.zh-CN.md' -Destination $aetherRelease
    $aetherReadme = Join-Path $aetherRelease 'README.md'
    $aetherReadmeText = [IO.File]::ReadAllText($aetherReadme).Replace('out/atlas-screenshots/','Preview/')
    [IO.File]::WriteAllText($aetherReadme,$aetherReadmeText,[Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath 'assets/atlas-design-board.png' -Destination (Join-Path $aetherRelease 'Preview')
    if (Test-Path -LiteralPath "$ScreenshotsDirectory/presets") { Get-ChildItem -LiteralPath "$ScreenshotsDirectory/presets" -File | Copy-Item -Destination (Join-Path $aetherRelease 'presets') }
    if (Test-Path -LiteralPath $ScreenshotsDirectory) { Get-ChildItem -LiteralPath $ScreenshotsDirectory -Filter '*.png' | Copy-Item -Destination (Join-Path $aetherRelease 'Preview') }
    if (Test-Path -LiteralPath $AudioDirectory) { Get-ChildItem -LiteralPath $AudioDirectory -Filter '*.wav' | Copy-Item -Destination (Join-Path $aetherRelease 'Audio') }
    if (Test-Path -LiteralPath "$AudioDirectory/Atlas-module-catalog.json") { Copy-Item -LiteralPath "$AudioDirectory/Atlas-module-catalog.json" -Destination (Join-Path $aetherRelease 'docs') }
    if (Test-Path -LiteralPath $ValidationDirectory) {
        foreach ($aetherEvidence in @('ctest-final.log','dsp-tests-final.log','fm-tests-final.log','modular-tests-final.log','plugin-tests.log','render-benchmark.log','dependencies-fx.log','pluginval-fx','pluginval-synth')) {
            $aetherEvidencePath = Join-Path $ValidationDirectory $aetherEvidence
            if (Test-Path -LiteralPath $aetherEvidencePath) { Copy-Item -LiteralPath $aetherEvidencePath -Destination (Join-Path $aetherRelease 'Validation') -Recurse }
        }
    }
    $aetherSourceArchive = Join-Path $aetherRelease "Source/AetherMicro-$aetherVersion-source.zip"
    & git archive --format=zip "--prefix=AetherMicro-$aetherVersion/" "--output=$aetherSourceArchive" HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Source archive failed.' }
    if ($JuceSourceZip) {
        if ((Get-FileHash -LiteralPath $JuceSourceZip -Algorithm SHA256).Hash -ne '66BCD380BA444DA6A9BB98B7471727C542232C27647C1EC22F47158353C186DC') { throw 'JUCE source SHA-256 mismatch.' }
        Copy-Item -LiteralPath $JuceSourceZip -Destination (Join-Path $aetherRelease 'Source/JUCE-501c076-source.zip')
    } else { throw 'Provide pinned JUCE source ZIP for complete source distribution.' }
    if ($JucePath) {
        Copy-Item -LiteralPath (Join-Path $JucePath 'LICENSE.md') -Destination (Join-Path $aetherRelease 'licenses/JUCE-LICENSE.md')
        Copy-Item -LiteralPath (Join-Path $JucePath 'modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt') -Destination (Join-Path $aetherRelease 'licenses/VST3-SDK-LICENSE.txt')
    }
    foreach ($aetherNotice in @('third_party/DaisySP/LICENSE','third_party/signalsmith-stretch/LICENSE.txt','third_party/signalsmith-linear/LICENSE.txt','third_party/chowdsp_wdf/LICENSE','assets/fonts/InstrumentSerif-OFL.txt','assets/fonts/DMMono-OFL.txt')) {
        $aetherNoticeName = ($aetherNotice -replace '[\\/]','_')
        Copy-Item -LiteralPath $aetherNotice -Destination (Join-Path $aetherRelease "licenses/$aetherNoticeName")
    }
    [IO.File]::WriteAllText((Join-Path $aetherRelease 'licenses/README.txt'),
        "Upstream license documents retain their original text and source-relative links. For JUCE-LICENSE.md links, extract Source/JUCE-501c076-source.zip and read the same LICENSE.md at that source root. For Atlas DSP/font licenses, the complete original source and notices are in Source/AetherMicro-2.0.0-source.zip. These archives are part of this distribution.`n",[Text.UTF8Encoding]::new($false))
    $aetherReleaseMetadata = [ordered]@{
        product = 'AETHER Micro - Atlas'
        version = $aetherVersion
        sourceCommit = ((& git rev-parse HEAD) | Out-String).Trim()
        expectedTag = "v$aetherVersion"
        platform = 'Windows x64'
        modules = 20
        slots = 12
        routingAlgorithms = 32
        fmOperators = 6
        fmVoices = 16
        fmAlgorithms = 32
        modulationRows = 16
        modulationDestinations = 84
        factoryPresets = 36
        schema = 2
    }
    [IO.File]::WriteAllText((Join-Path $aetherRelease 'RELEASE.json'),($aetherReleaseMetadata | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    $aetherManifest = Join-Path $aetherRelease 'SHA256SUMS.txt'
    $aetherChecksums = Get-ChildItem -LiteralPath $aetherRelease -Recurse -File | Sort-Object FullName | ForEach-Object {
        $aetherRelative = [IO.Path]::GetRelativePath($aetherRelease,$_.FullName).Replace([IO.Path]::DirectorySeparatorChar,[char]'/')
        "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $aetherRelative"
    }
    [IO.File]::WriteAllLines($aetherManifest,$aetherChecksums,[Text.UTF8Encoding]::new($false))
    $aetherZip = Join-Path $aetherRoot "dist/$aetherName.zip"
    Compress-Archive -LiteralPath $aetherRelease -DestinationPath $aetherZip
    $aetherZipHash = (Get-FileHash -LiteralPath $aetherZip -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$aetherZip.sha256","$aetherZipHash  $aetherName.zip`n",[Text.UTF8Encoding]::new($false))
    Write-Output $aetherZip
} finally { Pop-Location }
