@echo off
"C:\Users\24168\Documents\Projects\vst3development\tmp\tools\pluginval\pluginval.exe" --strictness-level 5 --validate "C:\Users\24168\Documents\Projects\wt-integration-210\build-integ-210-final\AetherMicroSynth_artefacts\Release\VST3\AETHER Micro Synth.vst3" > "C:\Users\24168\Documents\Projects\wt-integration-210\validation\integration-210-final\pluginval-synth.log" 2>&1
exit /b %ERRORLEVEL%
