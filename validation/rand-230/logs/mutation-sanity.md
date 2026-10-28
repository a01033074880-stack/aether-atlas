# Mutation sanity check (scratch copies of Rand.cpp under build-rand-230\mut, NOT committed, product code untouched)
Each mutant changes the single line in Engine::canWrite() (libs/aether-rand/src/Rand.cpp:178) and is built with the new test file.
- M1 lock ignored for Source group  : R02 -> 48342 failures; R06 (after adding the accepted-patch-ratio check) -> FAIL "locking group Source: only 450 of 1125 patches accepted" (the engine self-validates and discards the whole patch, so the diff-based checks alone were vacuous: that is why R06 also counts accepted patches)
- M2 groupClosed() removed          : R11 -> 2 failures, R13 -> 60 failures (FM/Source written while closed / outside scope)
- M3 scope ignored for Motion group : NOT detected (equivalent mutant: motion writers are additionally guarded by an explicit `scopeMask & ScopeMotion` in runFull/runMutate)
- M4 Never writable for Motion group: NOT detected (equivalent mutant: no sampler targets the Never rows lfo.*.sync)
