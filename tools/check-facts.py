#!/usr/bin/env python3
"""Cross-file fact agreement check.

WHY THIS EXISTS: this repo's most-logged defect is a number being corrected
in one file while a stale copy survives in another. It has happened at least
three times in the week to 2026-10-19. The latest: `docs/phase-0-status.md`
said the J4 divider disturbs its node by "126 mV (2.7 %)" while CLAUDE.md
said "134 mV (2.9 %)" -- and CLAUDE.md's hard invariant 1 cites that 2.9 %
as the only one of its three disturbance figures backed by a meter. The
wrong copy sat under the project's top-line safety clause.

Pointers have been tried twice and failed: status.md once carried a line
saying "CLAUDE.md carries the corner table" AND its own drifted copy of that
table, directly beneath it. A test has not been tried.

HOW A TAG WORKS. Put a marker beside each site that states the fact:

    the node is disturbed by **134 mV (2.9 %)**
    <!-- fact:j4-node-disturbance 134 mV|2.9 % -->

In C/C++/Go, `// fact:<id> <value>` instead of the HTML comment; `--` and
`#` leaders work too, for SQL and python.

Needles are separated by `|`. When a needle must itself contain a `|` --
which it must to assert a markdown table ROW rather than three words that
happen to be somewhere nearby -- separate them with `;;` instead:

    <!-- fact:example-table-row ground | yellow;;black | 0.3 V parked -->

Note the id in that example is deliberately fake. An earlier draft used the
live `j4-conductor-map` id with a cut-down value, which only avoided failing
CI because SKIP_PATHS excludes this file by its hardcoded path -- a booby
trap for whoever moves or copies the script.

Using `;;` anywhere in the value switches the whole value to `;;`.

Two things are then checked, and the second is the one that earns its keep:

  1. AGREEMENT -- every site tagged with the same id must carry the same
     value. This is the cross-file check.
  2. ANCHORING -- every `|`-separated needle in the value must actually
     appear in the prose within CONTEXT_LINES of its own marker. Without
     this, all a tag proves is that the tags agree with each other; the
     prose could say anything. With it, editing the prose and forgetting
     the tag fails the build, which is the realistic way drift starts.

Comparison is normalised, not literal: case, markdown emphasis, Ω vs K,
µ vs u, and the space in "16.4 kΩ" / "16.4 K" / "16.4K" are all the same
string here. Two sites may render a number differently; they may not
disagree about it.

WHAT IT STILL DOES NOT CATCH, stated plainly so nobody trusts it further
than it goes: an UNTAGGED site. If a seventh place states the K-factor and
carries no marker, this script is blind to it. The registry below is the
partial answer -- it pins a minimum number of sites per fact, so deleting
a tagged site fails rather than silently shrinking coverage -- but adding
an untagged one still passes. Tag new sites as you write them.
"""

import os
import re
import subprocess
import sys
from collections import defaultdict

# Lines either side of a marker that count as "its prose".
CONTEXT_LINES = 10

# Facts that must stay tagged, and the minimum number of distinct FILES each
# must appear in. A fact below its minimum fails the build: it means a site
# was deleted or lost its marker, which is exactly the state this check
# exists to prevent. Raise a number when you tag a new file; never lower one
# to make the build pass without saying why in the commit message.
#
# IT COUNTS FILES, NOT MARKERS, and that is a deliberate trade with a cost.
# The point of a minimum is cross-FILE coverage -- two markers in one file
# satisfied the old marker count while proving nothing about any other file.
# The cost: deleting a SECOND marker inside an already-counted file no longer
# fails here. Anchoring still covers the markers that remain, so the failure
# mode is lost coverage rather than a wrong number going green.
#
# `claude_md` means at least one of those files must be CLAUDE.md -- these are the
# facts whose live conclusion is supposed to stay in the auto-loaded file
# even though the arithmetic that justifies it has moved to docs/.
REQUIRED = {
    # Invariant 1 cites this as the only metered disturbance figure of the
    # three it names. It is the number that was wrong in status.md.
    "j4-node-disturbance": {"min_files": 3, "claude_md": True},
    # The three fitted dividers. Values, not arithmetic -- the corner tables
    # live in docs/taps-and-dividers.md.
    "divider-j2":          {"min_files": 3, "claude_md": True},
    "divider-j4":          {"min_files": 3, "claude_md": True},
    # J5's shunt is the highest-risk number in the repo: it was stated in
    # full on five surfaces, and 15 K (as built) is superseded by 16.4 K
    # (specified). A stale 15 K reads as a live instruction.
    "divider-j5-shunt":    {"min_files": 4, "claude_md": True},
    # Datasheet constant. Invariant 3 forbids baking it into stored data,
    # which makes the documented value the only copy there is.
    "digmesa-k-factor":    {"min_files": 4, "claude_md": True},
    # Firmware constant with a known-insufficient value and four prose
    # sites discussing it. The header is the source of truth.
    "brew-guard-ms":       {"min_files": 4, "claude_md": True},
    # Which colour is which conductor on the J4 lead. The one divergence in
    # this repo that could put a probe on the wrong wire -- and `+` is the
    # conductor with the read-only rule.
    "j4-conductor-map":    {"min_files": 2, "claude_md": True},
    # The one rule in this repo that is DELIBERATELY stated twice, because
    # it has been lost from CLAUDE.md three times. The duplication is the
    # point; the tag is what stops the two copies drifting apart.
    "j4-plus-readonly":    {"min_files": 2, "claude_md": True},
}

SCAN_SUFFIXES = (".md", ".h", ".cpp", ".c", ".go", ".sql", ".py")

# This file is full of example markers; scanning it would invent facts.
SKIP_PATHS = {"tools/check-facts.py"}

# `<!--` for markdown/HTML, `//` for C/C++/Go, `--` for SQL, `#` for python
# and shell. SCAN_SUFFIXES covers .sql and .py, and an earlier version of this
# regex accepted neither of their comment leaders -- so a tag written there
# was silently ignored, and the registry could not notice a site that never
# registered in the first place. Found by review, 2026-10-19.
MARKER = re.compile(
    r"(?:<!--|//|--|#)\s*fact:([a-z0-9][a-z0-9-]*)\s+(.+?)\s*(?:-->|$)"
)


def split_needles(value):
    """`;;` wins when present, so a needle can contain a literal `|`.

    Without this, a tag on a markdown table could only assert that a set of
    words appears nearby -- which does not pin a MAPPING. Reproduced by
    review on 2026-10-19: swapping the `yellow` and `green` cells between
    the `T` and `+` rows of the J4 conductor table still passed, on the one
    connector where a swapped colour puts a probe on the read-only wire.
    """
    sep = ";;" if ";;" in value else "|"
    return [n for n in value.split(sep) if n.strip()]


def normalise(text):
    """Collapse the renderings this repo legitimately varies between.

    "16.4 kΩ", "16.4 K" and "16.4K" are the same number written three ways,
    and all three are in the repo today. "2.7 %" and "2.9 %" are not.
    """
    t = text.lower()
    for ch in (" ", " ", " "):
        t = t.replace(ch, " ")
    t = re.sub(r"[*`~]", "", t)
    # Blockquote markers are layout, not content. Without this, a rule
    # wrapped as "> ... and nothing\n> else." cannot be matched by any
    # needle that spans the line break -- and the J4 `+` rule, the one
    # clause in this repo that is deliberately stated twice, is a
    # blockquote in both copies with DIFFERENT wrap points.
    t = re.sub(r"(?m)^[ \t]*>+[ \t]?", "", t)
    for ch in ("−", "–", "—"):
        t = t.replace(ch, "-")
    t = t.replace("kω", "k").replace("ω", "ohm")
    t = t.replace("µ", "u").replace("μ", "u")
    t = re.sub(r"\bkohm\b", "k", t)
    # "134 mV" -> "134mv", "5 %" -> "5%": digit followed by a unit word.
    t = re.sub(r"(\d)\s+(?=[a-z%])", r"\1", t)
    t = re.sub(r"\s*/\s*", "/", t)
    t = re.sub(r"\s*\+\s*", "+", t)
    t = re.sub(r"\s+", " ", t)
    return t.strip()


def repo_root():
    """Everything below is relative to the repo root.

    SKIP_PATHS and the `claude_md` registry rule are root-relative paths, so
    running this from tools/ used to produce a page of false errors AND scan
    this file's own docstring, inventing facts from its examples. Found by
    review, 2026-10-19.
    """
    return subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()


def tracked_files():
    # --others --exclude-standard includes files that are new and not yet
    # `git add`ed. Without it a brand-new doc is invisible to this check
    # until the commit that adds it -- which is precisely the commit where
    # its markers are most likely to be wrong, and it cost a false pass.
    out = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        capture_output=True,
        text=True,
        check=True,
        cwd=repo_root(),
    ).stdout
    for path in out.split("\0"):
        if not path or path in SKIP_PATHS:
            continue
        if path.endswith(SCAN_SUFFIXES):
            yield path


def collect():
    root = repo_root()
    sites = defaultdict(list)
    for path in tracked_files():
        try:
            lines = open(os.path.join(root, path), encoding="utf-8").read().splitlines()
        except UnicodeDecodeError:
            continue
        except (FileNotFoundError, IsADirectoryError):
            # `git ls-files --cached` lists a file that has been deleted or
            # moved in the worktree but not yet staged. That is an ordinary
            # mid-edit state, not a reason to traceback. Found by review.
            continue
        for i, line in enumerate(lines):
            m = MARKER.search(line)
            if not m:
                continue
            lo = max(0, i - CONTEXT_LINES)
            hi = min(len(lines), i + CONTEXT_LINES + 1)
            # The marker's own text is not evidence for itself.
            context = "\n".join(
                MARKER.sub("", lines[j]) if j == i else lines[j] for j in range(lo, hi)
            )
            sites[m.group(1)].append(
                {
                    "path": path,
                    "line": i + 1,
                    "value": m.group(2),
                    "context": normalise(context),
                }
            )
    return sites


def main():
    sites = collect()
    errors = []

    for fact, entries in sorted(sites.items()):
        # 1. agreement
        by_value = defaultdict(list)
        for e in entries:
            by_value[normalise(e["value"])].append(e)
        if len(by_value) > 1:
            lines = [f"fact:{fact} -- {len(by_value)} different values:"]
            for value, group in sorted(by_value.items()):
                where = ", ".join(f"{e['path']}:{e['line']}" for e in group)
                lines.append(f"    {value!r}  <-  {where}")
            errors.append("\n".join(lines))

        # 2. anchoring
        for e in entries:
            for needle in split_needles(e["value"]):
                n = normalise(needle)
                if n and n not in e["context"]:
                    errors.append(
                        f"fact:{fact} at {e['path']}:{e['line']} claims {needle.strip()!r} "
                        f"but no such text appears within {CONTEXT_LINES} lines of the "
                        f"marker. Either the prose changed and the tag did not, or the "
                        f"tag is on the wrong line."
                    )

    # 3. registry
    for fact, rule in sorted(REQUIRED.items()):
        entries = sites.get(fact, [])
        # Count distinct FILES, not markers. The point of a minimum is
        # cross-FILE coverage; two markers in one file satisfied the old
        # count while proving nothing about any other file. Found by review.
        files = {e["path"] for e in entries}
        if len(files) < rule["min_files"]:
            errors.append(
                f"fact:{fact} is tagged in {len(files)} file(s) "
                f"({', '.join(sorted(files)) or 'none'}), REQUIRED says at least "
                f"{rule['min_files']}. A file was deleted or lost its marker."
            )
        if rule.get("claude_md") and not any(
            e["path"] == "CLAUDE.md" for e in entries
        ):
            errors.append(
                f"fact:{fact} has no site in CLAUDE.md. The live conclusion is "
                f"supposed to stay in the auto-loaded file even when the "
                f"arithmetic behind it moves to docs/."
            )

    if errors:
        print("FACT CHECK FAILED\n", file=sys.stderr)
        for err in errors:
            print(f"  {err}\n", file=sys.stderr)
        return 1

    total = sum(len(v) for v in sites.values())
    print(f"fact check OK: {len(sites)} facts, {total} sites agree and are anchored.")
    for fact, rule in sorted(REQUIRED.items()):
        files = sorted({e["path"] for e in sites.get(fact, [])})
        print(f"  {fact:22s} {len(files)} files: {', '.join(files)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
