$ErrorActionPreference='Continue'
$s='C:\Users\24168\Documents\Projects\wt-scratch-13c6e73'; $V='C:\Users\24168\Documents\Projects\wt-adapt-230\validation\adapt-230'
$jobs=@()
foreach($form in 'Fx','Synth'){ foreach($g in 'schema4','new','invariant'){
  $exe=(Resolve-Path "$s\build-scratch\AetherModContract${form}_artefacts\Release\AetherModContract${form}.exe").Path
  $log="$V\on-13c6e73-$($form.ToLower())-$g.log"
  $j=Start-Process -FilePath $exe -ArgumentList '--group',$g -RedirectStandardOutput $log -RedirectStandardError "$log.err" -PassThru -WindowStyle Hidden; $null=$j.Handle; $jobs += [pscustomobject]@{J=$j;Name="$form-$g"}
}}
$sw=[Diagnostics.Stopwatch]::StartNew()
foreach($e in $jobs){ $e.J.WaitForExit(); "$($e.Name) exit=$($e.J.ExitCode) elapsed_s=$([math]::Round($sw.Elapsed.TotalSeconds,1))" | Out-File "$V\on-13c6e73-summary.txt" -Append -Encoding ascii }
'DONE' | Out-File "$V\on-13c6e73.done"
