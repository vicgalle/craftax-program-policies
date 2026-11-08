"""Evaluate one candidate emitter of a search on its training feedback (three per search).

A search lives in a directory ROOT holding candidate1.py..candidate3.py. Each
call audits the candidate, compiles it for every task (syntax repairs are allowed
until this preflight passes), archives it as ROOT/round_K/source.py, evaluates it
and writes ROOT/round_K/result.json; after candidate 3 it writes ROOT/closed.json.
Candidates must be evaluated in order and are immutable once evaluated. Selection
on separate worlds (selection.py) is never shown to the search agent.

    search.py shared --run N --candidate K --controller ROOT/candidateK.py [--root ROOT]
    search.py direct --goal goals/test/TASK.json --candidate K --controller ROOT/candidateK.py [--root ROOT]

ROOT defaults to build/search/shared_run_N or build/search/TASK. The archived
searches of the study are synthesis/run_N (shared) and direct/TASK (per-task).
"""
import argparse
import ast
import csv
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import time

from paths import HERE, BUILD, digest
from compiler import link, parse_goal
from audit import audit
from run import atomic_json, build, cache_lock, compile_tr, local
from study import evaluate

SHARED_SEED = 2000000000
DIRECT_SEED = 2030000000


def check_emitter(path):
    """Keep candidate modules declarative apart from their pure emitter helpers."""
    tree = ast.parse(Path(path).read_text())
    allowed_imports = {'re', 'math', 'itertools', 'functools', 'collections', 'json'}
    found = False
    for node in tree.body:
        if isinstance(node, (ast.Import, ast.ImportFrom)):
            names = [n.name.split('.')[0] for n in node.names] if isinstance(node, ast.Import) else [node.module or '']
            if any(name not in allowed_imports for name in names) or getattr(node, 'level', 0):
                raise ValueError('candidate imports must be pure standard-library helpers')
        elif isinstance(node, ast.FunctionDef):
            if node.decorator_list: raise ValueError('candidate decorators are not permitted')
            found |= node.name == 'emit'
        elif isinstance(node, (ast.Assign, ast.AnnAssign)):
            try: ast.literal_eval(node.value)
            except (ValueError, TypeError): raise ValueError('candidate top-level values must be literals')
        elif not (isinstance(node, ast.Expr) and isinstance(node.value, ast.Constant) and isinstance(node.value.value, str)):
            raise ValueError('candidate module must contain only constants, imports and emitter functions')
    if not found: raise ValueError('candidate must define emit(goal)')
    for node in ast.walk(tree):
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in {
                'open', 'exec', 'eval', 'compile', '__import__', 'input', 'print', 'breakpoint', 'globals', 'locals', 'getattr', 'setattr'}:
            raise ValueError('candidate emitter may not perform I/O or dynamic execution')
        if isinstance(node, ast.Attribute) and node.attr.startswith('__'):
            raise ValueError('candidate emitter may not inspect Python internals')
    return tree


def _validate_prior(root, candidate):
    records = []
    for number in range(1, candidate):
        path = root/f'round_{number}'/'result.json'
        if not path.exists(): raise ValueError('complete candidates in order 1, 2, 3')
        record = json.loads(path.read_text())
        if digest(HERE/record['authored_source']) != record['source_sha256']:
            raise ValueError('an earlier authored candidate changed after its trial')
        if digest(HERE/record['archived_source']) != record['source_sha256']:
            raise ValueError('an earlier archived candidate changed after its trial')
        records.append(record)
    return records



def preflight(controller, paths, workers=10):
    """Compile every task without executing any world; failed source is revisable."""
    sha = digest(controller)
    def prepare(path):
        goal = parse_goal(json.loads(path.read_text()))
        program = BUILD/'preflight'/sha/goal['name']
        with cache_lock('preflight:'+str(program)):
            if not program.exists(): link(goal, program, 'synthesized', controller)
            cert = json.loads((program/'plan.json').read_text())
            if cert['goal'] != goal:
                raise ValueError('preflight program differs from candidate')
        compile_tr(program)
        build(program, 'spec')
    with ThreadPoolExecutor(max_workers=min(4, workers)) as pool:
        for future in as_completed([pool.submit(prepare, path) for path in paths]): future.result()
    if digest(controller) != sha: raise ValueError('candidate changed during preflight')
    return sha


def trial(mode, controller, candidate, run_number=1, goal=None, workers=5, steps=50000, root=None):
    if mode not in ('shared', 'direct') or candidate not in (1,2,3) or run_number < 1:
        raise ValueError('invalid search mode, candidate or run')
    if steps != 50000: raise ValueError('the declared search horizon is exactly 50000')
    if not 1 <= workers <= 64: raise ValueError('workers must be 1..64')
    controller = local(controller)
    if mode == 'shared':
        root = local(root) if root else BUILD/'search'/f'shared_run_{run_number}'
        paths = sorted((HERE/'goals/train').glob('*.json'))
        if len(paths) != 16: raise ValueError('shared search requires the fixed 16 training tasks')
        seed, episodes = SHARED_SEED, 4
        label = f'search_shared_r{run_number}_c{candidate}'
    else:
        goal = local(goal)
        manifest = json.loads((HERE/'task_manifest.json').read_text())
        raw = json.loads(goal.read_text()); name = raw['name']
        if goal.parent != HERE/'goals/test' or name not in manifest['direct_subset']:
            raise ValueError('direct search requires a declared direct-subset task')
        root = local(root) if root else BUILD/'search'/name; paths = [goal]
        seed, episodes = DIRECT_SEED, 8
        label = f'search_direct_{name}_c{candidate}'
    if root in (HERE/'synthesis'/f'run_{run_number}', HERE/'direct'/str(name if mode == 'direct' else '')):
        raise ValueError('the archived searches are read-only; use a new --root')
    if controller != root/f'candidate{candidate}.py':
        raise ValueError(f'candidate source must be {root/f"candidate{candidate}.py"}')
    with cache_lock('search:'+str(root)):
        prior = _validate_prior(root, candidate)
        start = time.monotonic()
        archive = root/f'round_{candidate}'/'source.py'
        if archive.exists() and digest(archive) != digest(controller):
            raise ValueError('candidate is immutable after a trial starts')
        check_emitter(controller)
        audit_result = audit(controller, [json.loads(p.read_text()) for p in paths])
        preflight_start = time.monotonic()
        sha = preflight(controller, paths, workers)
        preflight_seconds = time.monotonic()-preflight_start
        archive = root/f'round_{candidate}'/'source.py'; archive.parent.mkdir(parents=True, exist_ok=True)
        if archive.exists():
            if digest(archive) != sha: raise ValueError('candidate is immutable after a trial starts')
        else:
            temporary = archive.with_suffix('.tmp'); temporary.write_bytes(controller.read_bytes()); os.replace(temporary, archive)
        results = evaluate(paths, ['synthesized'], label, seed, episodes, workers, archive, steps)
        if digest(controller) != sha or digest(archive) != sha: raise ValueError('candidate changed during trial')
        per_goal = {name: arms['synthesized'] for name, arms in results.items()}
        total_steps = 0
        for name in per_goal:
            with (BUILD/'runs'/label/f'{name}_synthesized'/'rows.csv').open() as handle:
                total_steps += sum(int(row['length']) for row in csv.DictReader(handle))
        record = {'mode': mode, 'run': run_number if mode == 'shared' else None,
                  'candidate': candidate, 'label': label, 'seed': seed, 'episodes_per_goal': episodes,
                  'horizon': steps, 'authored_source': str(controller.relative_to(HERE)),
                  'archived_source': str(archive.relative_to(HERE)), 'source_sha256': sha,
                  'audit': audit_result,
                  'tasks': {str(p.relative_to(HERE)): digest(p) for p in paths},
                  'goals': per_goal, 'total_episodes': len(paths)*episodes,
                  'successes': sum(r['successes'] for r in per_goal.values()),
                  'failure_penalized_steps': sum(r['failure_penalized_steps'] for r in per_goal.values())/len(paths),
                  'actual_steps': total_steps, 'evaluation_seconds': sum(r['seconds'] for r in per_goal.values()),
                  'preflight_seconds': preflight_seconds, 'wall_seconds': time.monotonic()-start}
        result_path = archive.parent/'result.json'
        if result_path.exists():
            old = json.loads(result_path.read_text())
            if {k:v for k,v in old.items() if k not in ('wall_seconds', 'preflight_seconds')} != {k:v for k,v in record.items() if k not in ('wall_seconds', 'preflight_seconds')}:
                raise ValueError('completed trial record differs on resume')
            record = old
        else: atomic_json(result_path, record)
        if candidate == 3:
            closed = {'rounds': [{ 'candidate': n, 'source_sha256': r['source_sha256'],
                                 'result_sha256': digest(root/f'round_{n}'/'result.json')}
                                for n, r in enumerate([*prior, record], 1)],
                      'training_seed': seed, 'episodes_per_goal': episodes, 'horizon': steps,
                      'tasks': record['tasks'], 'search_closed': True,
                      'validation_feedback_used': False}
            closed_path = root/'closed.json'
            if closed_path.exists() and json.loads(closed_path.read_text()) != closed:
                raise ValueError('search closure changed')
            atomic_json(closed_path, closed)
        return record


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__); ap.add_argument('mode', choices=('shared','direct'))
    ap.add_argument('--controller', type=Path, required=True); ap.add_argument('--candidate', type=int, choices=(1,2,3), required=True)
    ap.add_argument('--run', type=int, default=1); ap.add_argument('--goal', type=Path)
    ap.add_argument('--workers', type=int, default=5); ap.add_argument('--steps', type=int, default=50000)
    ap.add_argument('--root', type=Path, help='search directory (default build/search/...)')
    args = ap.parse_args()
    if args.mode == 'direct' and args.goal is None: ap.error('--goal is required for direct search')
    try:
        r = trial(args.mode, args.controller, args.candidate, args.run, args.goal, args.workers, args.steps, args.root)
        print(json.dumps({k:v for k,v in r.items() if k != 'goals'}, indent=2))
    except (ValueError, RuntimeError) as exc: ap.error(str(exc))
