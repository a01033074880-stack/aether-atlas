# -*- coding: utf-8 -*-
from pathlib import Path
root = Path(r"C:\Users\24168\Documents\Projects\wt-integration-210")

def read(rel):
    return (root / rel).read_text(encoding="utf-8")

def write(rel, text):
    (root / rel).write_text(text, encoding="utf-8", newline="\n")
    print("wrote", rel)

# CMakeLists.txt
t = read("CMakeLists.txt")
t2 = t.replace("project(AetherMicro VERSION 2.0.1 LANGUAGES C CXX)",
               "project(AetherMicro VERSION 2.1.0 LANGUAGES C CXX)", 1)
assert t2 != t
write("CMakeLists.txt", t2)

# package.ps1
t = read("scripts/package.ps1")
t2 = t.replace("$aetherVersion = '2.0.1'", "$aetherVersion = '2.1.0'", 1)
assert t2 != t
write("scripts/package.ps1", t2)

# workflow
t = read(".github/workflows/build.yml")
t2 = t.replace("AetherAtlas-2.0.1-Windows-x64", "AetherAtlas-2.1.0-Windows-x64", 1)
assert t2 != t
write(".github/workflows/build.yml", t2)

# Title-style docs: replace first-line / title 2.0.1 -> 2.1.0 carefully
title_files = [
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "docs/ATLAS_ACCEPTANCE.md",
    "docs/REQUIREMENTS.md",
    "docs/USER_GUIDE.zh-CN.md",
    "docs/VALIDATION.md",
]
for rel in title_files:
    t = read(rel)
    # Only bump the version token in the first ~5 lines (title area), plus any
    # "Atlas 2.0.1" / "AetherAtlas-2.0.1" current-release references that check-version requires.
    lines = t.splitlines(keepends=True)
    changed = False
    for i, line in enumerate(lines[:5]):
        if "2.0.1" in line:
            lines[i] = line.replace("2.0.1", "2.1.0")
            changed = True
    body = "".join(lines)
    # package zip name in README install section
    if "AetherAtlas-2.0.1-Windows-x64" in body:
        body = body.replace("AetherAtlas-2.0.1-Windows-x64", "AetherAtlas-2.1.0-Windows-x64")
        changed = True
    if not changed:
        raise SystemExit(f"no title bump in {rel}")
    write(rel, body)

# v2-contract.md: "Version 2.0.1"
t = read("docs/v2-contract.md")
if "Version 2.0.1" not in t:
    raise SystemExit("v2-contract Version 2.0.1 missing")
write("docs/v2-contract.md", t.replace("Version 2.0.1", "Version 2.1.0", 1))

# CHANGELOG: prepend 2.1.0 entry
cl = read("CHANGELOG.md")
if cl.lstrip().startswith("# Changelog"):
    pass
else:
    raise SystemExit("CHANGELOG unexpected start")
entry = (
    "## 2.1.0 — 2026-10-24\n"
    "\n"
    "- Merged Delay::read wrap fix (out-of-bounds read at buffer wrap).\n"
    "- Merged source layer (schema 3): Phase Geometry / Chaos engines, Input-gain follow, automatable seed/antialias.\n"
    "- Merged Source panel UI and state/preset fixtures; golden renders updated after the delay fix.\n"
    "- Schema 1..3 upgrade regression: archived v1 presets and v2.0.1 states load, reset source defaults, and re-save as schema 3.\n"
    "\n"
)
# Insert after the first heading line
parts = cl.splitlines(keepends=True)
out = []
inserted = False
for i, line in enumerate(parts):
    out.append(line)
    if not inserted and line.startswith("# Changelog"):
        # skip following blank lines then insert
        j = i + 1
        while j < len(parts) and parts[j].strip() == "":
            out.append(parts[j]); j += 1
        out.append(entry)
        out.extend(parts[j:])
        inserted = True
        break
if not inserted:
    raise SystemExit("failed to insert CHANGELOG entry")
if "## 2.1.0" not in "".join(out):
    raise SystemExit("2.1.0 heading missing after insert")
write("CHANGELOG.md", "".join(out))

# check-version.ps1: schema (1..2) -> (1..3)
t = read("scripts/check-version.ps1")
old = 'compatibility is decided by the integer "schema" (1..2).'
new = 'compatibility is decided by the integer "schema" (1..3).'
if old not in t:
    raise SystemExit("check-version schema (1..2) text missing")
# There may be two allowlist reasons mentioning schema — replace all
count = t.count(old)
t2 = t.replace(old, new)
# Also catch any other (1..2) schema wording
if 'schema" (1..2)' in t2 or "schema (1..2)" in t2:
    t2 = t2.replace("schema (1..2)", "schema (1..3)")
write("scripts/check-version.ps1", t2)
print("check-version schema replacements:", count)

print("DONE bump")
