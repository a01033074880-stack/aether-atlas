# Fresh-process repeat: N separate processes each render once; report sha per process and diff of each vs process 0.
param([Parameter(Mandatory)][string]$Sample,[int]$Block=256,[int]$N=20,[string]$Exe='build-repro\AetherReproTool_artefacts\Release\AetherReproTool.exe',[string[]]$Extra=@(),[string]$Tmp="$env:TEMP\aether-repro")
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot); Set-Location $root
New-Item -ItemType Directory $Tmp -Force | Out-Null
$exePath=(Resolve-Path $Exe).Path
"fresh-process sample=$Sample block=$Block N=$N extra=[$Extra] heapfill=[$env:AETHER_HEAPFILL] free=[$env:AETHER_HEAPFILL_FREE] $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') Asia/Shanghai"
$hashes=@{}; $differ=0
for($k=0;$k -lt $N;$k++){
  $f="$Tmp\run$k.bin"
  if($env:AETHER_HEAPFILL -eq 'rand'){ $env:AETHER_HEAPSEED=[string]$k }
  $p=Start-Process -FilePath $exePath -ArgumentList (@('dump',$Sample,'--block',$Block,'--out',$f)+$Extra) -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$Tmp\out$k.txt"
  $sha=((Get-Content "$Tmp\out$k.txt") -join ' ')
  if($p.ExitCode -ne 0){ "run$k exit=$($p.ExitCode) $sha"; continue }
  if($hashes.ContainsKey($sha)){ $hashes[$sha]++ } else { $hashes[$sha]=1 }
  if($k -gt 0 -and (Test-Path "$Tmp\run0.bin")){
    $c=& $exePath cmp "$Tmp\run0.bin" $f
    if($c -notmatch 'identical'){ $differ++; "run$k $sha $c" }
  }
}
"SUMMARY processes=$N differing_from_run0=$differ distinct_hashes=$($hashes.Count): " + (($hashes.GetEnumerator() | ForEach-Object { "$($_.Key) x$($_.Value)" }) -join ', ')
