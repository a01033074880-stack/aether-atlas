# Phase validation: check-version, pluginval x2, heap-fill (original 7 samples + new-destination scenarios), 36-preset compare vs 991e493.
param([Parameter(Mandatory)][string]$Tag,[string]$Build='build-integ-230')
$ErrorActionPreference='Continue'
Set-Location C:\Users\24168\Documents\Projects\wt-integration-230
. C:\Users\24168\Documents\Projects\wt-integration-210\validation\delay-fix\env.ps1
$V='validation\integration-230'; $head=$env:AETHER_HEAD
function Stamp($m){ "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') (Asia/Shanghai) $m" | Out-File "$V\$Tag-run.log" -Append -Encoding ascii }
Stamp "start tag=$Tag HEAD=$head build=$Build"
# 4 check-version
powershell -NoProfile -File scripts\check-version.ps1 *> "$V\$Tag-check-version.log"; Stamp "check-version exit=$LASTEXITCODE"
# 3 pluginval
$pv='C:\Users\24168\Documents\Projects\vst3development\tmp\tools\pluginval\pluginval.exe'
foreach($p in @(@('micro',"$Build\AetherMicro_artefacts\Release\VST3\AETHER Micro.vst3"),@('synth',"$Build\AetherMicroSynth_artefacts\Release\VST3\AETHER Micro Synth.vst3"))){
  $path=(Resolve-Path $p[1]).Path; $log="$V\$Tag-pluginval-$($p[0]).log"; $err="$V\$Tag-pluginval-$($p[0]).err"
  $proc=Start-Process -FilePath $pv -ArgumentList '--strictness-level','5','--validate-in-process','--skip-gui-tests','--validate',"`"$path`"" -Wait -PassThru -NoNewWindow -RedirectStandardOutput $log -RedirectStandardError $err
  $null=$proc.Handle; $proc.WaitForExit(); Stamp "pluginval $($p[0]) exit=$($proc.ExitCode) vst3=$path built=$((Get-Item $path).LastWriteTime)"
}
# 5 heap-fill
$tool="$Build\AetherReproTool_artefacts\Release\AetherReproTool.exe"; Stamp "ReproTool built=$((Get-Item $tool).LastWriteTime)"
& "$V\heapfill-regress-230.ps1" -Exe $tool -Log "$V\$Tag-heapfill-original-7samples.log" -Label "$Tag original 7 samples"
Stamp "heapfill original done: $(Select-String -Path "$V\$Tag-heapfill-original-7samples.log" -Pattern 'TOTAL' | % Line)"
$sc=@{
 'A-pg'   = @('--set','global.mode=1','--set','graph.enabled=1','--set','source.enabled=1','--set','source.engine=1','--set','modulation.1.source=1','--set','modulation.1.target=85','--set','modulation.2.source=2','--set','modulation.2.target=90','--set','modulation.3.source=5','--set','modulation.3.target=88');
 'B-chaos'= @('--set','global.mode=1','--set','graph.enabled=1','--set','source.enabled=1','--set','source.engine=2','--set','modulation.1.source=1','--set','modulation.1.target=97','--set','modulation.2.source=2','--set','modulation.2.target=85','--set','modulation.3.source=3','--set','modulation.3.target=99');
 'C-fm'   = @('--set','global.mode=1','--set','graph.enabled=1','--set','fm.enabled=1','--set','modulation.1.source=1','--set','modulation.1.target=102','--set','modulation.2.source=2','--set','modulation.2.target=104','--set','modulation.3.source=3','--set','modulation.3.target=107');
 'D-all'  = @('--set','global.mode=1','--set','graph.enabled=1','--set','source.enabled=1','--set','source.engine=1','--set','fm.enabled=1','--set','modulation.1.source=1','--set','modulation.1.target=85','--set','modulation.2.source=2','--set','modulation.2.target=90','--set','modulation.3.source=3','--set','modulation.3.target=97','--set','modulation.4.source=1','--set','modulation.4.target=102','--set','modulation.5.source=2','--set','modulation.5.target=104','--set','modulation.6.source=3','--set','modulation.6.target=107')
}
$depth=@{}
foreach($k in $sc.Keys){
  $n=($sc[$k] | ? { $_ -match '^modulation\.\d+\.target=' }).Count
  $extra=@($sc[$k]); $zero=@($sc[$k]); $i=1; foreach($r in 1..$n){ $extra+=@('--set',"modulation.$r.depth=0.5"); $zero+=@('--set',"modulation.$r.depth=0") }
  $samples=@('default','factory-12','custom-graph-fm')
  & "$V\heapfill-regress-230.ps1" -Exe $tool -Log "$V\$Tag-heapfill-newdest-$k.log" -Label "$Tag new destinations $k (depth 0.5)" -Samples $samples -Extra $extra
  Stamp "heapfill newdest $k done: $(Select-String -Path "$V\$Tag-heapfill-newdest-$k.log" -Pattern 'TOTAL' | % Line)"
  # sanity: modulated vs depth 0 must differ, else the scenario does not exercise the destinations
  $tmp="$env:TEMP\san_$PID"; New-Item -ItemType Directory -Force $tmp | Out-Null
  foreach($s in $samples){
    $on=(& $tool dump $s --block 256 --out "$tmp\on.bin" @extra) -join ' '; $off=(& $tool dump $s --block 256 --out "$tmp\off.bin" @zero) -join ' '
    "$k $s modulated: $on | depth0: $off | differ=$($on -ne $off)" | Out-File "$V\$Tag-heapfill-newdest-sanity.log" -Append -Encoding ascii
  }
  Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
# 6 presets
$old='C:\Users\24168\Documents\Projects\wt-integration-220\build-integ-220\AetherRenderPresets_artefacts\Release\AetherRenderPresets.exe'
$new="$Build\AetherRenderPresets_artefacts\Release\AetherRenderPresets.exe"
Stamp "RenderPresets old built=$((Get-Item $old).LastWriteTime) new built=$((Get-Item $new).LastWriteTime)"
$out="$Build\render-cmp-$Tag"; Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
foreach($mode in @(@('default'),@('fresh'),@('clamp'),@('fresh','clamp'))){
  $name=($mode -join '+'); $res=@{}
  foreach($w in @(@('old',$old),@('new',$new))){ $d="$out\$($w[0])-$name"; New-Item -ItemType Directory -Force $d | Out-Null; $o=& $w[1] $d 480 @mode; $res[$w[0]]=$d; "$name $($w[0]) exit=$LASTEXITCODE : $($o | select -Last 1)" | Out-File "$V\$Tag-preset-compare.log" -Append -Encoding ascii }
  $files=Get-ChildItem $res['old'] -File; $diff=0; $list=@()
  foreach($f in $files){ $a=(Get-FileHash $f.FullName -Algorithm SHA256).Hash; $bp=Join-Path $res['new'] $f.Name; $b=if(Test-Path $bp){(Get-FileHash $bp -Algorithm SHA256).Hash}else{'MISSING'}; if($a -ne $b){$diff++;$list+=$f.Name} }
  "$name : total=$($files.Count) differing=$diff $($list -join ',')" | Out-File "$V\$Tag-preset-compare.log" -Append -Encoding ascii
}
Stamp "preset compare done"; Stamp "ALL DONE"
'DONE' | Out-File "$V\$Tag-validate-done.flag"

