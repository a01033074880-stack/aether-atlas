# Dump each sample with the PRE-fix (f688c6d) and POST-fix ReproTool at heap fill 0x00, block 256, and compare bitwise.
param([string]$Pre='..\..\..\wt-fixfix-pre\build-fixfix\AetherReproTool_artefacts\Release\AetherReproTool.exe',[string]$Post='..\..\build-fixfix\AetherReproTool_artefacts\Release\AetherReproTool.exe',[string]$Log='prepost-diff.log')
$pre=(Resolve-Path (Join-Path $PSScriptRoot $Pre)).Path; $post=(Resolve-Path (Join-Path $PSScriptRoot $Post)).Path
$log=Join-Path $PSScriptRoot $Log; $t="$env:TEMP\prepost_$PID"; New-Item -ItemType Directory -Force $t | Out-Null
"# prepost $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') Asia/Shanghai pre=$pre post=$post (fill 0x00, block 256, 96000 frames @48k)" | Out-File $log -Encoding ascii
$env:AETHER_HEAPFILL='00'
foreach($s in 'factory-00','factory-05','factory-11','factory-12','factory-20','factory-35','ab-snapshot-b','custom-graph-fm','default','Gentle Orbit','Clean Canvas'){
  $a=(& $pre dump $s --block 256 --out "$t\a.bin") -join ' '; $b=(& $post dump $s --block 256 --out "$t\b.bin") -join ' '
  "== $s pre:[$a] post:[$b]" | Out-File $log -Append -Encoding ascii
  python (Join-Path $PSScriptRoot 'prepost.py') "$t\a.bin" "$t\b.bin" $s | Out-File $log -Append -Encoding ascii
}
Remove-Item Env:AETHER_HEAPFILL; Remove-Item -Recurse -Force $t
