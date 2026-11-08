"""Execution checks of the goal-program study.

    validate.py parity [--workers 5]
        28 cases (Guards, Reference and the starter emitter on one navigation and one
        mixed training task; each selected Shared emitter on the same two tasks; each
        Per-task emitter on its task), one world each with a 50,000-action horizon:
        the interpreted I(p_g) and the specialized mix(I, p_g) must produce identical
        rows and action hashes, and both must equal the retained results/parity_rows.
    validate.py controls
        Negative control from the retained held-out rows: on the 18 navigation test
        tasks (no reserves), Original and Guards must give identical rows apart from
        the policy's own progress field (576 paired episodes).
    validate.py audit
        Static/interface audit of every selected emitter.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import json

from audit import audit
from compiler import link
from paths import HERE, BUILD
from run import run


def read(path):
    with path.open() as f:
        return list(csv.DictReader(f))


def parity_cases():
    manifest = json.loads((HERE / 'task_manifest.json').read_text())
    selection = json.loads((HERE / 'selected.json').read_text())
    training = {}
    for kind in ('navigation', 'mixed'):
        item = next(t for t in manifest['tasks'] if t['split'] == 'train' and t['kind'] == kind)
        training[kind] = HERE / item['path']
    cases = []
    for arm in ('guards', 'reference', 'incumbent'):
        for kind, goal in training.items():
            cases.append((f'{arm}_{kind}', goal, 'synthesized' if arm == 'incumbent' else arm,
                          HERE / 'incumbent.py' if arm == 'incumbent' else None, 2000000000))
    for arm, selected in sorted(selection['shared'].items()):
        for kind, goal in training.items():
            cases.append((f'{arm}_{kind}', goal, 'synthesized', HERE / selected['controller_path'], 2000000000))
    for goal, selected in sorted(selection['direct'].items()):
        cases.append((f'direct_{goal}', HERE / 'goals/test' / f'{goal}.json', 'synthesized',
                      HERE / selected['controller_path'], 2030000000))
    return cases


def parity(workers=5):
    def check(case):
        name, goal_path, arm, controller, seed = case
        goal = json.loads(goal_path.read_text())
        if controller:
            audit(controller, [goal])
        program = BUILD / 'generated/validation_parity' / name
        if not program.exists():
            link(goal, program, arm, controller)
        modes = {}
        for mode in ('spec', 'interp'):
            output = BUILD / 'runs/validation_parity' / f'{name}_{mode}'
            if not output.exists():
                run(program, mode, seed, 1, 50000, output)
            modes[mode] = read(output / 'rows.csv')
        retained = {mode: read(HERE / 'results/parity_rows' / f'{name}_{mode}' / 'rows.csv') for mode in modes}
        result = {'case': name, 'goal': goal['name'], 'seed': seed,
                  'matched_decisions': sum(int(r['length']) for r in modes['spec']),
                  'interp_equals_spec': modes['spec'] == modes['interp'],
                  'equals_retained_rows': modes == retained}
        print(json.dumps(result), flush=True)
        return result
    records = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        for future in as_completed([pool.submit(check, case) for case in parity_cases()]):
            records.append(future.result())
    ok = all(r['interp_equals_spec'] and r['equals_retained_rows'] for r in records)
    summary = {'cases': len(records), 'matched_decisions': sum(r['matched_decisions'] for r in records),
               'all_interp_equals_spec': all(r['interp_equals_spec'] for r in records),
               'all_equal_retained_rows': all(r['equals_retained_rows'] for r in records)}
    print(json.dumps(summary))
    return ok


def controls():
    tasks = json.loads((HERE / 'task_manifest.json').read_text())['tasks']
    pairs = 0
    for task in tasks:
        if task['split'] != 'test' or task['kind'] != 'navigation':
            continue
        rows = {arm: [{k: v for k, v in r.items() if k != 'policy_stage'}
                      for r in read(HERE / 'results/rows' / f'{arm}_{task["id"]}.csv')]
                for arm in ('original', 'guards')}
        if rows['original'] != rows['guards']:
            print(f'mismatch: {task["id"]}')
            return False
        pairs += len(rows['original'])
    print(json.dumps({'navigation_tasks_checked': 18, 'identical_original_guards_pairs': pairs}))
    return pairs == 576


def audits():
    selected = json.loads((HERE / 'selected.json').read_text())
    n = 0
    for condition in ('shared', 'direct'):
        for key, item in selected[condition].items():
            goals = None if condition == 'shared' else [json.loads((HERE / 'goals/test' / f'{key}.json').read_text())]
            audit(HERE / item['controller_path'], goals)
            n += 1
    print(json.dumps({'selected_emitters_audited': n}))
    return True


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=('parity', 'controls', 'audit'))
    ap.add_argument('--workers', type=int, default=5)
    args = ap.parse_args()
    ok = {'parity': lambda: parity(args.workers), 'controls': controls, 'audit': audits}[args.command]()
    raise SystemExit(0 if ok else 1)
