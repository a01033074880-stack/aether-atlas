# Golden change 80165fc -> 2.2.0-rc (tests/fixtures/golden/render-48000.json)

Reason: AETHER Micro (effect plugin, JucePlugin_IsSynth=0) now clamps the stored global.mode through clampModeForPluginType (single read point, effectiveMode()). A stored mode=2 (Synth) falls back to FX (0) in AETHER Micro. The saved parameter value is not rewritten.
The fixtures run in the effect form, so the four archived samples that store mode=2 now render as FX:

- preset-v1.0.0:Copper Strings
- preset-v1.0.0:Prism Keys
- state-v2.0.1:custom-graph-fm
- state-v2.0.1:factory-11-Copper-Strings

Evidence
- C1-fixtures-before-golden-change.log / C1-failing-samples.txt: with the OLD golden, aether_fixtures failed on exactly these four samples (bit-exact SHA-256 matches 17 of 21).
- golden-old-80165fc.json: previous file, SHA-256 6B221D42FEFCBFFCF789EE396C90AF0390984A587624761C745A85C2E1C971E1 (identical to tests/fixtures/golden/render-48000.json at 80165fc).
- New file SHA-256 9105C9E93D5C2C2BA3B5DFB20804CFC46ACE6E2DE0F938B207C605E9C0ED6ADB. Generated twice in separate processes with AetherFixtureTests --generate-golden; both outputs byte-identical (generate-twice-proof.txt, generate-golden-run1/2.log).
- Old vs new: 157 lines each, 21 lines differ = line 3 (codeVersion 2.0.1 -> 2.2.0, generator metadata) + 4 samples x 5 lines (sha256, rmsL, rmsR, peakL, peakR). The other 17 sample entries are byte-identical. See golden-diff.txt.
- The 4 samples now render as FX (non-silent: rmsL[0] 0.18 where the Synth-mode render was 0 at the first window because no MIDI note had started).
