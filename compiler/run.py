"""Compile a linked task program, build its evaluator and run episodes.

A program directory (written by compiler.link) holds the TR sources, their ORDER
and goal.json. `compile_tr` lowers it with trc against the machine I; `build`
makes either the specialized binary mix(I, p_g) (mode spec, used for every
reported episode) or the interpreted I(p_g) (mode interp, p_g loaded at run time).
The evaluator steps the native engine and scores the task with an independent
monitor that reads engine state, never the controller's own progress fields.

    python3 compiler/run.py PROGRAM_DIR --seed 2020000000 --episodes 4 --steps 50000 --output compiler/build/runs/NAME
"""
import argparse
import csv
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import tempfile

from paths import HERE, MACHINE, TRC, BUILD

CXX = ["clang++", "-O3", "-std=c++23", "-ffp-contract=off", "-fno-exceptions", "-fno-rtti", "-w"]
CC = ["cc", "-O3", "-std=c11", "-ffp-contract=off", "-w"]
SOURCE_EXT = (".c", ".cpp", ".h", ".hpp", ".inc")


def command(argv, **kwargs):
    env = dict(kwargs.pop("env", os.environ))
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    r = subprocess.run([str(x) for x in argv], capture_output=True, text=True, env=env, **kwargs)
    if r.returncode:
        raise RuntimeError(f"command failed: {' '.join(map(str, argv))}\n{r.stdout[-1000:]}\n{r.stderr[-5000:]}")
    return r.stdout


def local(path):
    path = Path(path).resolve()
    if not path.is_relative_to(HERE):
        raise ValueError("program/output paths must be inside compiler/")
    return path


@contextmanager
def cache_lock(key):
    """Advisory filesystem lock shared across threads and processes."""
    root = BUILD / 'locks'; root.mkdir(parents=True, exist_ok=True)
    path = root / (hashlib.sha256(str(key).encode()).hexdigest() + '.lock')
    with path.open('a') as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(handle, fcntl.LOCK_UN)


def atomic_json(path, value):
    path = Path(path); path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, prefix='.'+path.name,
                                     delete=False) as handle:
        temporary = Path(handle.name)
        json.dump(value, handle, indent=2); handle.write('\n')
        handle.flush(); os.fsync(handle.fileno())
    os.replace(temporary, path)


def _machine_key():
    h = hashlib.sha256()
    for p in sorted(MACHINE.iterdir()):
        if p.is_file():
            h.update(p.name.encode()); h.update(p.read_bytes())
    h.update(TRC.read_bytes())
    return h.digest()


def compile_tr(program):
    program = local(program)
    sources = [(program/n).resolve() for n in (program/'ORDER').read_text().splitlines() if n]
    if any(not p.is_relative_to(program) or p.suffix != '.tr' for p in sources):
        raise ValueError('invalid program ORDER')
    key = hashlib.sha256(b''.join(p.read_bytes() for p in sources) + _machine_key()).hexdigest()[:20]
    dest = BUILD/'images'/key
    with cache_lock('image:'+key):
        if not (dest/'program.trb').exists():
            dest.parent.mkdir(parents=True, exist_ok=True)
            temporary = Path(tempfile.mkdtemp(prefix='.'+key, dir=dest.parent))
            try:
                stats = command([sys.executable, TRC, *sources, '--interp', MACHINE,
                                 '-o', temporary/'program.gen.hpp', '--image', temporary/'program.trb'])
                (temporary/'stats.txt').write_text(stats)
                if dest.exists(): shutil.rmtree(dest)
                os.replace(temporary, dest)
            finally:
                if temporary.exists(): shutil.rmtree(temporary)
    return dest


def build(program, mode):
    if mode not in ('spec', 'interp'): raise ValueError('invalid build mode')
    image = compile_tr(program)
    fingerprint = hashlib.sha256((HERE/'evaluate.c').read_bytes() + (HERE/'monitor.h').read_bytes() +
                                 _machine_key() + Path(__file__).read_bytes()).hexdigest()[:16]
    dest = BUILD/'bin'/f"{mode}-{fingerprint}-{image.name if mode == 'spec' else 'shared'}"
    exe = dest/'evaluate'
    with cache_lock('binary:'+str(dest)):
        if exe.exists(): return exe, image
        dest.mkdir(parents=True, exist_ok=True)
        for src in MACHINE.iterdir():
            if src.is_file() and src.suffix in SOURCE_EXT: shutil.copyfile(src, dest/src.name)
        shutil.copyfile(image/'program.gen.hpp', dest/'program.gen.hpp')
        (dest/'tr_config.h').write_text('#define TR_SPEC 1\n' if mode == 'spec' else '/* interpreted */\n')
        command([*CXX, '-I', dest, '-c', dest/'full_policy.cpp', '-o', dest/'policy.o'])
        ev = BUILD/f'evaluator-{fingerprint}.o'
        with cache_lock('evaluator:'+fingerprint):
            if not ev.exists():
                temporary = ev.with_suffix('.tmp.o')
                command([*CC, '-I', MACHINE, '-I', HERE, '-c', HERE/'evaluate.c', '-o', temporary])
                os.replace(temporary, ev)
        temporary = dest/'evaluate.tmp'
        command(['clang++', ev, dest/'policy.o', '-o', temporary, '-lm'])
        os.replace(temporary, exe)
    return exe, image


def goal_data(program):
    from compiler import vectors
    goal=json.loads((program/'goal.json').read_text())
    v=vectors(goal)
    return str(len(v))+'\n'+''.join(' '.join(map(str,[floor,*need,*keep]))+'\n' for floor,need,keep in v)


def summarize(rows,horizon):
    n=len(rows);wins=[int(r['length']) for r in rows if int(r['success'])]
    return {'episodes':n,'successes':len(wins),'success_rate':len(wins)/n,
            'violations':sum(int(r['violation'])!=0 for r in rows),
            'timeouts':sum(int(r['length'])==horizon and not int(r['success']) and float(r['health'])>0 for r in rows),
            'deaths':sum(float(r['health'])<=0 for r in rows),
            'mean_progress':sum(int(r['completed'])/int(r['stages']) for r in rows)/n,
            'failure_penalized_steps':sum(int(r['length']) if int(r['success']) else horizon for r in rows)/n}


def run(program, mode, seed, episodes, steps, output, trace=False):
    program, output = local(program), local(output)
    if output.exists():
        raise ValueError("output directory exists; choose a new run directory")
    if not 1 <= episodes <= 100000 or not 1 <= steps <= 100000 or not 0 <= seed <= 2147483647:
        raise ValueError("invalid episode count, horizon or seed")
    exe, image = build(program, mode)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='.'+output.name, dir=output.parent))
    try:
        (temporary/'goal.txt').write_text(goal_data(program))
        env = dict(os.environ); env.pop('TR_PROGRAM', None)
        if mode == 'interp': env['TR_PROGRAM'] = str(image/'program.trb')
        args = [exe, '--goal', temporary/'goal.txt', '--seed', seed,
                '--episodes', episodes, '--steps', steps, '--csv', temporary/'rows.csv']
        if trace: args += ['--trace', temporary/'trace.csv']
        start = time.monotonic(); command(args, env=env)
        with (temporary/'rows.csv').open() as handle: rows = list(csv.DictReader(handle))
        if len(rows) != episodes: raise ValueError('evaluator returned wrong episode count')
        result = {'program': str(program.relative_to(HERE)), 'mode': mode, 'seed': seed, 'horizon': steps,
                  'seconds': time.monotonic()-start, **summarize(rows, steps)}
        atomic_json(temporary/'summary.json', result)
        os.replace(temporary, output)
    finally:
        if temporary.exists(): shutil.rmtree(temporary)
    return result


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("program", type=Path)
    ap.add_argument("--mode", choices=("spec", "interp"), default="spec")
    ap.add_argument("--build-only", action="store_true")
    ap.add_argument("--seed", type=int, default=1900000000)
    ap.add_argument("--episodes", type=int, default=8)
    ap.add_argument("--steps", type=int, default=50000)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--trace", action="store_true")
    args = ap.parse_args()
    try:
        if args.build_only:
            print(build(args.program, args.mode)[0])
        elif args.output:
            print(json.dumps(run(args.program, args.mode, args.seed, args.episodes, args.steps, args.output, args.trace), indent=2))
        else:
            ap.error("--output is required for evaluation")
    except (ValueError, RuntimeError) as exc:
        ap.error(str(exc))
