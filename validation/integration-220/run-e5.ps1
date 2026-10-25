$ErrorActionPreference='Continue'
$x='build-integ-220\AetherReproTool_artefacts\Release\AetherReproTool.exe'; $V='validation\integration-220'; $h="$V\heapfill-regress-220.ps1"
& $h -Exe $x -Log "$V\E5-heapfill-baseline-7samples.log" -Label 'E5 original method, 7 samples, 2.2.0-rc'
foreach($eng in 1,2){ foreach($nm in $false,$true){
  $ex=@('--set','global.mode=1','--set',"source.engine=$eng",'--set','source.enabled=1'); $tag="resynth-src-eng$eng"
  if($nm){$ex+='--nomidi'; $tag+='-nomidi'}
  & $h -Exe $x -Log "$V\E5-heapfill-$tag.log" -Label "E5 Resynth+Source engine=$eng nomidi=$nm" -Extra $ex
}}
'sanity:' | Out-File "$V\E5-sanity-source-on-vs-off.log" -Encoding ascii
$tmp="$env:TEMP\e5san"; New-Item -ItemType Directory -Force $tmp | Out-Null
foreach($s in 'factory-12','default'){
 foreach($eng in 1,2){
  $off=(& $x dump $s --block 256 --out "$tmp\off.bin" --set global.mode=1 --set source.enabled=0) -join ' '
  $on=(& $x dump $s --block 256 --out "$tmp\on.bin" --set global.mode=1 --set "source.engine=$eng" --set source.enabled=1) -join ' '
  "$s eng=$eng SourceOff: $off | SourceOn: $on | differ=$($off -ne $on)" | Out-File "$V\E5-sanity-source-on-vs-off.log" -Append -Encoding ascii
 }}
'DONE' | Out-File "$V\E5-done.flag" -Encoding ascii
