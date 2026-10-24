<#
.SYNOPSIS
  Version consistency check for AETHER Atlas.

.DESCRIPTION
  Authoritative source: project(AetherMicro VERSION x.y.z ...) in CMakeLists.txt.
  Checks that the current-version documents, scripts and CI agree with it, that
  no stale workspace paths remain, and that hard-coded version strings in
  Source/ and tools/ are either equal to the authoritative version or listed in
  the explicit allowlist below (with the reason they are kept).
  Exit code: 0 = consistent, 1 = at least one mismatch (file:line listed).

  Historical records are deliberately NOT swept: older CHANGELOG sections,
  phase reports, docs/archive, docs/superpowers. References to an older MAJOR
  version (e.g. 1.0.0 release names) are accepted as history; a reference to the
  current major with a different minor/patch (e.g. 2.0.0 while releasing 2.0.1)
  is an error in any swept file.
#>
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$errorsFound = New-Object System.Collections.ArrayList
$allowed = New-Object System.Collections.ArrayList

function Add-Problem([string]$File, [int]$Line, [string]$Message) {
    [void]$errorsFound.Add(("{0}:{1}: {2}" -f $File, $Line, $Message))
}
function Get-Lines([string]$Relative) {
    $path = Join-Path $Root $Relative
    if (-not (Test-Path -LiteralPath $path)) { Add-Problem $Relative 0 'file is missing'; return @() }
    return ,[IO.File]::ReadAllLines($path, $utf8)
}

# ---- authoritative version -------------------------------------------------
$cmake = [IO.File]::ReadAllText((Join-Path $Root 'CMakeLists.txt'), $utf8)
$m = [regex]::Match($cmake, 'project\(\s*AetherMicro\s+VERSION\s+(\d+\.\d+\.\d+)')
if (-not $m.Success) { Write-Host 'ERROR: cannot read project(AetherMicro VERSION x.y.z) from CMakeLists.txt'; exit 2 }
$version = $m.Groups[1].Value
$major = [int]($version.Split('.')[0])
Write-Host "Authoritative version (CMakeLists.txt): $version"

# ---- 1. required current-version anchors ----------------------------------
function Require-Match([string]$Relative, [string]$Pattern, [string]$What, [int]$MaxLine = 0) {
    $lines = Get-Lines $Relative
    $limit = if ($MaxLine -gt 0) { [Math]::Min($MaxLine, $lines.Count) } else { $lines.Count }
    for ($i = 0; $i -lt $limit; $i++) { if ($lines[$i] -match $Pattern) { return } }
    Add-Problem $Relative 1 "expected $What (version $version) was not found"
}
# First '## x.y.z' heading in CHANGELOG is the current version.
$cl = Get-Lines 'CHANGELOG.md'
$clHit = $false
for ($i = 0; $i -lt $cl.Count; $i++) {
    $h = [regex]::Match($cl[$i], '^##\s+(\d+\.\d+\.\d+)')
    if ($h.Success) { $clHit = $true; if ($h.Groups[1].Value -ne $version) { Add-Problem 'CHANGELOG.md' ($i + 1) "newest entry is $($h.Groups[1].Value), expected $version" }; break }
}
if (-not $clHit) { Add-Problem 'CHANGELOG.md' 1 'no version heading found' }

$verRx = [regex]::Escape($version)
Require-Match 'README.md'                 "^#.*Atlas\s+$verRx"        'title "Atlas <version>"' 3
Require-Match 'README.md'                 "AetherAtlas-$verRx-Windows-x64" 'install archive name'
Require-Match 'THIRD_PARTY_NOTICES.md'    "^#.*Atlas\s+$verRx"        'title "Atlas <version>"' 3
Require-Match 'docs/REQUIREMENTS.md'      "^#.*Atlas\s+$verRx"        'title "Atlas <version>"' 3
Require-Match 'docs/ATLAS_ACCEPTANCE.md'  "^#.*Atlas\s+$verRx"        'title "Atlas <version>"' 3
Require-Match 'docs/USER_GUIDE.zh-CN.md'  "^#.*Atlas\s+$verRx"        'title "Atlas <version>"' 3
Require-Match 'docs/VALIDATION.md'        "^#.*Atlas\s+$verRx"        'title "Atlas <version>"' 3
Require-Match 'docs/v2-contract.md'       "Version\s*$verRx"          '"Version <version>"' 5
Require-Match '.github/workflows/build.yml' "name:\s*AetherAtlas-$verRx-Windows-x64" 'artifact name'
Require-Match 'scripts/package.ps1'       "\`$aetherVersion\s*=\s*'$verRx'" '$aetherVersion assignment'

# ---- 2. sweep current-version files for stale versions / stale paths -------
$sweep = @('README.md', 'THIRD_PARTY_NOTICES.md', '.github/workflows/build.yml')
$sweep += (Get-ChildItem -LiteralPath (Join-Path $Root 'scripts') -Filter *.ps1 | Where-Object { $_.Name -ne 'check-version.ps1' } | ForEach-Object { 'scripts/' + $_.Name })
$sweep += (Get-ChildItem -LiteralPath (Join-Path $Root 'docs') -Filter *.md | ForEach-Object { 'docs/' + $_.Name })
$tokenRx = '(?<![\w.])(?:v|Version\s*|Atlas\s+|Aether(?:Atlas|Micro)-)(\d+\.\d+\.\d+)(?![\d.]*\d)'
foreach ($rel in $sweep) {
    $lines = Get-Lines $rel
    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($t in [regex]::Matches($lines[$i], $tokenRx)) {
            $found = $t.Groups[1].Value
            if ($found -eq $version) { continue }
            if ([int]($found.Split('.')[0]) -lt $major) { continue }   # older major = history
            Add-Problem $rel ($i + 1) "stale version '$found' (expected $version): $($t.Value)"
        }
    }
}
# Stale developer-workspace paths anywhere in docs, reports, scripts and CI.
$pathFiles = $sweep + @('AETHER_Micro_Phase_1_Development_Report.zh-CN.md', 'AETHER_Atlas_Phase_2_Development_Report.zh-CN.md')
foreach ($rel in $pathFiles) {
    $lines = Get-Lines $rel
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match 'ChatGPT' -or $lines[$i] -match '[A-Za-z]:[/\\]+Users[/\\]+[^/\\\s]+[/\\]+Documents') {
            Add-Problem $rel ($i + 1) 'absolute developer workspace path; use a repository-relative path'
        }
    }
}

# ---- 3. hard-coded version literals in code --------------------------------
# Intentional exceptions. Each entry: file, regex on the line, reason.
# These are NOT silently ignored: they are printed as ALLOWED so the debt stays visible.
$codeAllow = @(
    @{ File = 'Source/PluginProcessor.cpp'; Line = 'setProperty\("version","2\.0\.0"';
       Reason = 'host-state ValueTree "version" is write-only informational metadata; compatibility is decided by the integer "schema" (1..2). Kept so this patch release does not touch code; follow-up: derive from JucePlugin_VersionString.' },
    @{ File = 'Source/PresetManager.cpp'; Line = 'setProperty\("version","2\.0\.0"';
       Reason = '.aether preset JSON "version" is informational (loadPreset checks product + schema only). Same follow-up as above.' },
    @{ File = 'Source/PluginEditor.cpp'; Line = '"2\.0\.0\s+/\s+"';
       Reason = 'editor footer DISPLAY text. File is owned by the UI branch (ui/min-size-fix); change it there, not in this docs/scripts patch.' },
    @{ File = 'tools/RenderAtlas.cpp'; Line = '\\"version\\":\s*\\"2\.0\.0\\"';
       Reason = 'version field of the generated Atlas-module-catalog.json (tool output metadata); tools/ code is out of scope for this patch.' }
)
$codeFiles = Get-ChildItem -LiteralPath (Join-Path $Root 'Source'), (Join-Path $Root 'tools') -Recurse -File -Include *.cpp, *.h, *.hpp, *.mm
$literalRx = '"(\d+\.\d+\.\d+)"|\\"(\d+\.\d+\.\d+)\\"|"(\d+\.\d+\.\d+)\s+/\s+"'
$rootFull = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\')
foreach ($f in $codeFiles) {
    $rel = $f.FullName.Substring($rootFull.Length + 1).Replace('\', '/')
    $lines = [IO.File]::ReadAllLines($f.FullName, $utf8)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($t in [regex]::Matches($lines[$i], $literalRx)) {
            $found = ($t.Groups[1].Value + $t.Groups[2].Value + $t.Groups[3].Value)
            if ($found -eq $version) { continue }
            $rule = $codeAllow | Where-Object { $_.File -eq $rel -and $lines[$i] -match $_.Line } | Select-Object -First 1
            if ($rule) { [void]$allowed.Add(("{0}:{1}: literal '{2}' allowed: {3}" -f $rel, ($i + 1), $found, $rule.Reason)) }
            else { Add-Problem $rel ($i + 1) "hard-coded version literal '$found' (expected $version) and not in the allowlist" }
        }
    }
}

# ---- report ----------------------------------------------------------------
foreach ($a in ($allowed | Select-Object -Unique)) { Write-Host "ALLOWED  $a" }
if ($errorsFound.Count -gt 0) {
    foreach ($e in $errorsFound) { Write-Host "MISMATCH $e" }
    Write-Host "FAIL: $($errorsFound.Count) version inconsistency(ies) against $version."
    exit 1
}
Write-Host "OK: all checked files are consistent with version $version ($($allowed.Count) allowlisted code literal(s))."
exit 0