"""Select one of a closed search's three candidates on separate worlds, without feedback to the agent.

Shared searches are selected on the 8 validation tasks x 8 worlds (seed 2010000000);
per-task searches on their own task x 8 other worlds (seed 2040000000). Rule: most
successes, then fewest failure-penalized steps, then the earlier candidate. Writes
ROOT/selection.json and ROOT/selected.py.

    selection.py shared --run N [--root ROOT]      (ROOT default build/search/shared_run_N)
    selection.py direct --goal TASK [--root ROOT]  (ROOT default build/search/TASK)
"""
import argparse
import json
from pathlib import Path

from audit import audit
from paths import HERE, BUILD, digest
from run import atomic_json, local
from study import evaluate


def select(mode, run_number=None, goal_id=None, workers=5, root=None):
    if mode == 'shared':
        if not run_number or run_number < 1:
            raise ValueError('shared run must be a positive integer')
        root = local(root) if root else BUILD / 'search' / f'shared_run_{run_number}'
        paths = sorted((HERE / 'goals/validation').glob('*.json'))
        seed, episodes = 2010000000, 8
        arm = f'shared_run_{run_number}'
        labels = [f'search_validation_r{run_number}_c{i}' for i in (1, 2, 3)]
    elif mode == 'direct':
        meta = json.loads((HERE / 'task_manifest.json').read_text())
        if goal_id not in meta['direct_subset']:
            raise ValueError('unknown direct task')
        root = local(root) if root else BUILD / 'search' / goal_id
        paths = [HERE / 'goals/test' / f'{goal_id}.json']
        seed, episodes = 2040000000, 8
        arm = goal_id
        labels = [f'search_direct_validation_{goal_id}_c{i}' for i in (1, 2, 3)]
    else:
        raise ValueError('invalid selection mode')
    if root in (HERE / 'synthesis' / f'run_{run_number}', HERE / 'direct' / str(goal_id)):
        raise ValueError('the archived searches are read-only; use a new --root')
    closed = json.loads((root / 'closed.json').read_text())
    if not closed.get('search_closed') or closed.get('validation_feedback_used'):
        raise ValueError('search must be closed without validation feedback')
    if [r['candidate'] for r in closed['rounds']] != [1, 2, 3]:
        raise ValueError('three closed candidate rounds required')
    candidates = []
    goals = [json.loads(p.read_text()) for p in paths]
    for number, label in enumerate(labels, 1):
        source = root / f'round_{number}/source.py'
        record_path = root / f'round_{number}/result.json'
        record = json.loads(record_path.read_text())
        declared = closed['rounds'][number - 1]
        if (digest(source) != declared['source_sha256'] or
                digest(record_path) != declared['result_sha256'] or
                digest(HERE / record['authored_source']) != declared['source_sha256']):
            raise ValueError('closed search source/results changed')
        audit(source, goals)
        results = evaluate(paths, ['synthesized'], label, seed, episodes,
                           workers=workers, controller=source, steps=50000)
        summaries = [r['synthesized'] for r in results.values()]
        candidates.append({'candidate': number, 'controller_path': str(source.relative_to(HERE)),
                           'sha256': digest(source), 'label': label,
                           'successes': sum(r['successes'] for r in summaries),
                           'episodes': sum(r['episodes'] for r in summaries),
                           'failure_penalized_steps': sum(r['failure_penalized_steps'] for r in summaries) / len(summaries),
                           'run_dirs': [f'build/runs/{label}/{g}_synthesized' for g in sorted(results)]})
    chosen = min(candidates, key=lambda c: (-c['successes'], c['failure_penalized_steps'], c['candidate']))
    selected = root / 'selected.py'
    source = HERE / chosen['controller_path']
    if selected.exists() and selected.read_bytes() != source.read_bytes():
        raise ValueError('previous selection differs')
    selected.write_bytes(source.read_bytes())
    record = {'mode': mode, 'key': arm, 'closed_sha256': digest(root / 'closed.json'),
              'selection_seed': seed, 'selection_worlds': episodes,
              'candidates': candidates, 'selected_candidate': chosen['candidate'],
              'controller_path': str(selected.relative_to(HERE)), 'sha256': digest(selected),
              'rule': 'max successes, min failure-penalized steps, earlier candidate',
              'feedback_returned_to_search_agent': False}
    record_path = root / 'selection.json'
    if record_path.exists() and json.loads(record_path.read_text()) != record:
        raise ValueError('selection record changed')
    atomic_json(record_path, record)
    return record


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('mode', choices=('shared', 'direct'))
    ap.add_argument('--run', type=int)
    ap.add_argument('--goal')
    ap.add_argument('--workers', type=int, default=5)
    ap.add_argument('--root', type=Path, help='search directory (default build/search/...)')
    args = ap.parse_args()
    result = select(args.mode, args.run, args.goal, args.workers, args.root)
    print(json.dumps({k: v for k, v in result.items() if k != 'candidates'}, indent=2))
