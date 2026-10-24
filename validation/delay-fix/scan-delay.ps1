# Extract the two Delay::read bodies verbatim from git, compile DelayScan.cpp with them, run it, log to validation\delay-fix\delay-scan.log.
param([string[]]$ScanArgs=@())
. "$PSScriptRoot\env.ps1"
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot); Set-Location $root
$out="$root\build-delayscan"; New-Item -ItemType Directory -Force $out | Out-Null
function ExtractDelay($text){ $m=[regex]::Match($text,'(?s)struct Delay \{.*?\r?\n\};'); if(-not $m.Success){throw 'Delay struct not found'}; $m.Value }
$old=ExtractDelay ((git show integration/2.0.1-rc:libs/aether-modular/src/ModuleState.h) -join "`n")
$new=ExtractDelay ([IO.File]::ReadAllText("$root\libs\aether-modular\src\ModuleState.h"))
Set-Content "$out\delay_old.h" $old -Encoding ascii; Set-Content "$out\delay_new.h" $new -Encoding ascii
Copy-Item tests\repro\DelayScan.cpp $out\DelayScan.cpp -Force
Push-Location $out
& cl.exe /nologo /O2 /std:c++20 /EHsc /fp:precise /I. DelayScan.cpp /Fe:DelayScan.exe 2>&1 | Select-Object -Last 8
Pop-Location
$log="validation\delay-fix\delay-scan.log"
"# delay-scan start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') Asia/Shanghai commit $(git rev-parse --short HEAD) args=[$ScanArgs]" | Out-File $log -Encoding ascii
"# OLD Delay::read extracted from integration/2.0.1-rc, NEW from working tree (diff old->new below)" | Out-File $log -Append -Encoding ascii
(Compare-Object ($old -split "`r?`n") ($new -split "`r?`n") | ForEach-Object { "$($_.SideIndicator) $($_.InputObject)" }) | Out-File $log -Append -Encoding ascii
& "$out\DelayScan.exe" @ScanArgs 2>&1 | Out-File $log -Append -Encoding ascii
"# delay-scan end $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') exit=$LASTEXITCODE" | Out-File $log -Append -Encoding ascii
