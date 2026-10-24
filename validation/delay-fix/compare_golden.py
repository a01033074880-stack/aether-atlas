# Compare old golden (test/state-fixtures f688c6d) with the new golden (after the Delay::read wrap fix): per sample, window RMS/peak.
import json, re, sys, glob, os, hashlib
old = json.load(open(sys.argv[1], encoding='utf-8'))
new = json.load(open(sys.argv[2], encoding='utf-8'))
fx = sys.argv[3]
CHUNK = old['chunk']
print(f"old golden: codeVersion={old['codeVersion']} windows of {CHUNK} frames; samples old={len(old['samples'])} new={len(new['samples'])}")
assert old['chunk'] == new['chunk'] and old['frames'] == new['frames'] and set(old['samples']) == set(new['samples'])
TYPES = ['Thru','Spectral','Modal','Colour','Space','PrismPitch','GrainCloud','Wavefolder','WdfDiode','Resonator','String','Ladder','FrequencyShift','RingMod','Comb','Diffuser','LoFi','Tape','SpectralFreeze','Chaos','Chorus']
def modules(sample):
    if not sample.startswith('state-v2.0.1:'): return None
    name = sample.split(':',1)[1]
    p = os.path.join(fx, 'state-v2.0.1', name + '.xml')
    if not os.path.exists(p): return None
    txt = open(p, encoding='utf-8', errors='replace').read()
    # live parameters are the top-level <PARAM .../> lines (the alternateSnapshot attribute is escaped, so not matched)
    en = re.search(r'<PARAM id="graph.enabled" value="([0-9.]+)"', txt)
    mode = re.search(r'<PARAM id="global.mode" value="([0-9.]+)"', txt)
    t = sorted(set(TYPES[int(float(v))] for v in re.findall(r'<PARAM id="node\.\d+\.type" value="([0-9.]+)"', txt)))
    return ('graph=' + (en.group(1) if en else '?')), t
rows = []
changed = []
for name in old['samples']:
    o, n = old['samples'][name], new['samples'][name]
    same_sha = o['sha256'] == n['sha256']
    info = {'name': name, 'sha_same': same_sha}
    if not same_sha:
        changed.append(name)
        for lane in ('rmsL','rmsR','peakL','peakR'):
            a, b = o[lane], n[lane]
            diffs = [i for i in range(len(a)) if a[i] != b[i]]
            info[lane] = diffs
            info[lane + '_maxabs'] = max((abs(a[i]-b[i]) for i in diffs), default=0.0)
            info[lane + '_maxrel'] = max((abs(a[i]-b[i]) / max(abs(a[i]), abs(b[i]), 1e-12) for i in diffs), default=0.0)
            info[lane + '_ref'] = max(max(a), 1e-12)
    rows.append(info)
print()
print(f"samples with identical SHA-256 of the float bits: {sum(r['sha_same'] for r in rows)} of {len(rows)}")
print("unchanged:", ', '.join(r['name'] for r in rows if r['sha_same']))
print("changed  :", ', '.join(changed))
print()
for r in rows:
    if r['sha_same']: continue
    m = modules(r['name'])
    print(f"== {r['name']}")
    if m: print(f"   state: {m[0]}, node module types present: {', '.join(m[1])}  -> Space present: {'Space' in m[1]}")
    for lane in ('rmsL','rmsR','peakL','peakR'):
        d = r[lane]
        if d:
            lo = d[0]
            print(f"   {lane}: first changed window {lo} (frames {lo*CHUNK}..{(lo+1)*CHUNK-1}); {len(d)} of {len(old['samples'][r['name']][lane])} windows changed; max |diff| {r[lane+'_maxabs']:.3e} (max rel {r[lane+'_maxrel']:.3e}, lane max level {r[lane+'_ref']:.4f})")
        else:
            print(f"   {lane}: no change")
print()
print("Legacy-path check (graph/FM off): v1 presets + state-v2.0.1 factory 0/5/11 must be unchanged:")
legacy = [r for r in rows if r['name'].startswith('preset-v1.0.0:') or any(r['name'].startswith('state-v2.0.1:' + p) for p in ('factory-00','factory-05','factory-11'))]
print(f"  legacy samples: {len(legacy)}; unchanged: {sum(r['sha_same'] for r in legacy)}")
print("Space presence per changed/unchanged state sample:")
for r in rows:
    m = modules(r['name'])
    if m: print(f"  {r['name']:62s} changed={not r['sha_same']!s:5s} {m[0]} Space={'Space' in m[1]} Comb={'Comb' in m[1]} Diffuser={'Diffuser' in m[1]} Tape={'Tape' in m[1]}")
