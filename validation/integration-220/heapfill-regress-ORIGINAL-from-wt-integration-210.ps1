# Heap-fill regression with AetherReproTool: per sample, baseline = fill 0x00; compare ff/cd/rand(+free) and block sizes against it.
# usage: heapfill-regress.ps1 -Exe <AetherReproTool.exe> -Log <file> [-Label text]
param([Parameter(Mandatory)][string]$Exe,[Parameter(Mandatory)][string]$Log,[string]$Label='')
$exe=(Resolve-Path $Exe).Path
function Clr { Remove-Item Env:AETHER_HEAPFILL,Env:AETHER_HEAPFILL_FREE,Env:AETHER_HEAPSEED -ErrorAction SilentlyContinue }
"# heapfill-regress $Label start $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') Asia/Shanghai exe=$exe built=$((Get-Item $exe).LastWriteTime)" | Out-File $Log -Encoding ascii
$tmp="$env:TEMP\hf_$PID"; New-Item -ItemType Directory -Force $tmp | Out-Null
$samples='factory-12','ab-snapshot-b','custom-graph-fm','default','factory-20','factory-35','factory-00'
$totRuns=0;$totDiff=0
foreach($s in $samples){
  Clr; $env:AETHER_HEAPFILL='00'; $base=(& $exe dump $s --block 256 --out "$tmp\base.bin") -join ' '; Clr
  $nofill=(& $exe dump $s --block 256 --out "$tmp\nofill.bin") -join ' '
  "== $s baseline(fill 0x00, block 256) $base ; no-fill-env run: $nofill (same=$($nofill -eq $base))" | Out-File $Log -Append -Encoding ascii
  $runs=0;$diff=0;$hs=@{}
  $plan=@()
  foreach($m in 'ff','cd'){ foreach($f in '0','1'){ $plan += ,@($m,$f,0,256) } }
  foreach($seed in 0..3){ foreach($f in '0','1'){ $plan += ,@('rand',$f,$seed,256) } }
  foreach($b in 17,257,1000){ $plan += ,@('00','0',0,$b); $plan += ,@('ff','0',0,$b) }
  foreach($pl in $plan){
    Clr; $env:AETHER_HEAPFILL=$pl[0]; $env:AETHER_HEAPFILL_FREE=$pl[1]; $env:AETHER_HEAPSEED=[string]$pl[2]
    $h=(& $exe dump $s --block $pl[3] --out "$tmp\run.bin") -join ' '; Clr
    $runs++; if($hs.ContainsKey($h)){$hs[$h]++}else{$hs[$h]=1}
    if($h -ne $base){ $diff++; $c=& $exe cmp "$tmp\base.bin" "$tmp\run.bin"; "   DIFF fill=$($pl[0]) free=$($pl[1]) seed=$($pl[2]) block=$($pl[3]) $h : $c" | Out-File $Log -Append -Encoding ascii }
  }
  "$s SUMMARY runs=$runs differ_from_baseline=$diff distinct_hashes=$($hs.Count)" | Out-File $Log -Append -Encoding ascii
  $totRuns+=$runs;$totDiff+=$diff
}
"# heapfill-regress $Label end $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') TOTAL runs=$totRuns differ_from_baseline=$totDiff" | Out-File $Log -Append -Encoding ascii
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
