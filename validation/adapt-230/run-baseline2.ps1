Set-Location C:\Users\24168\Documents\Projects\wt-adapt-230
$V='validation\adapt-230'
$jobs=@()
foreach($form in 'Fx','Synth'){ foreach($g in 'invariant','new'){
  $exe=(Resolve-Path "build-adapt-230\AetherModTargets${form}_artefacts\Release\AetherModTargets${form}.exe").Path
  $log=(Join-Path (Resolve-Path $V).Path "baseline-$($form.ToLower())-$g.log")
  $jobs += Start-Process -FilePath $exe -ArgumentList '--group',$g -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru -WindowStyle Hidden
}}
$sw=[Diagnostics.Stopwatch]::StartNew()
foreach($j in $jobs){ $j.WaitForExit(); "$($j.Id) exit=$($j.ExitCode) elapsed_s=$([math]::Round($sw.Elapsed.TotalSeconds,1))" | Out-File "$V\baseline-summary.txt" -Append -Encoding ascii }
'DONE' | Out-File "$V\baseline-done.flag"
