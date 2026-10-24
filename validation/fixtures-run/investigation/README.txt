Intermediate evidence for the graph/FM-path non-determinism (see tests/fixtures/README.md section 4).
- direct-runs-before-fix-6x.log: 6 runs of the strict version of the test exe (18:54-18:58): 3 failed.
- direct-runs-srand-experiment-6x.log: same with std::srand(1) before every render (experimental build, since reverted): 5 of 6 failed, so CRT rand() is not the cause.
- poison-stack-scribble.log: --poison, 84 renders with 5 stack fill patterns, 0 differed.
- stress-all-fresh-process.log: --stress 3 in a fresh process, 189 cross-block comparisons, 0 differed.
These logs come from earlier builds of tests/FixtureTests.cpp than the final one; the final results are in the parent folder.
