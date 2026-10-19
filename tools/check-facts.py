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

In C/C++/Go, `// fact:<id> <value>` instead of the HTML comment.

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

import re
import subprocess
import sys
from collections import defaultdict

# Lines either side of a marker that count as "its prose".
CONTEXT_LINES = 10

# Facts that must stay tagged, and the minimum number of sites each. A fact
# below its minimum fails the build: it means a site was deleted or lost its
# marker, which is exactly the state this check exists to prevent. Raise a
# number when you add a site; never lower one to make the build pass without
# saying why in the commit message.
#
# `claude_md` means at least one site must be in CLAUDE.md -- these are the
# facts whose live conclusion is supposed to stay in the auto-loaded file
# even though the arithmetic that justifies it has moved to docs/.
REQUIRED = {
    # Invariant 1 cites this as the only metered disturbance figure of the
    # three it names. It is the number that was wrong in status.md.
    "j4-node-disturbance": {"min_sites": 3, "claude_md": True},
    # The three fitted dividers. Values, not arithmetic -- the corner tables
    # live in docs/taps-and-dividers.md.
    "divider-j2":          {"min_sites": 3, "claude_md": True},
    "divider-j4":          {"min_sites": 3, "claude_md": True},
    # J5's shunt is the highest-risk number in the repo: it was stated in
    # full on five surfaces, and 15 K (as built) is superseded by 16.4 K
    # (specified). A stale 15 K reads as a live instruction.
    "divider-j5-shunt":    {"min_sites": 4, "claude_md": True},
    # Datasheet constant. Invariant 3 forbids baking it into stored data,
    # which makes the documented value the only copy there is.
    "digmesa-k-factor":    {"min_sites": 4, "claude_md": True},
    # Firmware constant with a known-insufficient value and four prose
    # sites discussing it. The header is the source of truth.
    "brew-guard-ms":       {"min_sites": 4, "claude_md": True},
}

SCAN_SUFFIXES = (".md", ".h", ".cpp", ".c", ".go", ".sql", ".py")

# This file is full of example markers; scanning it would invent facts.
SKIP_PATHS = {"tools/check-facts.py"}

MARKER = re.compile(r"(?:<!--|//)\s*fact:([a-z0-9][a-z0-9-]*)\s+(.+?)\s*(?:-->|$)")


def normalise(text):
    """Collapse the renderings this repo legitimately varies between.

    "16.4 kΩ", "16.4 K" and "16.4K" are the same number written three ways,
    and all three are in the repo today. "2.7 %" and "2.9 %" are not.
    """
    t = text.lower()
    for ch in (" ", " ", " "):
        t = t.replace(ch, " ")
    t = re.sub(r"[*`~]", "", t)
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
    ).stdout
    for path in out.split("\0"):
        if not path or path in SKIP_PATHS:
            continue
        if path.endswith(SCAN_SUFFIXES):
            yield path


def collect():
    sites = defaultdict(list)
    for path in tracked_files():
        try:
            lines = open(path, encoding="utf-8").read().splitlines()
        except UnicodeDecodeError:
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
            for needle in e["value"].split("|"):
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
        if len(entries) < rule["min_sites"]:
            errors.append(
                f"fact:{fact} has {len(entries)} site(s), REQUIRED says at least "
                f"{rule['min_sites']}. A site was deleted or lost its marker."
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
