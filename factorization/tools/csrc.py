"""Small helpers for reading the target policy's C/C++ sources: find a function's
definition, its parameter list and its body, and strip comments. The policy is
written in a regular style (one `static T name(params) {` per definition), which
these helpers rely on; every use is checked by exact parity downstream."""
import os, re

SOURCES = None


def load(d):
    """{filename: text} for every policy source in directory d."""
    return {f: open(os.path.join(d, f)).read() for f in sorted(os.listdir(d))
            if f.endswith((".inc", ".cpp", ".h", ".hpp"))}


def strip_comments(t):
    """Blank out comments, keeping every offset and newline (so positions match the original)."""
    blank = lambda m: re.sub(r"[^\n]", " ", m.group(0))
    return re.sub(r"/\*.*?\*/|//[^\n]*", blank, t, flags=re.S)


def match_brace(t, i):
    """Index just past the brace block that opens at t[i] == '{' (comments must be stripped)."""
    depth = 0
    for j in range(i, len(t)):
        if t[j] == "{":
            depth += 1
        elif t[j] == "}":
            depth -= 1
            if depth == 0:
                return j + 1
    raise ValueError("unbalanced braces")


DEF = r"^(?P<head>(?:static |inline |template\s*<[^>]*>\s*)*(?P<ret>[A-Za-z_][\w:<>]*(?:\s*\*)?)\s+(?P<name>{name})\s*\((?P<params>[^)]*)\)\s*)\{{"


def find_def(srcs, name):
    """(file, start, end, ret, params, body) of the definition of `name`, or None."""
    pat = re.compile(DEF.format(name=re.escape(name)), re.M)
    for f, t in srcs.items():
        clean = strip_comments(t)
        m = pat.search(clean)
        if m:
            open_i = m.end() - 1
            end = match_brace(clean, open_i)
            return dict(file=f, start=m.start(), end=end, ret=m.group("ret").strip(),
                        params=" ".join(m.group("params").split()), body=t[open_i + 1:end - 1],
                        clean_body=clean[open_i + 1:end - 1])
    return None


def split_params(params):
    params = params.strip()
    if not params or params == "void":
        return []
    return [p.strip() for p in params.split(",")]
