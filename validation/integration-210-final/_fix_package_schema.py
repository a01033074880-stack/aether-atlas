from pathlib import Path
p = Path(r"C:\Users\24168\Documents\Projects\wt-integration-210\scripts\package.ps1")
t = p.read_text(encoding="utf-8")
old = "        schema = 2"
new = "        schema = 3"
if old not in t:
    raise SystemExit("schema = 2 not found in package.ps1 metadata")
t = t.replace(old, new, 1)
p.write_text(t, encoding="utf-8", newline="\n")
print("package.ps1 schema metadata -> 3")
