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

SEVERAL MARKERS ON ONE LINE are supported (a markdown table row cannot take a
marker on its own line, so two facts tagged inline on one row are legitimate).
Two cautions. An HTML marker ends at `-->`, so two of them coexist; a `//`,
`--` or `#` marker has no terminator and its value runs to the END of the
line, so a second marker after one of those is swallowed into its value.
And a marker's own text is never evidence: every marker is stripped from the
context window before needles are searched for.

`fact:<id>@<n>` TIGHTENS THE WINDOW for that one marker: the needles must
appear within n lines of it instead of CONTEXT_LINES. The window is SYMMETRIC: `@n` is n lines
either side, not n lines down. `photomos-guarantee-25c@2` relies on that --
its satisfier is two lines ABOVE its marker. The cost is that a comment or
paragraph above a tight marker is inside the window too, so quoting the value
there re-weakens it; see brew_guard.h, where the narrative had to stop
quoting the numbers. Use `@1` on a headline site -- the sentence next to the
marker -- whenever the surrounding paragraph legitimately repeats the value, which in this repo is most of them:
a retraction narrative quotes the number, a warning restates it, a comparison
names it twice. With the default window those repetitions SATISFY the needle,
so the anchor proves only that the paragraph mentions the value somewhere and
the headline itself can be edited freely. Verified by planting: before `@1`,
changing "134 mV (2.9 %)" to "126 mV (2.7 %)" at the 0d site PASSED, rescued
by the parenthetical nine lines below that quotes both renderings.

The alternative was rewording those repetitions, and a draft of this change
did exactly that. It is the wrong trade twice over: the repetitions are the
deliberately-kept history this repo exists to preserve, and swapping a literal
for "that same pair" introduced a WRONG REFERENT -- the pronoun bound to the
nearest antecedent, which was the J4 divider, inverting the claim into "0d
rejects 100 K / 220 K". Do not contort prose to satisfy this script.

`@n` is part of the id token, not the value, so it does not participate in the
agreement check: two sites of one fact may carry different windows.

A TAG PINS PRESENCE, NOT MEANING, and that is a limit of the design rather
than a bug to be fixed with a cleverer needle. A site that keeps the needle's
literal while promoting a different value -- a parenthetical "(330 ohm, 1 %
is the hot-box option)", or a struck-through rejected value -- still passes,
because the check cannot read intent. The parenthetical and the strikethrough
cannot be closed by any needle, positive or negative, since the literal that
is the VALUE and the literal that is the REJECTED ALTERNATIVE are the same
string. A bare promoted value ("390 ohm, 1 %" standing where "330 ohm, 1 %"
was) COULD be closed by a negative needle -- a `!needle` form meaning "must
be absent from the context" is about five lines -- but this script does not
have one, and it would still miss "390 ohm only for a cool box" because that
phrase does not contain ", 1 %". What the check does catch is the realistic
drift: a number edited, a qualifier dropped, a marker lost, a typo that
shifts a digit. It does NOT catch a qualifier dropped at a site whose
neighbour, within CONTEXT_LINES, carries the same literal. The example this
paragraph used to give -- shopping-list's 330 ohm parts row rescued by the
LED-drive lead nine lines below -- NO LONGER REPRODUCES, and is kept only as
the shape of the problem: that site has one satisfier today and planting the
dropped qualifier now fails. The weak-anchor report at the end of a run
lists the live ones instead of this prose guessing at them.

WHAT IT STILL DOES NOT CATCH, stated plainly so nobody trusts it further
than it goes: an UNTAGGED site. If a seventh place states the K-factor and
carries no marker, this script is blind to it. The registry below is the
partial answer -- it pins a minimum number of FILES per fact (and, for the
four facts that carry `min_sites`, a minimum number of sites), so deleting a
tagged file's last marker fails rather than silently shrinking coverage --
but adding an untagged one still passes, and deleting a second marker inside
a file that keeps another passes for every fact that has no `min_sites`. Tag new sites as you write them.
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
# `min_sites` is OPTIONAL and counts markers, for the facts whose sites crowd
# into the same files. `min_files` alone cannot see a marker deleted from
# inside a file that still has another one -- which is exactly how a table
# row's second inline marker was lost to the old `search`. Added 2026-10-19
# for the two photoMOS drive facts; set it to the number tagged TODAY and
# raise it when you tag more, never lower it to turn the build green.
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
    # The constant itself, as opposed to the seconds every prose site quotes.
    # One file on purpose -- there is nowhere else for it -- so this entry
    # buys only "the tag still exists", which is the point: `brew-guard-ms`
    # sat ON this line and did not guard it, because the comment above
    # satisfied the seconds needle. 75000 -> 25000 passed. Review, 2026-10-20.
    "brew-guard-ms-const": {"min_files": 1, "claude_md": False},
    # Which colour is which conductor on the J4 lead. The one divergence in
    # this repo that could put a probe on the wrong wire -- and `+` is the
    # conductor with the read-only rule.
    "j4-conductor-map":    {"min_files": 2, "claude_md": True},
    # The one rule in this repo that is DELIBERATELY stated twice, because
    # it has been lost from CLAUDE.md three times. The duplication is the
    # point; the tag is what stops the two copies drifting apart.
    "j4-plus-readonly":    {"min_files": 2, "claude_md": True},
    # The arm-window cap. Tagged 2026-10-30 on a safety design still marked
    # PROPOSED; the number is what keeps a hold under the ~2 s reprogram
    # threshold, so losing one copy's tag must fail the build. Both tagged
    # sites sit in gate prose. If the bench sweep drops the gate, MOVE the
    # tags to whatever then bounds the arm window and re-state the margin
    # ordering with them -- do not just delete the entry.
    "arm-window-cap":      {"min_files": 2, "claude_md": True},
    # The one-shot pulse target, decided 2026-10-31 (~120 ms before). Three
    # sites state it as live; losing a tag must fail rather than let one
    # copy drift back to the old figure.
    "oneshot-target":      {"min_files": 3, "claude_md": True},
    # The photoMOS LED-drive resistor. Tagged 2026-10-19 after two review
    # rounds each found a surviving "330 or 390" / unqualified "guaranteed
    # 3.0 mA" site in a file the correction had not reached. NOT claude_md:
    # CLAUDE.md carries no drive figure, and putting one there to satisfy this
    # rule would add a copy to keep in sync rather than remove one.
    "led-drive-resistor":       {"min_files": 2, "min_sites": 4, "claude_md": False},
    # The 3.0 mA operate guarantee is a 25 C figure. The value pins both the
    # number and its temperature, so a site that drops the qualifier fails.
    "photomos-guarantee-25c":   {"min_files": 2, "min_sites": 5, "claude_md": False},
    # The stop_ms trio, tagged 2026-10-27 after FIVE review rounds on one
    # sentence, each of which corrected it in some of these three files and
    # not the others. Untagged duplication is exactly what this script is
    # for, and these were the most-restated untagged facts in the repo.
    # min_sites is set because these facts crowd into the same few files:
    # min_files alone cannot see a second marker deleted from a file that
    # keeps another one.
    "stop-ms-backdating":  {"min_files": 3, "min_sites": 3, "claude_md": True},
    # The lift-during-SETTLING overwrite. Its whole history is a wrong
    # bound: 5 s where it is STOP_WINDOW_MS + SETTLE_MS, understated by
    # exactly the backdating the paragraph exists to explain.
    # Four files: the trio plus BOTH v_shot definitions, which restate it for
    # query writers. 001_init.sql carries two sites (the column and the view),
    # which is what min_sites is here to see -- min_files alone cannot notice
    # one of them going.
    "stop-ms-lift-bound":  {"min_files": 4, "min_sites": 5, "claude_md": True},
    # CLAUDE.md's table is the only place stating WHICH path backdates, and a
    # plain needle pins presence, not mapping: swapping the two rows' middle
    # cells inverted the claim and passed. This is the `;;` row form, and it
    # is CLAUDE.md-only because the other files say it in prose that no
    # shared needle can normalise to.
    "stop-ms-row-mapping": {"min_files": 1, "claude_md": True},
    # The constants the prose quotes as "2 s" and "5 s", and whose SUM it
    # quotes as "~7 s". Without these, STOP_WINDOW_MS 2000 -> 3000 passed
    # every other fact while seven prose sites went stale -- the
    # `brew-guard-ms-const` trap exactly.
    #
    # IT IS NOT SHUT, only narrowed, and `brew-guard-ms-const` has the same
    # limit: this pins LITERALS, NOT RELATIONSHIPS. Editing the constant and
    # its own marker together is green while every prose site still says 2 s.
    # What it buys is that the half-edit -- the realistic one -- fails.
    "stop-window-ms-const": {"min_files": 1, "claude_md": False},
    "settle-ms-const":      {"min_files": 1, "claude_md": False},
    # NO `lift-count-46`. It was tagged 2026-10-27 and WITHDRAWN the same
    # day: the value pinned "seven of the first 46", and 46 is a five-filter
    # cohort, not the archive -- overshoot.py's own set is 45, and the repo
    # states the archive as both 78 and 80. A tag that asserts an
    # indefensible denominator is worse than no tag, because CI then defends
    # it. Do not re-add one until the archive size is settled from data.
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
    # `(@\S*)?` is deliberately loose. With `(@\d+)?` a typo like `@1x`, `@ `
    # or a bare `@` made the whole pattern fail to match, so the marker was
    # SILENTLY DROPPED -- not reported, just gone -- and its text was not
    # stripped from a neighbour's context either, so its value could vacuously
    # anchor a sibling marker. Planted `@1x` plus `FIT 16.4K -> FIT 15K` and
    # the run was green with the superseded shunt back in the firmware and
    # min_files still satisfied. Matching loosely and validating below turns
    # a typo into an error. Found by review, 2026-10-20.
    r"(?:<!--|//|--|#)\s*fact:([a-z0-9][a-z0-9-]*)(@\S*)?\s+(.+?)\s*(?:-->|$)"
)


def needle_in(needle, context):
    """Substring match, but a needle that starts like a number or a word
    must not match in the MIDDLE of one: "330 ohm, 1 %" is not present in
    "1330 ohm, 1 %", and "2.9 %" is not present in "12.9 %". Without this the
    check pinned a character sequence, not a value. Found by review of
    00439f6.

    The start boundary rejects a preceding word character (any script),
    digit, `.`, `,` or `-`: so `-134 mV` (normalise() turns a minus sign into
    `-`) does not satisfy "134 mV", nor `1,330` satisfy "330". The end
    boundary rejects a following digit, or a following `.`, `,` or `-` and
    digit, so "330 ohm, 1 %" does not match "330 ohm, 1 %5", and "pin 1"
    matches none of "pin 10", "pin 1.5", "pin 1,5", "pin 1-5" (but does match
    "pin 1." or "pin 1, and" -- punctuation, no digit after). A consequence
    worth knowing: an UNSPACED list or range after a needle ("150k,220k",
    "0.4-0.65" when the needle is "0.4") is rejected too. No live site is
    affected. It deliberately does
    NOT reject a following letter: normalise() strips markdown emphasis and
    backticks, which GLUES words together ("pin 1 `VSS`" becomes "pin 1vss"),
    and a letter-rejecting end boundary failed a live site on exactly that.
    So "75s" still matches inside "75 seconds" -- after normalise() collapses
    the space, "75 seconds" is "75seconds" -- an open hole, stated here.
    Some live needles end in a digit after normalise() (the J4 conductor row
    and the 3 W IRM-03-5 rule), which is why the end boundary exists; the
    history of how it was found is in the commit log, not here, because a
    docstring cannot be checked against its own count.
    Review of 271a1f9 found the start-only version let the signed, comma and
    trailing-digit cases through. Known false failure, deliberately kept: a needle
    glued to a period with no space ("stock.330") is rejected, because the
    alternative is allowing "0.330" to match "330".

    A needle that STARTS with a non-alphanumeric ("+5 V", "(x)") skips the
    boundary entirely and is a plain substring. None exists today."""
    if needle[0].isalnum():
        return re.search(
            r"(?<![\w.,\-])" + re.escape(needle) + r"(?![0-9]|[.,\-][0-9])", context
        ) is not None
    return needle in context


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
    errors_at_parse = []
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
            # finditer, not search: a markdown table row cannot take a
            # marker on its own line, so two facts tagged inline on one row
            # are legitimate -- and `search` silently registered only the
            # first, which let a site lose its tag without the build noticing.
            # Found 2026-10-19 while tagging the photoMOS drive figures.
            for m in MARKER.finditer(line):
                # `@n` on the id tightens this marker's window. See the
                # docstring: the default of 10 lines is satisfied by the
                # paragraph's own restatements of the value, so a headline
                # site needs @1 to actually guard its headline.
                suffix = m.group(2)
                # isdecimal(), not isdigit(): isdigit() accepts '²' and the
                # int() below then raises ValueError instead of this error.
                if suffix and not suffix[1:].isdecimal():
                    errors_at_parse.append(
                        f"{path}:{i + 1} fact:{m.group(1)} has a malformed window "
                        f"suffix {suffix!r}. It must be `@` followed by digits, "
                        f"e.g. `@1`. A malformed suffix used to drop the marker "
                        f"silently."
                    )
                    suffix = None
                span = int(suffix[1:]) if suffix else CONTEXT_LINES
                if suffix and span == 0:
                    # @0 leaves only the marker's own line, which is stripped
                    # of its marker -- so every needle fails and the message
                    # blames the prose. Same reasoning as the upper bound:
                    # reject the typo rather than act on it.
                    errors_at_parse.append(
                        f"{path}:{i + 1} fact:{m.group(1)} has @0, which leaves no "
                        f"lines to search. The smallest useful window is @1, the "
                        f"marker's own line and its neighbours."
                    )
                    span = CONTEXT_LINES
                elif span > CONTEXT_LINES:
                    # `@n` exists to TIGHTEN. Widening silently would make a
                    # weak anchor look deliberate, so it is an error rather
                    # than a clamp -- a clamp would hide the typo.
                    errors_at_parse.append(
                        f"{path}:{i + 1} fact:{m.group(1)} has @{span}, above the "
                        f"CONTEXT_LINES default of {CONTEXT_LINES}. `@n` tightens "
                        f"the window; it is not a way to widen one."
                    )
                    span = CONTEXT_LINES
                lo = max(0, i - span)
                hi = min(len(lines), i + span + 1)
                # No marker's text is evidence for anything. Stripping only
                # line i left an ADJACENT marker line's value in the window,
                # so two facts with overlapping needles on neighbouring lines
                # could anchor each other vacuously. Found by review of
                # d5f36d8; no live fact was affected.
                context = "\n".join(
                    MARKER.sub("", lines[j]) for j in range(lo, hi)
                )
                sites[m.group(1)].append(
                    {
                        "path": path,
                        "line": i + 1,
                        "span": span,
                        "lines": lines,
                        "value": m.group(3),
                        "context": normalise(context),
                    }
                )
    return sites, errors_at_parse


def main():
    sites, errors = collect()

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
                if n and not needle_in(n, e["context"]):
                    errors.append(
                        f"fact:{fact} at {e['path']}:{e['line']} claims {needle.strip()!r} "
                        f"but no such text appears within {e['span']} lines of the "
                        f"marker. Either the prose changed and the tag did not, the "
                        f"tag is on the wrong line, or — at a tight @n — the line was "
                        f"rewrapped without changing the value, which is a cost of "
                        f"@n and is fixed by rewrapping back or widening it."
                    )

    # 3. registry
    for fact, rule in sorted(REQUIRED.items()):
        entries = sites.get(fact, [])
        # Count distinct FILES, not markers. The point of a minimum is
        # cross-FILE coverage; two markers in one file satisfied the old
        # count while proving nothing about any other file. Found by review.
        files = {e["path"] for e in entries}
        if len(entries) < rule.get("min_sites", 0):
            errors.append(
                f"fact:{fact} has {len(entries)} tagged site(s), REQUIRED says at "
                f"least {rule['min_sites']}. A marker was deleted or lost, or the "
                f"minimum was raised past what is tagged."
            )
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

    # WEAK ANCHORS, reported and not failed. A needle satisfied by two or more
    # distinct lines in its window cannot tell which of them was edited, so the
    # anchor proves the paragraph mentions the value, not that the
    # authoritative sentence still says it. `@1` on the headline is usually the
    # fix. It is a warning rather than an error because some are unavoidable --
    # a retraction that quotes both the old and the new rendering is the thing
    # this repo exists to keep. Three review rounds found these one at a time;
    # this prints the list instead.
    #
    # TWO LIMITS, both real. Hits are counted PER LINE while anchoring runs on
    # the joined window, so a needle satisfied only ACROSS a line break scores
    # zero here -- that is not "no satisfier", it is "not auditable this way",
    # and it is reported separately rather than silently skipped. And a case
    # with exactly ONE satisfier is not weak by this measure even when that
    # line is not the authoritative one: nothing in the file says which
    # sentence is authoritative, so only multiplicity is machine-checkable.
    # `brew-guard-ms` on the constant had one satisfier and was invisible to
    # this; it took a planted edit to find. A draft of this comment listed it
    # as something the report catches. It does not.
    #
    # SO A SILENT REPORT IS NOT A GUARANTEE. phase-0-status's
    # j4-node-disturbance site went quiet under @3 while still failing a
    # planted edit to its own sentence, because @3's one remaining satisfier
    # was the NEXT sentence. It is back on the default window, annotated as
    # deliberately weak. Only a planted edit proves an anchor.
    weak = []
    spans = []
    for fact, entries in sorted(sites.items()):
        for e in entries:
            lines = e["lines"]
            i = e["line"] - 1

            def hits_for(needle, span):
                n = normalise(needle)
                return [
                    j + 1
                    for j in range(max(0, i - span), min(len(lines), i + span + 1))
                    if needle_in(n, normalise(MARKER.sub("", lines[j])))
                ]

            needles = [nd for nd in split_needles(e["value"]) if normalise(nd)]
            # A needle that failed anchoring is already an error; reporting it
            # here too would print "not auditable for weakness" beside its own
            # failure. The gate used to sit above this pass and a comment here
            # claimed we were past it; moving the pass earlier made that false.
            needles = [nd for nd in needles if needle_in(normalise(nd), e["context"])]
            if not needles:
                continue

            per = {nd: hits_for(nd, e["span"]) for nd in needles}
            if any(len(h) >= 2 for h in per.values()):
                # Smallest window leaving exactly one satisfier for EVERY needle
                # of this marker. Per-needle hints could flip exit 0 to 1 when a
                # sibling disagreed -- the same class as the regression the
                # comment above records. Found by review, 2026-10-20.
                fix = next(
                    (
                        f"@{n}"
                        for n in range(1, e["span"])
                        if all(len(hits_for(nd, n)) == 1 for nd in needles)
                    ),
                    None,
                )
                for nd, h in per.items():
                    if len(h) >= 2:
                        weak.append(
                            f"  weak anchor: fact:{fact} at {e['path']}:{e['line']} "
                            f"({nd.strip()!r} satisfied by lines {h}) -- "
                            + (
                                f"{fix} leaves one satisfier for every needle here"
                                f" -- CHECK IT IS THE AUTHORITATIVE LINE; the count"
                                f" is machine-checkable and the choice is not"
                                if fix
                                else "no window isolates one; annotate it as deliberate"
                            )
                        )
            for nd, h in per.items():
                if not h:
                    spans.append(
                        f"  spans lines: fact:{fact} at {e['path']}:{e['line']} "
                        f"({nd.strip()!r}) -- satisfied across a line break, so "
                        f"multiplicity cannot be judged"
                    )

    if weak:
        print(f"\n{len(weak)} weak anchor(s), not a failure:")
        for w in weak:
            print(w)
    if spans:
        print(f"\n{len(spans)} needle(s) satisfied across a line break:")
        for sp in spans:
            print(sp)

    if errors:
        print("FACT CHECK FAILED\n", file=sys.stderr)
        for err in errors:
            print(f"  {err}\n", file=sys.stderr)
        return 1

    total = sum(len(v) for v in sites.values())
    print(f"fact check OK: {len(sites)} facts, {total} sites agree and are anchored.")


    for fact, rule in sorted(REQUIRED.items()):
        files = sorted({e["path"] for e in sites.get(fact, [])})
        print(f"  {fact:22s} {len(files)} file{'' if len(files) == 1 else 's'}: {', '.join(files)}")


    return 0


if __name__ == "__main__":
    sys.exit(main())
