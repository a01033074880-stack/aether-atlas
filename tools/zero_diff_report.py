"""Multi-run reproducibility / zero-diff report for AetherRenderPresets output.

Layout: <root>/<tag>-b<block>-r<run>/NN.f32 with tag in {base,new}; plus optional
<tag>-b<block>-fresh<run>. Criterion: relative diff below -120 dB (or bit-identical).
diff_dB = 10*log10(sum((a-b)^2)/sum(a^2)) with a = reference run.
"""
import sys, os, re, glob, itertools
import numpy as np
root = sys.argv[1]; CRIT = -120.0
runs = {}
for d in sorted(os.listdir(root)):
    m = re.fullmatch(r'(base|new)-b(\d+)-(r|fresh)(\d+)', d)
    if m: runs.setdefault((m[1], int(m[2]), m[3]), []).append(os.path.join(root, d))
cache = {}
def load(d, n):
    k = (d, n)
    if k not in cache:
        cache[k] = np.fromfile(os.path.join(d, n), dtype=np.float32)
    return cache[k]
def names(d): return sorted(f for f in os.listdir(d) if f.endswith('.f32'))
def pdiff(a, b):
    """returns (identical, dB, max_abs)"""
    if a.shape != b.shape: return False, 999.0, float('inf')
    if a.tobytes() == b.tobytes(): return True, -999.0, 0.0
    a64 = a.astype(np.float64); d = a64 - b.astype(np.float64)
    sig = float((a64 * a64).sum()); e = float((d * d).sum())
    return False, 10 * np.log10(max(e, 1e-300) / max(sig, 1e-30)), float(np.abs(d).max())
def fmt(db): return '-inf (bit-identical)' if db <= -998 else f'{db:.1f}'
def group_pairs(A, B, label):
    """compare every dir in A against every dir in B; per preset worst dB over all pairs"""
    nm = names(A[0]); res = {}
    for n in nm:
        worst, peak, ident = -999.0, 0.0, True
        for a in A:
            for b in B:
                if a == b: continue
                i, db, pk = pdiff(load(a, n), load(b, n))
                ident &= i; worst = max(worst, db); peak = max(peak, pk)
        res[n] = (ident, worst, peak)
    return res
def summarize(res, label, detail=True):
    ident = [n for n, v in res.items() if v[0]]; bad = [n for n, v in res.items() if not v[0]]
    worst = max((v[1] for v in res.values()), default=-999.0)
    print(f'  {label}: presets={len(res)} bit_identical={len(ident)} not_identical={len(bad)} max_diff_dB={fmt(worst)} '
          f'-> {"PASS (< -120 dB)" if worst < CRIT else "FAIL (>= -120 dB)"}')
    if detail and bad:
        print('    not bit-identical presets: ' + ', '.join(f'{n[:2]}({v[1]:.1f} dB, peak {v[2]:.2e})' for n, v in sorted(res.items()) if not v[0]))
    return worst, bad
print(f'criterion: relative diff < {CRIT:.0f} dB per preset (or bit-identical)')
blocks = sorted({k[1] for k in runs if k[2] == 'r'})
verdict = {}
for blk in blocks:
    print(f'\n=== block {blk} ===')
    for tag in ('base', 'new'):
        R = runs.get((tag, blk, 'r'), [])
        if len(R) >= 2:
            print(f' {tag}: {len(R)} runs')
            w, bad = summarize(group_pairs(R, R, tag), f'{tag} self (all run pairs)')
            verdict[(tag, blk)] = (w, bad)
    B, N = runs.get(('base', blk, 'r'), []), runs.get(('new', blk, 'r'), [])
    if B and N:
        w, bad = summarize(group_pairs(B, N, 'x'), f'base-vs-new (all {len(B)}x{len(N)} run pairs)')
        verdict[('cross', blk)] = (w, bad)
        # reference-only: first run vs first run
        summarize(group_pairs(B[:1], N[:1], 'x'), 'base r1 vs new r1')
if len(blocks) >= 2:
    print('\n=== block-size dependence (run 1 of each block size) ===')
    for tag in ('base', 'new'):
        a = runs.get((tag, blocks[0], 'r')); b = runs.get((tag, blocks[1], 'r'))
        if a and b: summarize(group_pairs(a[:1], b[:1], tag), f'{tag} block {blocks[0]} vs block {blocks[1]}')
for key in sorted(k for k in runs if k[2] == 'fresh'):
    F = runs[key]; tag, blk = key[0], key[1]
    print(f'\n=== fresh instance per preset ({tag}, block {blk}, {len(F)} runs) ===')
    if len(F) >= 2: summarize(group_pairs(F, F, tag), f'{tag} fresh self')
    R = runs.get((tag, blk, 'r'))
    if R: summarize(group_pairs(F[:1], R[:1], tag), f'{tag} fresh vs shared-instance r1')
print('\n=== summary ===')
for blk in blocks:
    bs = verdict.get(('base', blk)); ns = verdict.get(('new', blk)); cx = verdict.get(('cross', blk))
    print(f'block {blk}: baseline self max diff (dB) = {fmt(bs[0]) if bs else "n/a"}; new self max diff (dB) = {fmt(ns[0]) if ns else "n/a"}; '
          f'baseline-vs-new max diff (dB) = {fmt(cx[0]) if cx else "n/a"}')
    if bs and bs[1]: print(f'  baseline NON-REPRODUCIBLE presets: {[n[:2] for n in bs[1]]}')
    if ns and ns[1]: print(f'  new-branch NON-REPRODUCIBLE presets: {[n[:2] for n in ns[1]]}')
    if cx and cx[1]: print(f'  base-vs-new differing presets: {[n[:2] for n in cx[1]]}')