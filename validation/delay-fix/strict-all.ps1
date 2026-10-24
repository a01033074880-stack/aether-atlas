Set-Location C:\Users\24168\Documents\Projects\wt-fixfix\validation\delay-fix
$post='..\..\build-fixfix\AetherFixtureTests_artefacts\Release\AetherFixtureTests.exe'
$pre='..\..\..\wt-fixfix-pre\build-fixfix\AetherFixtureTests_artefacts\Release\AetherFixtureTests.exe'
.\strict-runs.ps1 -Exe $post -Log strict-postfix-normal.log -N 20 -Label postfix-normal
.\strict-runs.ps1 -Exe $post -Log strict-postfix-highload.log -N 10 -Spinners 24 -Label postfix-highload-24spinners
.\strict-runs.ps1 -Exe $pre -Log strict-prefix-normal.log -N 10 -Label prefix-f688c6d-normal
.\strict-runs.ps1 -Exe $pre -Log strict-prefix-highload.log -N 10 -Spinners 24 -Label prefix-f688c6d-highload-24spinners
