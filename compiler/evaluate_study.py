"""Run the study's fixed evaluations and compare them with the retained rows.

    evaluate_study.py test [--arms ...] [--tasks ...] [--episodes 32] [--workers 5] [--check]
        Held-out evaluation: 48 test tasks x 32 worlds (seed 2020000000, horizon 50,000)
        for Original, Guards, Reference, the five selected Shared emitters
        (shared_run_1..5) and, on the 12-task subset, the Per-task emitters (direct).
        --arms/--tasks/--episodes select a subset (the first N worlds of the cohort);
        --check compares every produced row with results/rows exactly.
    evaluate_study.py baseline-shared | baseline-direct --goal TASK
        The control feedback shown to the coding agents before their searches
        (training worlds, or the tuning worlds of one per-task search), including
        the starter emitter (incumbent.py) that every search started from.
"""
import argparse
import csv
import json

from paths import HERE, BUILD
from study import evaluate

TEST_SEED, WORLDS, HORIZON = 2020000000, 32, 50000
CONTROLS = ('original', 'guards', 'reference')
SHARED = tuple(f'shared_run_{i}' for i in range(1, 6))


def baseline(mode, goal_id=None, workers=5):
    if mode == 'shared':
        paths = sorted((HERE / 'goals/train').glob('*.json'))
        label, seed, episodes = 'baseline_shared', 2000000000, 4
    else:
        meta = json.loads((HERE / 'task_manifest.json').read_text())
        if goal_id not in meta['direct_subset']:
            raise ValueError('unknown direct-subset task')
        paths = [HERE / 'goals/test' / f'{goal_id}.json']
        label, seed, episodes = f'baseline_direct_{goal_id}', 2030000000, 8
    evaluate(paths, list(CONTROLS), label, seed, episodes, workers)
    evaluate(paths, ['synthesized'], label + '_incumbent', seed, episodes, workers, HERE / 'incumbent.py')
    return label


def cells(arms=None, tasks=None):
    """(arm, label, underlying arm, task paths, emitter) for every held-out evaluation group."""
    chosen = json.loads((HERE / 'selected.json').read_text())
    paths = sorted((HERE / 'goals/test').glob('*.json'))
    if tasks:
        paths = [p for p in paths if p.stem in tasks]
    out = []
    for arm in CONTROLS:
        out.append((arm, f'test_{arm}', arm, paths, None))
    for arm, item in sorted(chosen['shared'].items()):
        out.append((arm, f'test_{arm}', 'synthesized', paths, HERE / item['controller_path']))
    for goal, item in sorted(chosen['direct'].items()):
        sub = [p for p in paths if p.stem == goal]
        out.append(('direct', f'test_direct_{goal}', 'synthesized', sub, HERE / item['controller_path']))
    return [c for c in out if c[3] and (not arms or c[0] in arms)]


def compare(arm, label, underlying, paths, episodes):
    mismatched = 0
    for path in paths:
        produced = BUILD / 'runs' / label / f'{path.stem}_{underlying}' / 'rows.csv'
        retained = HERE / 'results/rows' / f'{arm}_{path.stem}.csv'
        with produced.open() as a, retained.open() as b:
            new, old = list(csv.DictReader(a)), list(csv.DictReader(b))[:episodes]
        same = new == old
        mismatched += not same
        print(json.dumps({'arm': arm, 'task': path.stem, 'episodes': len(new), 'identical_to_retained_rows': same}), flush=True)
    return mismatched


def heldout(arms=None, tasks=None, episodes=WORLDS, workers=5, check=False):
    mismatched = 0
    for arm, label, underlying, paths, emitter in cells(arms, tasks):
        if episodes != WORLDS:
            label += f'_first{episodes}'
        evaluate(paths, [underlying], label, TEST_SEED, episodes, workers, emitter, HORIZON)
        if check:
            mismatched += compare(arm, label, underlying, paths, episodes)
    if check:
        print('ALL ROWS IDENTICAL' if not mismatched else f'{mismatched} TASK GROUPS DIFFER')
    return mismatched


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('command', choices=('baseline-shared', 'baseline-direct', 'test'))
    ap.add_argument('--goal')
    ap.add_argument('--arms', nargs='*', help='original guards reference shared_run_1..5 direct')
    ap.add_argument('--tasks', nargs='*', help='test task ids, e.g. t_d48da6015806f6ea')
    ap.add_argument('--episodes', type=int, default=WORLDS)
    ap.add_argument('--workers', type=int, default=5)
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    if args.command == 'test':
        raise SystemExit(1 if heldout(args.arms, args.tasks, args.episodes, args.workers, args.check) else 0)
    print(baseline('shared' if args.command == 'baseline-shared' else 'direct', args.goal, args.workers))
