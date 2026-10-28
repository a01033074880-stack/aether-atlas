#!/usr/bin/env python3
"""Generates libs/aether-rand/src/RandTable.inc from the RandSpec documents.

Inputs (all committed):
  docs/randomization-spec-params.tsv   495 rows: classification, windows, distribution, activeWhen (spec 2.5)
  docs/randomization-spec-ranges.tsv   parameterSpecs() min/max/step/default/choices (written by AetherRandSpecDump)
  docs/randomization-spec.md           3.4 node macro table, 5.3 routing algorithms, 5.4 FM algorithms
Output is deterministic (no timestamps).  Usage:
  python tools/gen_rand_table.py [--root <repo>]
"""
import argparse, csv, re, sys, io, os

FNV_OFF, FNV_PRIME, M64 = 0xcbf29ce484222325, 0x100000001b3, (1 << 64) - 1

def fnv1a64(s):
    h = FNV_OFF
    for b in s.encode('utf-8'):
        h = ((h ^ b) * FNV_PRIME) & M64
    return h

ACTIVE = {
    '-': 'None',
    'graph.enabled=0 且 FM/Source 均未启用时才发声': 'ClassicOnly',
    'graph.enabled=1': 'GraphOn',
    'graph.enabled=1 && 算法含反馈弧': 'GraphFeedbackAlgo',
    'graph.enabled=1 && node.N.on=1 && node.N.type!=Thru': 'NodeActive',
    'graph.enabled=1 && routing.algorithm=32': 'GraphAlgo32',
    'graph.enabled=1 && LFO N 被至少一个有效调制行引用': 'LfoReferenced',
    'graph.enabled=1 && 该行 source≠None && target≠None': 'RowLive',
    'mode∈{Resynth,Synth}': 'ModeRS',
    'fm.enabled=1 && mode∈{Resynth,Synth}': 'FmOnModeRS',
    'fm.enabled=1': 'FmOn',
    'fm.enabled=1 && mode≠FX': 'FmOnNotFx',
    'fm.enabled=1 && fm.custom=0': 'FmOnCustom0',
    'fm.enabled=1 && fm.custom=1': 'FmOnCustom1',
    'source.enabled=1 && source.engine>0 && mode≠FX': 'SrcOn',
    'source.enabled=1 && source.engine=1 && mode≠FX': 'SrcEngine1',
    'source.enabled=1 && source.engine=2 && mode≠FX': 'SrcEngine2',
}
DIST = {'-': 'None', 'Choice': 'Choice', 'Uniform': 'Uniform', 'Derived': 'Derived', 'Bool': 'Bool',
        'Centered': 'Centered', 'PerType': 'PerType', 'Log': 'Log', 'Sparse': 'Sparse', 'Kumaraswamy': 'Kumaraswamy'}
GROUP = {'Chain': 'Chain', 'ModuleParams': 'ModuleParams', 'FM': 'FM', 'Source': 'Source', 'Motion': 'Motion',
         'MixGlobal': 'MixGlobal', 'System': 'System'}
ROLE = {'Never': 'Never', 'Structural': 'Structural', 'Value': 'Value', 'Choice': 'Choice', 'ModSource': 'ModSource',
        'ModDest': 'ModDest', 'ModDepth': 'ModDepth'}
TYPE = {'float': 'Float', 'choice': 'Choice', 'bool': 'Bool'}
# Spec 3.3: fm.feedback is K(1,2); the spec gives no shape for the other two Kumaraswamy rows -> same K(1,2) (reported).
PARAM_K = {'fm.feedback': (1.0, 2.0), 'source.pg.phaserandom': (1.0, 2.0), 'source.chaos.momentum': (1.0, 2.0)}
# Spec 3.3: fm.morph is Sparse with P(non-zero) = Safe 0 / Normal 0.2 / Wild 0.4.
PARAM_SPARSE = {'fm.morph': (0.0, 0.2, 0.4)}

def num(s):
    return float(s)

def read_tsv(path):
    with open(path, encoding='utf-8', newline=None) as f:
        return list(csv.DictReader(f, delimiter='\t'))

def fmt(x):
    r = repr(float(x))
    if r.endswith('.0'):
        r = r[:-2]
    if r in ('-0',):
        r = '0'
    return r + ('' if ('.' in r or 'e' in r or 'n' in r) else '.0')

WIN = re.compile(r'\[\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*\]')

def parse_macro_rows(spec):
    rows = {}
    overrides = []
    for line in spec.split('\n'):
        m = re.match(r'^\|\s*(\d+)\s*\|\s*([^|]+?)\s*\|\s*node\.N\.([abcd])\s*\|(.*)\|\s*$', line)
        if not m:
            continue
        t, name, knob = int(m.group(1)), m.group(2), 'abcd'.index(m.group(3))
        cols = [c.strip() for c in m.group(4).split('|')]
        # cols: name, unit range, default, dist, safe, normal, wild, safety class, note
        if len(cols) != 9:
            raise SystemExit('bad macro row: ' + line)
        mname, unit, default, dist, safe, normal, wild, scls, note = cols
        if t == 0:
            rows[(t, knob)] = dict(dist='None', a=1.0, b=1.0, center=0.5, win=[(0, 0)] * 3, p=[0, 0, 0], scls='S0', default=float(default), name=mname, module=name)
            continue
        win, p = [], [0.0, 0.0, 0.0]
        for i, w in enumerate((safe, normal, wild)):
            mm = WIN.search(w)
            if mm:
                win.append((float(mm.group(1)), float(mm.group(2))))
            elif w.startswith('恒'):
                win.append((0.0, 0.0))
            else:
                mp = re.match(r'P=([\d.]+)→U\[0\.6,1\]', w)
                if not mp:
                    raise SystemExit('bad window %r in %s' % (w, line))
                win.append((0.0, 1.0)); p[i] = float(mp.group(1))
        a = b = 1.0
        if dist.startswith('Uniform'):
            d = 'Uniform'
        elif dist.startswith('Kumaraswamy'):
            d = 'Kumaraswamy'
            mk = re.search(r'K\(([\d.]+),([\d.]+)\)', dist)
            a, b = float(mk.group(1)), float(mk.group(2))
        elif dist.startswith('Centered'):
            d = 'Centered'
        elif dist.startswith('Sparse'):
            d = 'Sparse'
        else:
            raise SystemExit('bad dist ' + dist)
        mo = re.search(r'Safe/Normal 窗口为 \[([\d.]+),([\d.]+)\]/\[([\d.]+),([\d.]+)\]', note)
        if mo:
            win[0] = (float(mo.group(1)), float(mo.group(2)))
            win[1] = (float(mo.group(3)), float(mo.group(4)))
            overrides.append('type %d knob %s (%s %s): Safe/Normal windows taken from the note text' % (t, 'abcd'[knob], name, mname))
        rows[(t, knob)] = dict(dist=d, a=a, b=b, center=0.5, win=win, p=p, scls=scls, default=float(default), name=mname, module=name)
    if len(rows) != 84:
        raise SystemExit('expected 21x4 macro rows, got %d' % len(rows))
    return rows, overrides

def parse_fm_algos(spec):
    algos = {}
    for line in spec.split('\n'):
        m = re.match(r'^\|\s*(\d+)\s*\|\s*([^|]+?)\s*\|\s*\[([\d, ]*)\]\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(acyclic|CYCLE)\s*\|\s*([\d.]+)\s*\|\s*\[([\d, ]*)\]\s*\|', line)
        if not m:
            continue
        n = int(m.group(1))
        carriers = [int(x) for x in m.group(3).replace(' ', '').split(',') if x]
        unreach = [int(x) for x in m.group(8).replace(' ', '').split(',') if x]
        algos[n] = dict(name=m.group(2), carrier=sum(1 << (c - 1) for c in carriers), edges=int(m.group(4)),
                        selfloops=int(m.group(5)), cycle=(m.group(6) == 'CYCLE'), maxIn=float(m.group(7)),
                        unreach=sum(1 << (c - 1) for c in unreach))
    if sorted(algos) != list(range(32)):
        raise SystemExit('FM algorithm table incomplete: %s' % sorted(algos))
    return algos

def parse_route_algos(spec):
    algos = {}
    for line in spec.split('\n'):
        m = re.match(r'^\|\s*(\d+)\s*\|\s*([A-Za-z][^|]*?)\s*\|\s*([^|]*)\|\s*([^|]*)\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*([\d.]+)\s*\|\s*(inf|[\d.]+)\s*\|\s*([\d.]+)\s*\|', line)
        if not m:
            continue
        n = int(m.group(1))
        algos[n] = dict(name=m.group(2), fwd=int(m.group(5)), fb=int(m.group(6)), minScale=float(m.group(9)))
    if sorted(algos) != list(range(32)):
        raise SystemExit('routing algorithm table incomplete: %s' % sorted(algos))
    if sum(1 for a in algos.values() if a['fb'] == 0) != 20:
        raise SystemExit('expected 20 feedback-free routing algorithms')
    return algos

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
    ap.add_argument('--params'); ap.add_argument('--ranges'); ap.add_argument('--spec'); ap.add_argument('--out')
    a = ap.parse_args()
    root = a.root
    pp = a.params or os.path.join(root, 'docs/randomization-spec-params.tsv')
    rp = a.ranges or os.path.join(root, 'docs/randomization-spec-ranges.tsv')
    sp = a.spec or os.path.join(root, 'docs/randomization-spec.md')
    op = a.out or os.path.join(root, 'libs/aether-rand/src/RandTable.inc')
    params = read_tsv(pp); ranges = read_tsv(rp)
    with open(sp, encoding='utf-8', newline=None) as f:
        spec = f.read()
    if len(params) != 495 or len(ranges) != 495:
        raise SystemExit('expected 495 rows, got %d / %d' % (len(params), len(ranges)))
    macros, overrides = parse_macro_rows(spec)
    fm = parse_fm_algos(spec)
    rt = parse_route_algos(spec)
    out = io.StringIO()
    w = out.write
    w('// GENERATED by tools/gen_rand_table.py - do not edit.\n')
    w('// Inputs: docs/randomization-spec-params.tsv, docs/randomization-spec-ranges.tsv, docs/randomization-spec.md (3.4, 5.3, 5.4).\n')
    w('// kRandAlgoVersion must be bumped whenever a regenerated table changes sampling results.\n\n')
    cnt = {'Random': 0, 'Structural': 0, 'Never': 0}
    w('const std::array<ParamInfo, kParamCount> kParamTable = {{\n')
    for i, (p, r) in enumerate(zip(params, ranges)):
        pid = p['id']
        if r['id'] != pid or int(r['index']) != i:
            raise SystemExit('row %d id mismatch: %s vs %s' % (i, pid, r['id']))
        if TYPE[p['type']].lower() != r['type']:
            raise SystemExit('type mismatch for ' + pid)
        cnt[p['classification']] += 1
        wins = []
        if p['classification'] == 'Never':
            wins = [(0, 0)] * 3
        else:
            def f(x):
                return num(x) if x not in ('-', 'set', '§3.4') else 0.0
            wins = [(f(p['safe_lo']), f(p['safe_hi'])), (f(p['range_lo']), f(p['range_hi'])), (f(p['wild_lo']), f(p['wild_hi']))]
        ka, kb = PARAM_K.get(pid, (1.0, 1.0))
        if p['distribution'] == 'Kumaraswamy' and pid not in PARAM_K:
            raise SystemExit('no K params for ' + pid)
        sp3 = PARAM_SPARSE.get(pid, (0.0, 0.0, 0.0))
        act = ACTIVE[p['activeWhen']]
        w('  {"%s", 0x%016xULL, ParamType::%s, Classification::%s, Group::%s, Role::%s, ActiveWhen::%s, Dist::%s, %d, %s, %d, %s, %s, %s, %s, %s, %s, {%s, %s, %s}, {{%s, %s}, {%s, %s}, {%s, %s}}},\n' % (
            pid, fnv1a64(pid), TYPE[p['type']], p['classification'], GROUP[p['group']], ROLE[p['role']], act, DIST[p['distribution']],
            int(p['safety'][1:]), 'true' if p['edition'] == 'RS' else 'false', int(r['num_choices']),
            fmt(num(r['min'])), fmt(num(r['max'])), fmt(num(r['step'])), fmt(num(r['default'])), fmt(ka), fmt(kb),
            fmt(sp3[0]), fmt(sp3[1]), fmt(sp3[2]),
            fmt(wins[0][0]), fmt(wins[0][1]), fmt(wins[1][0]), fmt(wins[1][1]), fmt(wins[2][0]), fmt(wins[2][1])))
    w('}};\n\n')
    if cnt != {'Random': 175, 'Structural': 273, 'Never': 47}:
        raise SystemExit('classification counts differ from spec 2.3: %s' % cnt)
    w('// Node macro windows by (module type 0..20, knob a..d), spec 3.4; the windows are knob-space (0..1) values.\n')
    w('const std::array<MacroInfo, kModuleTypeCount * 4> kMacroTable = {{\n')
    for t in range(21):
        for k in range(4):
            m = macros[(t, k)]
            w('  {Dist::%s, %s, %s, %s, {%s, %s, %s}, {{%s, %s}, {%s, %s}, {%s, %s}}, %d, %s},  // %d %s / %s\n' % (
                m['dist'], fmt(m['a']), fmt(m['b']), fmt(m['center']), fmt(m['p'][0]), fmt(m['p'][1]), fmt(m['p'][2]),
                fmt(m['win'][0][0]), fmt(m['win'][0][1]), fmt(m['win'][1][0]), fmt(m['win'][1][1]), fmt(m['win'][2][0]), fmt(m['win'][2][1]),
                int(m['scls'][1:]), fmt(m['default']), t, m['module'], m['name']))
    w('}};\n\n')
    w('// FM preset algorithms, spec 5.4 (carrier / unreachable are bit masks, bit k = operator k+1).\n')
    w('const std::array<FmAlgoInfo, kFmAlgorithmCount> kFmAlgoTable = {{\n')
    for n in range(32):
        x = fm[n]
        w('  {0x%02x, 0x%02x, %d, %d, %s, %s},  // %d %s\n' % (x['carrier'], x['unreach'], x['edges'], x['selfloops'], 'true' if x['cycle'] else 'false', fmt(x['maxIn']), n, x['name']))
    w('}};\n\n')
    w('// Routing preset algorithms 0..31, spec 5.3 (feedbackArcs = connections with s >= d).\n')
    w('const std::array<RouteAlgoInfo, kRouteAlgorithmCount> kRouteAlgoTable = {{\n')
    for n in range(32):
        x = rt[n]
        w('  {%d, %d, %s},  // %d %s\n' % (x['fwd'], x['fb'], fmt(x['minScale']), n, x['name']))
    w('}};\n')
    data = out.getvalue()
    if overrides:
        data += '// Spec-internal inconsistencies resolved by the generator:\n' + ''.join('//   - %s\n' % o for o in overrides)
    with open(op, 'w', encoding='utf-8', newline='\n') as f:
        f.write(data)
    print('wrote', op, len(data), 'bytes;', 'overrides:', overrides)

main()
