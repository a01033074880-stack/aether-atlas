# Imports the portable MSVC environment into this PowerShell process and puts ninja on PATH.
$vc='C:\SDKs\AetherBuildTools\VC\Auxiliary\Build\vcvars64.bat'
$envLines = & cmd.exe /d /s /c "`"call `"$vc`" >nul && set`""
foreach ($l in $envLines) { if ($l -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') } }
$env:PATH = 'C:\SDKs\AetherBuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;' + $env:PATH
'MSVC env ready: ' + (Get-Command cl.exe).Source
