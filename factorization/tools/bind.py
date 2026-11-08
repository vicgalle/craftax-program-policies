#!/usr/bin/env python3
"""Generate I's bindings from its sources and its export lists.

    bind.py I_DIR

Writes, from I_DIR/exports/*.txt (read in name order), next to I's runtime
(tr_runtime.hpp, tr_machine.inc):
    tr_ops.gen.hpp      opcodes shared with the front end
    tr_natives.gen.hpp  one wrapper per exported native, the native table, constant tables
    schema.json         record layouts of the C ABI structs (tools/schema.py)
    natives.json        what programs may call: natives, machine constants, tables

An export list holds one native per line (`name` or `name = c_function`),
`const NAME` for a machine constant defined in full_policy.cpp,
`table NAME SIZE` for a constant int table, and `query NAME DEFAULT` for a
query: a zero-argument `def int NAME()` of the program that I calls back
(tr_query(a,o,s,TRQ_NAME)), DEFAULT when the program has none. Natives are the
program's calls into I; queries are I's calls into the program.
"""
import glob, json, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "trc"))
import csrc, schema, trc  # noqa: E402

ENV_PARAMS = {"FPAgent": "a", "FPObservation": "o", "FPConfig": "cfg", "FPScratch": "s"}


def parse_exports(paths, queries=None):
    natives, consts, tables = [], [], []
    for line in (l for p in paths for l in open(p)):
        line = line.split("#")[0].strip()
        if not line:
            continue
        if line.startswith("query "):
            if queries is not None:
                _, name, default = line.split()
                queries.append((name, int(default)))
            continue
        if line.startswith("const "):
            consts.append(line.split()[1])
        elif line.startswith("table "):
            _, name, size = line.split()
            tables.append((name, int(size)))
        else:
            name, _, cfun = (x.strip() for x in line.partition("="))
            natives.append((name, cfun or name))
    return natives, consts, tables


def classify_param(p):
    p = " ".join(p.replace("*", " * ").split())
    m = re.match(r"(?:const )?(FPAgent|FPObservation|FPConfig|FPScratch) \* \w+$", p)
    if m:
        return ("env", ENV_PARAMS[m.group(1)])
    if re.match(r"int \* \w+$", p):
        return ("out", None)
    if re.match(r"int \w+$", p):
        return ("i", None)
    if re.match(r"float \w+$", p):
        return ("f", None)
    if re.match(r"uint64_t \w+$", p):
        return ("mask", None)
    raise SystemExit(f"cannot bind parameter {p!r}")


def bindings(srcs, natives):
    wrappers, spec = [], []
    for name, cfun in natives:
        d = csrc.find_def(srcs, cfun)
        if d is None:
            raise SystemExit(f"exported native {cfun} not found in I")
        ret = d["ret"]
        if ret not in ("int", "float", "void"):
            raise SystemExit(f"{cfun} returns {ret}")
        args, kinds, k = [], [], 0
        for p in csrc.split_params(d["params"]):
            kind, env = classify_param(p)
            if kind == "env":
                args.append(f"e.{env}")
                continue
            kinds.append(kind)
            args.append({"i": f"v[{k}].i", "f": f"v[{k}].f", "out": f"&e.loc[v[{k}].i].i",
                         "mask": f"UINT64_C(1)<<v[{k}].i"}[kind])
            k += 1
        call = f"{cfun}({','.join(args)})"
        body = {"int": f"return tr_i({call});", "float": f"return tr_f({call});",
                "void": f"{call};return tr_i(0);"}[ret]
        wrappers.append(f"static TRVal trn_{name}(TREnv &e,const TRVal *v){{(void)e;(void)v;{body}}}")
        spec.append(dict(name=name, ret=ret, params=kinds))
    return wrappers, spec


def header_consts(i_dir):
    """Values of the header's FP_* enumerators (exported without the prefix) and of I's own constants."""
    text = csrc.strip_comments(open(os.path.join(i_dir, "full_policy.h")).read())
    names = []
    for body in re.findall(r"enum\s*\{([^}]*)\}", text):
        for item in body.split(","):
            item = item.strip()
            if item:
                names.append(item.split("=")[0].strip())
    return names


def probe_values(i_dir, names, extra_src=""):
    import subprocess, tempfile
    src = ['#include "full_policy.h"', "#include <stdio.h>", extra_src, "int main(void){"]
    src += [f'printf("%s %lld\\n","{n}",(long long)({n}));' for n in names]
    src.append("return 0;}")
    with tempfile.TemporaryDirectory() as td:
        open(os.path.join(td, "p.c"), "w").write("\n".join(src))
        subprocess.run(["cc", "-std=c11", "-I", i_dir, os.path.join(td, "p.c"), "-o", os.path.join(td, "p")], check=True)
        out = subprocess.run([os.path.join(td, "p")], capture_output=True, text=True, check=True).stdout
    return {l.split()[0]: int(l.split()[1]) for l in out.splitlines()}



def bind(out):
    srcs = csrc.load(out)
    queries = []
    natives, consts, tables = parse_exports(sorted(glob.glob(os.path.join(out, "exports", "*.txt"))), queries)
    qh = ["// Generated by tools/bind.py from the query lines of exports/*.txt. Do not edit.", "#pragma once",
          "/* Queries: the machine's calls into the program (tr_runtime.hpp). */",
          "enum { " + "".join(f"TRQ_{n.upper()}={k}," for k, (n, _) in enumerate(queries)) + f"TRQ_COUNT={len(queries)} }};",
          "static const int tr_query_default[" + str(max(1, len(queries))) + "]={" +
          ",".join(str(d) for _, d in queries) + ("" if queries else "0") + "};",
          "static int tr_query(const FPAgent *a,const FPObservation *o,FPScratch *s,int q);"]
    open(os.path.join(out, "tr_queries.gen.hpp"), "w").write("\n".join(qh) + "\n")
    names = [n for n, _ in natives]
    dup = {n for n in names if names.count(n) > 1}
    if dup:
        raise SystemExit(f"exported twice: {sorted(dup)}")
    wrappers, spec = bindings(srcs, natives)
    gen = ["// Generated by tools/bind.py from exports/*.txt. Do not edit.", "#pragma once"]
    gen += wrappers
    gen.append("static TRVal (*const tr_native_fn[])(TREnv &,const TRVal *)={" +
               ",".join(f"trn_{n}" for n, _ in natives) + "};")
    gen.append("static const int *const tr_tables[]={" + ",".join(n for n, _ in tables) + "};")
    open(os.path.join(out, "tr_natives.gen.hpp"), "w").write("\n".join(gen) + "\n")
    ops = ["// Generated by tools/bind.py from trc.OPS. Do not edit.", "#pragma once", "enum TROp {"]
    ops += [f" OP_{name} = {i}," for i, name in enumerate(trc.OPS)]
    ops.append("};")
    open(os.path.join(out, "tr_ops.gen.hpp"), "w").write("\n".join(ops) + "\n")
    vals = probe_values(out, header_consts(out))
    machine = {(n[3:] if n.startswith("FP_") else n): v for n, v in vals.items()}
    enum_src = csrc.strip_comments(open(os.path.join(out, "full_policy.cpp")).read())
    for c in consts:
        m = re.search(r"\b" + c + r"\s*=\s*([^,}\n]+)", enum_src)
        machine[c] = int(m.group(1).strip(), 0)
    json.dump(schema.build(out), open(os.path.join(out, "schema.json"), "w"), indent=1)
    json.dump(dict(natives=spec, consts=machine, tables=[dict(name=n, size=s) for n, s in tables],
                   queries=[dict(name=n, default=d) for n, d in queries]),
              open(os.path.join(out, "natives.json"), "w"), indent=1)
    return dict(natives=len(natives), consts=len(machine), tables=len(tables), queries=len(queries))


if __name__ == "__main__":
    print(json.dumps(bind(sys.argv[1])))
