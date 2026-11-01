#!/usr/bin/env python3
"""Makes the bundled libraries (ports/one-ish/libraries/<BANK>/*.oneish) from Moo Won user patches converted with
IMPORT .M1P: the sounds Steveo, a friend of the owner's, made on his own instrument and is happy to share (owner,
2026-11-01: "they aren't official ones, it's just what my mate named them ... then include them"). One folder of
converted patches per bank in, one bank out:

    python3 ports/one-ish/libraries/make_bundled.py <converted folder> <BANK> [<converted folder> <BANK> ...]

Each patch: tagged GROUP Steveo, its notes "By Steveo." plus its own comment (the import's details left out), and
("By Moo Won" is for the sounds made for Moo Won itself: owner, 2026-11-01)
the maker's name written MOO wherever a name or comment says MOOG (the project keeps that name out). The bank's old
files are replaced. The .m1p files themselves never go into the project."""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MOOG = re.compile(r"moog", re.IGNORECASE)


def moo(text):   # MOOG -> MOO, Moog -> Moo, moog -> moo (POLYMOOG -> POLYMOO)
    return MOOG.sub(lambda m: "MOO" if m.group(0).isupper() else "Moo" if m.group(0)[0].isupper() else "moo", text)


def clean(text):
    out = []
    for line in text.split("\n"):
        key, eq, val = line.partition("=")
        if key == "group":
            line = "group=Steveo"
        elif key == "notes":
            m = re.search(r"Its comment: (.*?)\. Its group:", val) or re.search(r"Its comment: (.*?)\. \d+ of its", val)
            comment = m.group(1).strip().strip('"').strip() if m else ""
            if re.search(r"bella donna", comment, re.IGNORECASE):   # (a tribute in its comment, not its maker: owner)
                comment = "Inspired by a favourite performance sound."
            line = "notes=By Steveo." + (" " + moo(comment) if comment else "")
        elif eq and (key == "name" or key.endswith("syn_name")):
            line = key + "=" + moo(val)
        out.append(line)
    return "\n".join(out)


def main(args):
    if len(args) < 2 or len(args) % 2:
        sys.exit(__doc__)
    for src, bank in zip(args[::2], args[1::2]):
        dst = os.path.join(HERE, bank)
        os.makedirs(dst, exist_ok=True)
        for f in os.listdir(dst):
            if f.endswith(".oneish"):
                os.remove(os.path.join(dst, f))
        n = 0
        for f in sorted(os.listdir(src)):
            if not f.endswith(".oneish"):
                continue
            with open(os.path.join(src, f), encoding="utf-8", errors="replace", newline="") as fh:
                text = clean(fh.read())
            name = moo(f)
            with open(os.path.join(dst, name), "w", encoding="utf-8", newline="") as fh:
                fh.write(text)
            n += 1
        print("%s: %d patches" % (bank, n))


if __name__ == "__main__":
    main(sys.argv[1:])
