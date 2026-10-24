# Run the fixture test executable in N fresh processes, strict determinism. Optionally under CPU load (spinner processes).
# usage: strict-runs.ps1 -Exe <AetherFixtureTests.exe> -Log <file> -N 10 [-Spinners 24] [-Label text] [-Append]
param([Parameter(Mandatory)][string]$Exe,[Parameter(Mandatory)][string]$Log,[int]$N=10,[int]$Spinners=0,[string]$Label='',[switch]$Append)
$exe=(Resolve-Path $Exe).Path
$env:AETHER_STRICT_RENDER_DETERMINISM='1'
$hdr="# strict-runs $Label start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') Asia/Shanghai exe=$exe built=$((Get-Item $exe).LastWriteTime) N=$N spinners=$Spinners AETHER_STRICT_RENDER_DETERMINISM=1"
if($Append){ $hdr | Out-File $Log -Append -Encoding ascii } else { $hdr | Out-File $Log -Encoding ascii }
$spin=@()
if($Spinners -gt 0){ for($i=0;$i -lt $Spinners;$i++){ $spin += Start-Process pwsh -ArgumentList '-NoProfile','-Command','while($true){}' -WindowStyle Hidden -PassThru }; Start-Sleep 5 }
$pass=0;$fail=0
try {
  for($k=1;$k -le $N;$k++){
    $cpu=[math]::Round((Get-Counter '\Processor(_Total)\% Processor Time' -ErrorAction SilentlyContinue).CounterSamples.CookedValue,0)
    $o="$env:TEMP\strict_$PID`_$k.out"; $e="$env:TEMP\strict_$PID`_$k.err"
    $t0=Get-Date
    $p=Start-Process -FilePath $exe -Wait -PassThru -NoNewWindow -RedirectStandardOutput $o -RedirectStandardError $e
    $txt=((Get-Content $o,$e -ErrorAction SilentlyContinue) -join ' | ')
    $res=[regex]::Match($txt,'RESULT \d+ tests passed, \d+ failed').Value
    $ok=($p.ExitCode -eq 0)
    if($ok){$pass++}else{$fail++}
    $fl=([regex]::Matches($txt,'FAIL [^|]*') | ForEach-Object { $_.Value.Trim() }) -join ' ;; '
    "run $k exit=$($p.ExitCode) $(if($ok){'PASS'}else{'FAIL'}) cpu_before=${cpu}% secs=$([math]::Round(((Get-Date)-$t0).TotalSeconds,0)) $res $fl" | Out-File $Log -Append -Encoding ascii
  }
} finally { foreach($s in $spin){ try{ Stop-Process -Id $s.Id -Force }catch{} } }
"# strict-runs $Label end $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') SUMMARY processes=$N pass=$pass fail=$fail spinners=$Spinners" | Out-File $Log -Append -Encoding ascii
