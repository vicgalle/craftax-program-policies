"""Link, build and evaluate tasks x arms concurrently (always the specialized build).

Programs go to build/generated/LABEL/<task>_<arm>, runs to build/runs/LABEL/<task>_<arm>
(rows.csv + summary.json) and a summary of the group to build/results/LABEL.json.
A completed run is reused when its configuration matches.

    python3 compiler/study.py --split train --arm guards --label my_label --seed 2000000000 --episodes 4
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import json
import os
from pathlib import Path
import re

from compiler import ARMS, link, parse_goal
from paths import HERE, BUILD
from run import atomic_json, build, cache_lock, compile_tr, run


def _check_run(result, out, seed, episodes, steps):
    if (result.get('seed'), result.get('episodes'), result.get('horizon'), result.get('mode')) != (seed, episodes, steps, 'spec'):
        raise ValueError(f'existing run has another configuration: {out}')
    with (out/'rows.csv').open() as handle:
        if sum(1 for _ in csv.DictReader(handle)) != episodes:
            raise ValueError(f'wrong raw episode count: {out}')


def evaluate(paths, arms, label, seed, episodes, workers=5, controller=None, steps=50000):
    if not re.fullmatch('[a-zA-Z0-9_]+', label): raise ValueError('invalid label')
    if not 1 <= workers <= 64: raise ValueError('workers must be 1..64')
    if not 1 <= episodes <= 100000 or not 1 <= steps <= 100000 or not 0 <= seed <= 2147483647:
        raise ValueError('invalid episode count, horizon or seed')
    paths = sorted(Path(path).resolve() for path in paths); arms = list(arms)
    if not paths or not arms or len(set(arms)) != len(arms) or any(a not in ARMS for a in arms):
        raise ValueError('need nonempty tasks and distinct valid arms')
    if 'synthesized' in arms and controller is None:
        raise ValueError('the synthesized arm needs an emitter (controller)')
    controller = Path(controller).resolve() if controller else None
    jobs = []; names = set(); unique = {}
    for path in paths:
        goal = parse_goal(json.loads(path.read_text()))
        if goal['name'] in names: raise ValueError('duplicate task names')
        names.add(goal['name'])
        for arm in arms:
            program = BUILD/'generated'/label/f"{goal['name']}_{arm}"
            expected = os.path.relpath(controller, HERE) if arm == 'synthesized' else 'reference.py' if arm == 'reference' else None
            with cache_lock('program:'+str(program)):
                if not program.exists(): link(goal, program, arm, controller if arm == 'synthesized' else None)
                plan = json.loads((program/'plan.json').read_text())
                if plan['goal'] != goal or plan['arm'] != arm or plan['controller'] != expected:
                    raise ValueError(f'existing generated program differs: {program}')
            image = compile_tr(program); unique[image.name] = program
            jobs.append((goal['name'], arm, program))
    with ThreadPoolExecutor(max_workers=min(4, workers)) as pool:
        for future in as_completed([pool.submit(build, p, 'spec') for p in unique.values()]): future.result()
    print(json.dumps({'prepared': label, 'programs': len(jobs), 'distinct_images': len(unique)}), flush=True)
    results = {}
    def task(job):
        name, arm, program = job; out = BUILD/'runs'/label/f'{name}_{arm}'
        with cache_lock('run:'+str(out)):
            if (out/'summary.json').exists(): result = json.loads((out/'summary.json').read_text())
            else: result = run(program, 'spec', seed, episodes, steps, out)
            _check_run(result, out, seed, episodes, steps)
        print(json.dumps({'goal': name, 'arm': arm, **result}), flush=True)
        return name, arm, result
    with ThreadPoolExecutor(max_workers=workers) as pool:
        for future in as_completed([pool.submit(task, job) for job in jobs]):
            name, arm, result = future.result(); results.setdefault(name, {})[arm] = result
    results = dict(sorted(results.items()))
    with cache_lock('results:'+label): atomic_json(BUILD/'results'/f'{label}.json', results)
    return results


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--split', choices=('train', 'validation', 'test'), default='train')
    ap.add_argument('--arm', choices=ARMS, action='append'); ap.add_argument('--label', required=True)
    ap.add_argument('--seed', type=int, required=True); ap.add_argument('--episodes', type=int, required=True)
    ap.add_argument('--workers', type=int, default=5); ap.add_argument('--steps', type=int, default=50000)
    ap.add_argument('--controller', type=Path); args = ap.parse_args()
    try:
        evaluate(sorted((HERE/'goals'/args.split).glob('*.json')), args.arm or ['original','guards','reference'],
                 args.label, args.seed, args.episodes, args.workers, args.controller, args.steps)
    except (ValueError, RuntimeError) as exc: ap.error(str(exc))
