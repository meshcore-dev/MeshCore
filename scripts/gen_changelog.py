#!/usr/bin/env python3
"""Append new branch commits to CHANGELOG.md, newest-first, grouped by month.

Usage:
    gen_changelog.py [path/to/CHANGELOG.md]   (default: repo-root/CHANGELOG.md)

Design notes
------------
The hand-curated history at the top of CHANGELOG.md is never rewritten. This
script is **append-only and idempotent**: it reads the set of commit hashes
already accounted for (from the ``gen_changelog:hashes`` manifest comment at the
bottom of the file, plus any hash cited inline), then inserts only commits not
yet present.

"Branch work" is defined as non-merge commits authored by ``agessaman`` (the
deterministic filter that matches how the changelog was framed). Merges of the
upstream MeshCore ``dev`` branch are surfaced as "Upstream sync" markers; all
other merges and pure rebase artifacts are recorded in the manifest but not
shown.

New entries clean up best when commits follow ``type(scope): subject``
(feat/fix/refactor/...); free-form subjects fall back to a trimmed, as-written
summary. If nothing new is found the file is left byte-for-byte untouched.
"""
import re
import subprocess
import sys
from pathlib import Path

AUTHOR = "agessaman"
DEFAULT = Path(__file__).resolve().parent.parent / "CHANGELOG.md"

MONTHS = ["", "January", "February", "March", "April", "May", "June", "July",
          "August", "September", "October", "November", "December"]

TYPE_LABEL = {"feat": "New", "fix": "Fix", "enhance": "Improvement",
              "refactor": "Internal", "perf": "Performance", "docs": "Docs",
              "chore": "Change", "build": "Build", "ci": "CI", "style": "Internal"}

VERB_LABEL = [
    (r"^(add|added|implement|introduce|enable|restore|create|support)\b", "New"),
    (r"^(fix|fixed|correct|resolve)\b", "Fix"),
    (r"^(enhance|improve|improved|optimize|increase|simplify|streamline)\b", "Improvement"),
    (r"^(refactor)\b", "Internal"),
    (r"^(document|docs)\b", "Docs"),
]

CC = re.compile(r"^(\w+)(?:\(([^)]*)\))?(!)?:\s*(.*)$")
UPSTREAM_MERGE = re.compile(r"[Mm]erge.*(?:upstream|origin)/dev")
# rebase / merge artifacts that should never appear in the visible log
NOISE = re.compile(r"conflict marker|duplicate (?:mqtt )?function|unnecessary comments"
                   r"|from rebase|^revert |whoops|^remove final|^remove remaining", re.I)
HASH_TOKEN = re.compile(r"·\s*`([0-9a-f]{7,40})`")
MANIFEST_HASH = re.compile(r"\b[0-9a-f]{7,40}\b")


def clean(subj: str) -> str:
    s = subj.strip().lstrip("* ").strip()
    s = re.split(r"(?<=[.!?])\s+", s)[0].rstrip(". ")
    if len(s) > 140:
        s = s[:137].rstrip() + "…"
    return s[:1].upper() + s[1:] if s else s


def label_for(typ, summary):
    if typ and typ.lower() in TYPE_LABEL:
        return TYPE_LABEL[typ.lower()]
    for pat, lab in VERB_LABEL:
        if re.match(pat, summary, re.I):
            return lab
    return "Change"


def git_commits():
    out = subprocess.run(
        ["git", "log", f"--author={AUTHOR}", "--pretty=format:%h%x09%ad%x09%p%x09%s",
         "--date=short"],
        capture_output=True, text=True, check=True).stdout.splitlines()
    rows = []
    for ln in out:
        h, date, parents, subj = ln.split("\t", 3)
        rows.append((h, date, len(parents.split()) > 1, subj))
    return rows


def render(h, date, is_merge, subj):
    """Return a markdown bullet, or None if the commit should be hidden."""
    if is_merge:
        if UPSTREAM_MERGE.search(subj):
            ver = re.search(r"v\d+\.\d+\.\d+", subj)
            tail = f" ({ver.group()})" if ver else ""
            return f"- ⬆ **Upstream sync** — Synced with upstream MeshCore dev{tail}  <sub>{date} · `{h}`</sub>"
        return None
    if NOISE.search(subj):
        return None
    m = CC.match(subj)
    if m:
        typ, scope, _, rest = m.groups()
        summary = clean(rest)
    else:
        typ = scope = None
        summary = clean(subj)
    sc = f" · `{scope}`" if scope else ""
    return f"- **{label_for(typ, summary)}**{sc} — {summary}  <sub>{date} · `{h}`</sub>"


def insert_entry(lines, date, bullet):
    """Insert a rendered bullet at the top of its month section (creating it)."""
    y, mo, _ = date.split("-")
    heading = f"### {MONTHS[int(mo)]} {y}"
    # existing section: insert as the first bullet (after heading + blank lines)
    for i, ln in enumerate(lines):
        if ln.rstrip() == heading:
            j = i + 1
            while j < len(lines) and not lines[j].startswith("- "):
                j += 1
            lines.insert(j, bullet)
            return
    # new (newer) month: place its section above the first existing section
    for i, ln in enumerate(lines):
        if ln.startswith("### "):
            lines[i:i] = [heading, "", bullet, ""]
            return
    raise SystemExit("no '### Month Year' section found; is this a CHANGELOG.md?")


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    text = path.read_text()
    lines = text.split("\n")

    m = re.search(r"<!--\s*gen_changelog:hashes.*?-->", text, re.S)
    if not m:
        sys.exit("manifest block '<!-- gen_changelog:hashes ... -->' not found")
    known = set(MANIFEST_HASH.findall(m.group(0)))
    known |= set(HASH_TOKEN.findall(text))

    # oldest-first so that inserting each at the top of its section keeps newest on top
    new = [(h, date, mg, subj) for (h, date, mg, subj) in git_commits()
           if h not in known]
    new.reverse()
    if not new:
        print("CHANGELOG.md already up to date.")
        return

    manifest_idx = next(i for i, ln in enumerate(lines) if ln.rstrip() == "-->"
                        and "gen_changelog:hashes" in "\n".join(lines[max(0, i - 40):i]))
    body = lines[:manifest_idx]
    tail = lines[manifest_idx:]

    added = []
    for h, date, is_merge, subj in new:
        bullet = render(h, date, is_merge, subj)
        if bullet:
            insert_entry(body, date, bullet)
        added.append(h)  # record every processed hash, shown or not

    # append newly processed hashes to the manifest (just before the closing -->)
    tail.insert(len(tail) - 1, " ".join(added))

    out = "\n".join(body + tail)
    if "### " not in out:
        sys.exit("refusing to write: output has no month sections")
    path.write_text(out)
    shown = sum(1 for h, d, mg, s in new if render(h, d, mg, s))
    print(f"added {len(added)} commit(s) to {path.name} "
          f"({shown} shown, {len(added) - shown} recorded silently)")


if __name__ == "__main__":
    main()
