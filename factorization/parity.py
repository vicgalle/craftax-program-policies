#!/usr/bin/env python3
"""Build and check the factorization P* = t = I(p0) = mix(I, p0) by exact parity.

    python3 factorization/parity.py check [--jobs 5] [--sets dev,holdout] [--no-tests]

`check` does everything:
  1. builds P* (the C policy in policies/optimized_extension) and records its
     action and skill streams on the dev set (64 worlds, seed 2100000000) and the
     held-out set (256 worlds, seed 2110000000);
  2. builds the target t (target/, C++), compiles the rule program p0 (p0/*.tr)
     with trc against the machine I, and builds the interpreted I(p0) and the
     specialized mix(I, p0);
  3. replays every episode from reset on the live engine with the scratch memory
     poisoned before every decision, and reports the first step at which a
     candidate's (action, skill) differs from P*'s; it also runs I(p0) with p0
     loaded at run time from its binary image (TR_PROGRAM), on the dev set;
  4. runs the policy unit tests against every candidate.

Other commands:
    parity.py record SET                     record P*'s reference streams for SET
    parity.py parity DIR [--set SET]         check one policy directory against them
    parity.py assemble [PROGRAM_DIR]         compile a TR program (default p0/) into build/<name>/{interp,spec}
    parity.py test DIR                       policy unit tests against DIR
    parity.py time DIR [DIR ...]             replay timing on the 8-episode dev trace (best of --rounds)

A policy directory holds full_policy.c (C) or full_policy.cpp (C++, one translation
unit), full_policy.h (the C ABI) and its included sources. Sources are copied into
build/ and compiled there with fixed flags: C11 for C, C++23 for C++.
"""
import argparse, json, os, shutil, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(HERE, "build")
HARNESS = os.path.join(HERE, "harness")
PSTAR = os.path.join(ROOT, "policies", "optimized_extension")
TARGET = os.path.join(HERE, "target")
MACHINE = os.path.join(HERE, "I")
P0 = os.path.join(HERE, "p0")
TRC = os.path.join(HERE, "trc", "trc.py")

FLAGS = ["-O3", "-std=c11", "-ffp-contract=off", "-Wno-unused-function"]
CXXFLAGS = ["-O3", "-std=c++23", "-ffp-contract=off", "-fno-exceptions", "-fno-rtti", "-Wno-unused-function"]
CXXFLAGS += os.environ.get("POLICY_CXX_EXTRA", "").split()  # opt-in, e.g. a libc++ include path
SETS = {"dev": (2100000000, 64), "holdout": (2110000000, 256)}
EXPECTED_STEPS = {"dev": 3209631, "holdout": 12354934}
TIMING_EPISODES = (15, 34, 36, 41, 57, 58, 60, 63)  # dev episodes of the timing trace
SOURCE_EXT = (".c", ".cpp", ".h", ".hpp", ".inc")


def sh(cmd, what, env=None):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, env=env)
    if r.returncode:
        sys.exit(f"{what} failed:\n" + r.stderr[-6000:])
    return r.stdout


def stage(d, name):
    """Copy a policy directory's sources into build/cand/NAME/src."""
    dest = os.path.join(BUILD, "cand", name)
    src = os.path.join(dest, "src")
    if os.path.abspath(d) != src:
        shutil.rmtree(src, ignore_errors=True)
        os.makedirs(src)
        for f in os.listdir(d):
            if f.endswith(SOURCE_EXT):
                shutil.copyfile(os.path.join(d, f), os.path.join(src, f))
    return dest, src


def link(d, main_src, out_name, name=None):
    """A C program (bench or unit test) linked against the policy in directory d."""
    name = name or os.path.basename(os.path.normpath(d))
    dest, src = stage(d, name)
    obj = os.path.join(dest, "full_policy.o")
    if os.path.exists(os.path.join(src, "full_policy.cpp")):
        sh(["clang++", *CXXFLAGS, "-I", src, "-c", os.path.join(src, "full_policy.cpp"), "-o", obj], f"{name}: policy build (C++)")
    else:
        sh(["cc", *FLAGS, "-I", src, "-c", os.path.join(src, "full_policy.c"), "-o", obj], f"{name}: policy build (C)")
    exe = os.path.join(dest, out_name)
    sh(["cc", *FLAGS, "-I", src, "-c", main_src, "-o", exe + ".main.o"], f"{name}: build of {os.path.basename(main_src)}")
    sh(["clang++", exe + ".main.o", obj, "-o", exe, "-lm"], f"{name}: link")
    return exe


def bench(d, name=None):
    return link(d, os.path.join(HARNESS, "bench.c"), "bench", name)


def chunks(count, jobs):
    size = max(1, (count + 4 * jobs - 1) // (4 * jobs))
    return [(a, min(size, count - a)) for a in range(0, count, size)]


def run_set(exe, which, jobs, extra, env=None):
    seed, total = SETS[which]

    def one(part):
        a, n = part
        r = subprocess.run([exe, "run", "--seed", str(seed), "--first", str(a), "--count", str(n), *extra],
                           capture_output=True, text=True, env=env)
        rows = [json.loads(l) for l in r.stdout.splitlines() if l.startswith("{")]
        if len(rows) != n:
            sys.exit(f"bench crashed on episodes {a}..{a+n-1}:\n{r.stderr[-2000:]}")
        return rows

    with ThreadPoolExecutor(jobs) as ex:
        return [row for part in ex.map(one, chunks(total, jobs)) for row in part]


def ref_dir(which):
    return os.path.join(BUILD, "ref", which)


def record(which, jobs):
    exe = bench(PSTAR, "P_star")
    out = ref_dir(which)
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    rows = run_set(exe, which, jobs, ["--dump", out])
    steps = sum(r["steps"] for r in rows)
    print(json.dumps({"record": which, "policy": "P*", "episodes": len(rows), "steps": steps}))
    return steps


def parity(d, which, jobs, poison=True, name=None, env=None):
    exe = bench(d, name)
    if not os.path.isdir(ref_dir(which)):
        sys.exit(f"no reference streams for {which!r}: run `parity.py record {which}` first")
    rows = run_set(exe, which, jobs, ["--check", ref_dir(which)] + (["--poison"] if poison else []), env)
    bad = [r for r in rows if r["diverged"] >= 0]
    steps = sum(r["steps"] for r in rows)
    label = name or os.path.basename(os.path.normpath(d))
    print(json.dumps({"candidate": label, "set": which, "episodes": len(rows), "steps": steps,
                      "diverged": len(bad), "parity": not bad}))
    for r in sorted(bad, key=lambda r: r["ep"]):
        print(f"  ep {r['ep']}: step {r['diverged']} expected (action,skill)={tuple(r['expected'])} got {tuple(r['got'])}")
    return not bad, steps


def assemble(program=P0, name="p0"):
    """Compile a TR program (floor8.tr first, then the other .tr files in name order) against I."""
    out = os.path.join(BUILD, name)
    os.makedirs(out, exist_ok=True)
    files = sorted(f for f in os.listdir(program) if f.endswith(".tr"))
    if "floor8.tr" in files:
        files.remove("floor8.tr")
        files.insert(0, "floor8.tr")
    stats = sh([sys.executable, TRC, *[os.path.join(program, f) for f in files], "--interp", MACHINE,
                "-o", os.path.join(out, "program.gen.hpp"), "--image", os.path.join(out, "program.trb")], "trc")
    for mode in ("interp", "spec"):
        d = os.path.join(out, mode)
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        for f in os.listdir(MACHINE):
            if f.endswith(SOURCE_EXT):
                shutil.copyfile(os.path.join(MACHINE, f), os.path.join(d, f))
        shutil.copyfile(os.path.join(out, "program.gen.hpp"), os.path.join(d, "program.gen.hpp"))
        with open(os.path.join(d, "tr_config.h"), "w") as fh:
            fh.write("#define TR_SPEC 1\n" if mode == "spec" else "/* dynamic interpretation */\n")
    return out, stats.strip()


def unit_tests(d, name=None):
    exe = link(d, os.path.join(HARNESS, "full_policy_test.c"), "full_policy_test", name)
    r = subprocess.run([exe], capture_output=True, text=True)
    text = (r.stdout + r.stderr).strip()
    last = text.splitlines()[-1] if text else "no output"
    print(json.dumps({"candidate": name or os.path.basename(os.path.normpath(d)), "unit_tests": last,
                      "pass": r.returncode == 0}))
    return r.returncode == 0


def timing_trace():
    path = os.path.join(BUILD, "traces", "timing.bin")
    if os.path.exists(path):
        return path
    os.makedirs(os.path.dirname(path), exist_ok=True)
    exe = bench(PSTAR, "P_star")
    seed, _ = SETS["dev"]
    parts = []
    for ep in TIMING_EPISODES:
        part = f"{path}.{ep}"
        sh([exe, "run", "--seed", seed, "--first", ep, "--count", 1, "--trace", part], "trace recording")
        parts.append(part)
    with open(path, "wb") as out:
        for part in parts:
            with open(part, "rb") as fh:
                shutil.copyfileobj(fh, out)
            os.remove(part)
    return path


def timing(dirs, rounds, repeat):
    trace = timing_trace()
    exes = {os.path.basename(os.path.normpath(d)): bench(d) for d in dirs}
    best = {k: None for k in exes}
    for _ in range(rounds):  # alternate candidates within each round
        for k, exe in exes.items():
            r = json.loads(sh([exe, "replay", trace, "--repeat", repeat], f"replay {k}"))
            if r["mismatched_episodes"]:
                sys.exit(f"{k}: replay does not match the trace")
            if best[k] is None or r["ns_per_step"] < best[k]["ns_per_step"]:
                best[k] = r
    for k, r in best.items():
        print(json.dumps({"candidate": k, "steps": r["steps"], "ns_per_step": r["ns_per_step"],
                          "instructions_per_step": r["instructions_per_step"]}))


def check(jobs, sets, tests):
    ok = True
    _, stats = assemble()
    print(json.dumps({"assemble": "p0", "trc": stats}))
    built = os.path.join(BUILD, "p0")
    candidates = [("P_star", PSTAR), ("t", TARGET), ("I_p0_interp", os.path.join(built, "interp")),
                  ("mix_I_p0_spec", os.path.join(built, "spec"))]
    for which in sets:
        steps = record(which, jobs)
        if steps != EXPECTED_STEPS[which]:
            print(f"  note: {steps} reference steps, expected {EXPECTED_STEPS[which]}")
            ok = False
        for name, d in candidates:
            good, n = parity(d, which, jobs, name=name)
            ok &= good and n == steps
        if which == "dev":  # p0 as opaque run-time data: the interpreted build loads the .trb image
            env = dict(os.environ, TR_PROGRAM=os.path.join(built, "program.trb"))
            good, n = parity(os.path.join(built, "interp"), which, jobs, name="I_p0_interp", env=env)
            print("  (above: I(p0) with p0 loaded from program.trb via TR_PROGRAM)")
            ok &= good and n == steps
    if tests:
        for name, d in candidates:
            ok &= unit_tests(d, name)
    print("PARITY CHECK PASS" if ok else "PARITY CHECK FAIL")
    return ok


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("cmd", choices=["check", "record", "parity", "assemble", "test", "time"])
    p.add_argument("args", nargs="*")
    p.add_argument("--set", default="dev", choices=sorted(SETS))
    p.add_argument("--sets", default="dev,holdout")
    p.add_argument("--jobs", type=int, default=5)
    p.add_argument("--no-poison", action="store_true")
    p.add_argument("--no-tests", action="store_true")
    p.add_argument("--rounds", type=int, default=3)
    p.add_argument("--repeat", type=int, default=2)
    a = p.parse_args()
    if a.cmd == "check":
        sys.exit(0 if check(a.jobs, [s for s in a.sets.split(",") if s], not a.no_tests) else 1)
    elif a.cmd == "record":
        record(a.args[0] if a.args else a.set, a.jobs)
    elif a.cmd == "parity":
        sys.exit(0 if parity(a.args[0], a.set, a.jobs, not a.no_poison)[0] else 3)
    elif a.cmd == "assemble":
        program = a.args[0] if a.args else P0
        out, stats = assemble(program, os.path.basename(os.path.normpath(program)))
        print(stats)
        print(out)
    elif a.cmd == "test":
        sys.exit(0 if unit_tests(a.args[0]) else 1)
    else:
        timing(a.args or [TARGET, os.path.join(BUILD, "p0", "interp"), os.path.join(BUILD, "p0", "spec")],
               a.rounds, a.repeat)


if __name__ == "__main__":
    main()
