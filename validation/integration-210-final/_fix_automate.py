from pathlib import Path
p = Path(r"C:\Users\24168\Documents\Projects\wt-integration-210\tests\PluginTests.cpp")
t = p.read_text(encoding="utf-8")
old = 'require(param->isAutomatable()==(spec.id!="source.seed"&&spec.id!="source.antialias"),"automation flag: "+spec.id);'
new = 'require(param->isAutomatable(),"automation flag (all source params automatable, incl. seed/antialias): "+spec.id);'
if old not in t:
    raise SystemExit("old assert not found")
t = t.replace(old, new, 1)
p.write_text(t, encoding="utf-8", newline="\n")
print("patched OK")
print("verify:", "all source params automatable" in t)
