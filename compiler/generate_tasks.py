"""Deterministic, outcome-independent task declaration. Reachability is not certified.

Regenerates goals/{train,validation,test}/*.json and task_manifest.json (seed
20261024). Sequences equal to 13 earlier pilot programs, listed in
goals/excluded_previous_sequences.json, are excluded.

    python3 compiler/generate_tasks.py
"""
import argparse
import hashlib
import json
from itertools import combinations
from pathlib import Path
import random

from compiler import parse_goal
from paths import HERE

DEFAULT_SEED = 20261024
KINDS = ('navigation', 'single', 'mixed')
PAIRS = (('wood', 'iron'), ('wood', 'stone'), ('coal', 'iron'))
RESOURCES = ('wood', 'stone', 'coal', 'iron')
SPECS = (('train', 'short', 16, (6, 5, 5), (2, 3, 4)),
         ('validation', 'short', 8, (2, 3, 3), (2, 3, 4)),
         ('test', 'short', 16, (6, 5, 5), (2, 3, 4)),
         ('test', 'medium', 16, (6, 5, 5), (5, 6)),
         ('test', 'long', 16, (6, 5, 5), (7, 8)))


def canonical_sequence(goal):
    """Canonical parsed semantics, ignoring name, zero entries and omitted maintain."""
    return json.dumps(parse_goal(goal)['sequence'], sort_keys=True, separators=(',', ':'))


def sequence_hash(goal):
    return hashlib.sha256(canonical_sequence(goal).encode()).hexdigest()


def previous_sequences():
    data = json.loads((HERE/'goals'/'excluded_previous_sequences.json').read_text())
    return {canonical_sequence({'name': 'previous', 'sequence': seq}) for seq in data['sequences']}


def _sequence(rng, length, kind):
    if kind == 'navigation':
        seq = []
        last = 0
        for _ in range(length):
            floor = rng.choice([f for f in range(4) if f != last])
            seq.append({'reach': floor}); last = floor
        return seq
    resources = (rng.choice(RESOURCES),) if kind == 'single' else rng.choice(PAIRS)
    # Sample delivery slots before any outcomes exist. Acquisitions may precede,
    # follow or separate travel milestones, with no redundant adjacent delivery.
    n_acquire = rng.randint(1, (length+1)//2)
    slots = rng.choice([positions for positions in combinations(range(length), n_acquire)
                        if all(b-a > 1 for a, b in zip(positions, positions[1:]))])
    seq = []; reserves = {}; last_floor = 0
    for i in range(length):
        if i in slots:
            need = {}; keep = {}
            for resource in resources:
                low, high = (6, 14) if resource == 'wood' else (2, 6)
                need[resource] = rng.randint(max(low, reserves.get(resource, 0)), high)
                keep_low, keep_high = (2, 8) if resource == 'wood' else (1, 3)
                keep[resource] = rng.randint(keep_low, min(keep_high, need[resource]))
                reserves[resource] = max(reserves.get(resource, 0), keep[resource])
            # Every later acquire explicitly contains every active reserve.
            seq.append({'acquire': need, 'maintain': keep})
        else:
            floor = rng.choice([f for f in range(4) if f != last_floor])
            seq.append({'reach': floor}); last_floor = floor
    return seq


def generate(seed=DEFAULT_SEED):
    rng = random.Random(seed); seen = previous_sequences()
    goals = {split: [] for split in ('train', 'validation', 'test')}
    records = []
    for split, stratum, count, counts, lengths in SPECS:
        for kind, quota in zip(KINDS, counts):
            for index in range(quota):
                # Prior tasks leave only four distinct two-stage navigation
                # sequences on floors 0..3, used by train and short test.
                offset = 1 if split == 'validation' and kind == 'navigation' else 0
                length = lengths[(index+offset) % len(lengths)]
                for attempt in range(100000):
                    raw = {'name': 'temporary', 'sequence': _sequence(rng, length, kind)}
                    canonical = canonical_sequence(raw)
                    if canonical not in seen:
                        break
                else:
                    raise RuntimeError('task uniqueness space exhausted')
                seen.add(canonical); sha = sequence_hash(raw)
                raw['name'] = 't_' + sha[:16]
                goals[split].append(raw)
                records.append({'id': raw['name'], 'path': f'goals/{split}/{raw["name"]}.json',
                                'split': split, 'stratum': stratum, 'kind': kind,
                                'length': length, 'sequence_sha256': sha})
    # Fixed declaration order chooses 1 navigation, 1 single, 2 mixed per stratum.
    direct = []
    for stratum in ('short', 'medium', 'long'):
        for kind, count in (('navigation', 1), ('single', 1), ('mixed', 2)):
            direct.extend(r['id'] for r in [r for r in records
                          if r['split'] == 'test' and r['stratum'] == stratum and r['kind'] == kind][:count])
    manifest = {'version': 1, 'seed': seed, 'reachability_certified': False,
                'generation': 'outcome-independent; no evaluation-based filtering',
                'reserve_semantics': 'cumulative per-resource maximum, as in fixed monitor',
                'excluded_previous_sequences': len(previous_sequences()),
                'resource_pairs': [list(pair) for pair in PAIRS],
                'tasks': records, 'direct_subset': direct}
    return goals, manifest


def write_tasks(seed=DEFAULT_SEED, root=HERE):
    goals, manifest = generate(seed); root = Path(root)
    expected = {r['path'] for r in manifest['tasks']}
    existing = {str(p.relative_to(root)) for p in (root/'goals').glob('*/*.json')}
    if existing - expected:
        raise ValueError('destination contains a different task declaration')
    for split, tasks in goals.items():
        for goal in tasks:
            path = root/'goals'/split/(goal['name']+'.json'); path.parent.mkdir(parents=True, exist_ok=True)
            content = json.dumps(goal, indent=2)+'\n'
            if path.exists() and path.read_text() != content:
                raise ValueError(f'existing task differs: {path}')
            path.write_text(content)
    path = root/'task_manifest.json'; content = json.dumps(manifest, indent=2)+'\n'
    if path.exists() and path.read_text() != content:
        raise ValueError('existing task manifest differs')
    path.write_text(content)
    return manifest


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__); ap.add_argument('--seed', type=int, default=DEFAULT_SEED)
    args = ap.parse_args(); manifest = write_tasks(args.seed)
    print(json.dumps({'seed': args.seed, 'tasks': len(manifest['tasks']), 'direct_subset': manifest['direct_subset']}))
