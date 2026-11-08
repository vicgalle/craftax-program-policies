"""Record layouts of the policy's C ABI structs, as the compiler lays them out.

Parses the typedef'd structs of full_policy.h, then compiles a probe that prints
offsetof/sizeof for every field (and the bit position of bitfields), so offsets
come from clang rather than from this parser. Output: JSON {struct: {size,
fields: {name: {off, type, dims, bits?}}}}, read by the L front-end to turn
`a.floors[i].marks[q].item` into a typed load at base + offset + i*stride + ...
"""
import json, os, re, subprocess, sys, tempfile

SCALARS = {"int": "i32", "float": "f32", "uint8_t": "u8", "int8_t": "i8", "uint16_t": "u16",
           "int16_t": "i16", "uint32_t": "u32", "uint64_t": "u64", "unsigned": "u32"}


def parse_header(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    structs = {}
    for m in re.finditer(r"typedef struct\s*\{([^{}]*)\}\s*(\w+)\s*;", text, flags=re.S):
        body, name = m.group(1), m.group(2)
        if "*" in body or "(" in body:
            continue  # pointer-holding records (the scratch) are reached only through natives
        fields = []
        for decl in body.split(";"):
            decl = " ".join(decl.split())
            if not decl:
                continue
            dm = re.match(r"(?:const\s+)?(\w+)\s+(.*)", decl)
            typ, rest = dm.group(1), dm.group(2)
            for part in split_top(rest):
                pm = re.match(r"(\w+)\s*(?::\s*(\d+))?\s*((?:\[[^\]]+\])*)$", part.strip())
                if not pm:
                    raise SystemExit(f"cannot parse field {part!r} in {name}")
                dims = re.findall(r"\[([^\]]+)\]", pm.group(3))
                fields.append(dict(name=pm.group(1), type=typ, bitwidth=int(pm.group(2)) if pm.group(2) else None,
                                   dims=dims))
        structs[name] = fields
    return structs


def split_top(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    return out


def probe(header_dir, structs):
    lines = ['#include "full_policy.h"', "#include <stdio.h>", "#include <stddef.h>", "#include <string.h>",
             "int main(void){"]
    for s, fields in structs.items():
        lines.append(f'printf("S {s} %zu\\n",sizeof({s}));')
        for f in fields:
            dims = "".join("[0]" for _ in f["dims"])
            if f["bitwidth"] is None:
                lines.append(f'printf("F {s} {f["name"]} %zu\\n",offsetof({s},{f["name"]}));')
                for k, d in enumerate(f["dims"]):
                    lines.append(f'printf("D {s} {f["name"]} {k} %d\\n",(int)({d}));')
            else:
                # bit position: set the field to all ones in a zeroed record and find the bits
                lines.append("{%s r;memset(&r,0,sizeof r);r.%s=(1u<<%d)-1;unsigned char b[sizeof r];memcpy(b,&r,sizeof r);"
                             "for(int i=0;i<(int)sizeof r;i++)if(b[i]){int sh=0;while(!((b[i]>>sh)&1))sh++;"
                             'printf("B %s %s %%d %%d\\n",i,sh);break;}}' % (s, f["name"], f["bitwidth"], s, f["name"]))
    lines.append("return 0;}")
    with tempfile.TemporaryDirectory() as td:
        src, exe = os.path.join(td, "probe.c"), os.path.join(td, "probe")
        open(src, "w").write("\n".join(lines))
        subprocess.run(["cc", "-std=c11", "-I", header_dir, src, "-o", exe], check=True)
        return subprocess.run([exe], capture_output=True, text=True, check=True).stdout


def build(header_dir):
    structs = parse_header(open(os.path.join(header_dir, "full_policy.h")).read())
    out = probe(header_dir, structs)
    res = {s: {"size": None, "fields": {}} for s in structs}
    for s, fields in structs.items():
        for f in fields:
            res[s]["fields"][f["name"]] = {"type": SCALARS.get(f["type"], f["type"]), "dims": [], "off": None}
            if f["bitwidth"] is not None:
                res[s]["fields"][f["name"]]["bits"] = [None, f["bitwidth"]]
    for line in out.splitlines():
        p = line.split()
        if p[0] == "S":
            res[p[1]]["size"] = int(p[2])
        elif p[0] == "F":
            res[p[1]]["fields"][p[2]]["off"] = int(p[3])
        elif p[0] == "D":
            res[p[1]]["fields"][p[2]]["dims"].append(int(p[4]))
        elif p[0] == "B":
            fd = res[p[1]]["fields"][p[2]]
            fd["off"], fd["bits"][0] = int(p[3]), int(p[4])
    return res


if __name__ == "__main__":
    json.dump(build(sys.argv[1]), open(sys.argv[2], "w"), indent=1)
