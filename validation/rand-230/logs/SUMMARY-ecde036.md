# rand-230 re-run on merge of ecde036 (test commit 1607526)
Branch test/rand-230; merge 5062e61 = --no-ff of feat/randomize-engine-230@ecde036 (no conflicts). Release, MSVC, Ninja; this turn, own commands.
Results:
- ctest -L rand-230: 9/9 passed (R02 R05 R06 R08 R11 R12 R13 R16 R18), R13 now 2714710 checks 0 failures incl. 7200 attempt-clamp comparisons
- aether_rand: 2268241 checks, 0 failures; aether_rand_state (new from ecde036): 1951 checks, 0 failures
- aether_rand_coverage (JUCE build, AetherRandCoverage): 187418 checks, 0 failures (495 parameters)
- negative check: R13(e) built against the OLD engine (eb9b8bd Rand.cpp, temp dir): 7200 failures (all high-side attempt 6/7/100/65536/INT_MAX-1/INT_MAX), so the assertion is not vacuous
Not run: full ctest (aether_dsp/fm/modular/source/plugin/plugin_synth_ui/fixtures/split/mod_targets*), pluginval, any DAW.
