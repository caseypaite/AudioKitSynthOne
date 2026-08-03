#!/usr/bin/env python3
"""
extract_tunings.py
AudioKitSynthOne - Linux port

Converts the curated tuning library in
AudioKitSynthOne/Tunings/Model/Tunings+DefaultTunings.swift into a JSON data
file the Linux build loads at runtime, so the scales are the exact ones the
iOS app ships rather than a hand transcription.

Each entry in the Swift source is a (name, closure) pair whose closure returns
a "master set" of frequency ratios. Closures are either a literal array or a
call to one of the generators in Tunings+Math.swift, all of which are
reimplemented faithfully below.

    ./extract_tunings.py <path to Tunings/Model> <output.json>
"""

import json
import re
import sys
from pathlib import Path


# --- generators, transcribed from Tunings+Math.swift ------------------------

def harmonic_series(root):
    """Harmonic series from n..2n."""
    out = [1.0]
    if root < 1:
        return out
    for n in range(root, 2 * root):
        if n != root:
            out.append(float(n) / float(root))
    return out


def sub_harmonic_series(root):
    """Subharmonic series from n..2n."""
    out = [1.0]
    if root < 1:
        return out
    for n in range(root, 2 * root):
        if n != root:
            out.append(float(root) / float(n))
    return out


def harmonic_subharmonic_series(root):
    """Union of the two, deduplicated and sorted -- matches the Swift Set()."""
    if root < 1:
        return [1.0]
    combined = set(harmonic_series(root)) | set(sub_harmonic_series(root))
    return sorted(combined)


def _combination_product_set(master, choose):
    """Wilson CPS: products of every `choose`-subset, in Swift's emitted order."""
    from itertools import combinations
    return [float(a) * float(b) for a, b in combinations(master, choose)]


GENERATORS = {
    "harmonicSeries": lambda args: harmonic_series(int(args[0])),
    "subHarmonicSeries": lambda args: sub_harmonic_series(int(args[0])),
    "harmonicSubharmonicSeries": lambda args: harmonic_subharmonic_series(int(args[0])),
    "hexany": lambda args: _combination_product_set(args, 2),
    "dekany": lambda args: _combination_product_set(args, 2),
    "pentadekany": lambda args: _combination_product_set(args, 2),
}


# --- parsing ---------------------------------------------------------------

def parse_number(token):
    """Swift numeric literal; underscores are digit separators, and simple
    ratios like 3/2 appear in a few scales."""
    token = token.strip().replace("_", "")
    if not token:
        return None
    if re.fullmatch(r"[0-9.eE+*/() -]+", token):
        return float(eval(token, {"__builtins__": {}}, {}))  # noqa: S307
    raise ValueError("unparseable number: %r" % token)


def parse_array(text):
    values = []
    for token in text.split(","):
        token = token.strip()
        if token:
            values.append(parse_number(token))
    return values


def extract(source):
    """Yields (name, master_set) for every entry in the file."""
    # ("Name", { return <expr> }
    pattern = re.compile(r'\(\s*"((?:[^"\\]|\\.)*)"\s*,\s*\{\s*return\s+(.*?)\s*\}', re.S)

    for match in pattern.finditer(source):
        name = match.group(1)
        expr = match.group(2).strip()

        if expr.startswith("["):
            depth, end = 0, None
            for i, ch in enumerate(expr):
                if ch == "[":
                    depth += 1
                elif ch == "]":
                    depth -= 1
                    if depth == 0:
                        end = i
                        break
            if end is None:
                raise ValueError("unterminated array for %r" % name)
            inner = expr[1:end]
            if "[" in inner:  # generator taking an array, e.g. hexany([...])
                raise ValueError("nested array for %r" % name)
            yield name, parse_array(inner)
            continue

        # Calls appear both bare and qualified, e.g. Tunings.hexany([...]).
        call = re.match(r"(?:Tunings\.)?([A-Za-z]+)\s*\(\s*(.*?)\s*\)\s*$", expr, re.S)
        if call:
            fn, raw = call.group(1), call.group(2)
            if fn not in GENERATORS:
                raise ValueError("unknown generator %r for %r" % (fn, name))
            raw = raw.strip()
            if raw.startswith("[") and raw.endswith("]"):
                args = parse_array(raw[1:-1])
            else:
                args = parse_array(raw)
            yield name, GENERATORS[fn](args)
            continue

        raise ValueError("unrecognised expression for %r: %r" % (name, expr[:60]))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)

    model_dir = Path(sys.argv[1])
    out_path = Path(sys.argv[2])

    source = (model_dir / "Tunings+DefaultTunings.swift").read_text(encoding="utf-8")

    tunings = []
    for order, (name, master) in enumerate(extract(source)):
        if not master:
            print("skipping empty tuning: %s" % name, file=sys.stderr)
            continue
        tunings.append({"name": name, "order": order, "masterSet": master})

    # The 12-ET default from Tuning.swift, always first.
    twelve_et = [2.0 ** (i / 12.0) for i in range(12)]
    tunings.insert(0, {"name": "12 ET", "order": -1, "masterSet": twelve_et})

    out_path.write_text(json.dumps({"tunings": tunings}, indent=1), encoding="utf-8")
    print("wrote %s with %d tunings" % (out_path, len(tunings)))


if __name__ == "__main__":
    main()
