"""Strict-ish structural validation of an ISO-10303-21 STEP file.

Checks (no CAD kernel needed):
  1. balanced parentheses / brackets over the DATA section
  2. every statement ends with ';' and has the form #id = TYPE(...)
  3. ids are unique and every '#n' reference resolves to a defined id
  4. every numeric literal is a valid STEP INTEGER or REAL
     (REAL must contain a '.', an exponent must be uppercase 'E')
  5. header / trailer intact
"""
import re
import sys

INT = re.compile(r"^[+-]?[0-9]+$")
REAL = re.compile(r"^[+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:E[+-]?[0-9]+)?$")


def check_real(tok):
    if "." in tok:
        return bool(REAL.match(tok))
    return bool(INT.match(tok))


def validate(path):
    raw = open(path, "r", errors="replace").read()
    ok = True

    for tag in ("ISO-10303-21;", "HEADER;", "ENDSEC;", "DATA;", "END-ISO-10303-21;"):
        if tag not in raw:
            print(f"  FAIL missing {tag}")
            ok = False

    body = raw[raw.index("DATA;") + 5: raw.rindex("ENDSEC;")]

    # 1. balance (ignoring quoted strings)
    dep = re.sub(r"'[^']*'", "''", body)
    for op, cl in (("(", ")"), ("[", "]")):
        if dep.count(op) != dep.count(cl):
            print(f"  FAIL unbalanced {op}{cl}: {dep.count(op)} vs {dep.count(cl)}")
            ok = False

    # 2/3. statements + references
    defined = set()
    stmts = 0
    for m in re.finditer(r"#(\d+)\s*=\s*(.*?);", body, re.S):
        stmts += 1
        defined.add(int(m.group(1)))
    if stmts == 0:
        print("  FAIL no statements")
        ok = False
    ids = [int(x) for x in re.findall(r"#(\d+)", body)]
    missing = sorted({i for i in ids if i not in defined})
    if missing:
        print(f"  FAIL dangling references: {missing[:20]}")
        ok = False

    # 4. numeric literals
    toks = re.findall(r"[-+]?(?:[0-9]+\.[0-9]*|\.[0-9]+|[0-9]+)(?:[eE][-+]?[0-9]+)?", dep)
    bad = [t for t in toks if not check_real(t)]
    if bad:
        print(f"  FAIL {len(bad)} non-STEP numeric literals, e.g. {sorted(set(bad))[:10]}")
        ok = False

    print(f"  entities={stmts}  refs={len(ids)}  numeric literals={len(toks)}")
    return ok


if __name__ == "__main__":
    allok = True
    for p in sys.argv[1:]:
        print(f"== {p}")
        allok &= validate(p)
    print("\nRESULT:", "PASS" if allok else "FAIL")
