"""Analysis of the held-out goal-program study from the retained rows; runs no episodes.

    python3 compiler/report.py        (needs numpy and matplotlib)

Reads results/rows/<arm>_<task>.csv (48 test tasks x 32 paired worlds for Original,
Guards, Reference and the five Shared searches; the 12-task subset for Per-task),
the search records in synthesis/ and direct/, and their feedback rows in
results/feedback_rows/. Writes results/compare.json, results/compare.md and
results/figures/. Intervals use a crossed bootstrap of independent coding
searches, paired worlds and stratified tasks (10,000 draws, seed 20261024).
"""
from __future__ import annotations

import argparse
from collections import Counter
import csv
import hashlib
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
SHARED = tuple(f'shared_run_{i}' for i in range(1, 6))
CONTROLS = ('original', 'guards', 'reference')
STRATA = ('short', 'medium', 'long')
KINDS = ('navigation', 'single', 'mixed')
LABELS = {'shared': 'Shared search (mean of five)', 'original': 'Original',
          'guards': 'Guards only', 'reference': 'Reference',
          'direct': 'Per-task search'}
BOOTSTRAP_SEED = 20261024
DRAWS = 10000
TEST_SEED = 2020000000
HORIZON = 50000
FEEDBACK = 'results/feedback_rows'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + '\n')


def local(root, value):
    path = (Path(root) / value).resolve()
    if not path.is_relative_to(Path(root).resolve()):
        raise ValueError(f'path outside experiment: {value}')
    return path


def normalize_tasks(manifest):
    """Accept the task declaration, preserving its predeclared direct subset."""
    records = manifest['tasks'] if isinstance(manifest, dict) else manifest
    subset = set(manifest.get('direct_subset', [])) if isinstance(manifest, dict) else set()
    tasks = []
    for raw in records:
        if raw.get('split', 'test') != 'test':
            continue
        task = dict(raw)
        task['id'] = task.get('id', task.get('name'))
        task['stratum'] = task.get('stratum', task.get('length_stratum'))
        task['kind'] = {'single_resource': 'single', 'mixed_resource': 'mixed'}.get(task['kind'], task['kind'])
        task['direct_subset'] = task['id'] in subset or bool(task.get('direct_subset'))
        if not task['id'] or task['stratum'] not in STRATA or task['kind'] not in KINDS:
            raise ValueError('invalid task metadata')
        length = int(task['length'])
        if length not in {'short': (2, 3, 4), 'medium': (5, 6), 'long': (7, 8)}[task['stratum']]:
            raise ValueError(f'incorrect length stratum: {task["id"]}')
        task['length'] = length
        tasks.append(task)
    if len({t['id'] for t in tasks}) != len(tasks):
        raise ValueError('duplicate declared task')
    return tasks


def validate_design(tasks):
    counts = Counter((t['stratum'], t['kind']) for t in tasks)
    expected = {(s, k): n for s in STRATA for k, n in zip(KINDS, (6, 5, 5))}
    if counts != expected:
        raise ValueError(f'incomplete test declaration: {dict(counts)}')
    direct = Counter((t['stratum'], t['kind']) for t in tasks if t['direct_subset'])
    if direct != {(s, k): n for s in STRATA for k, n in zip(KINDS, (1, 1, 2))}:
        raise ValueError('direct subset must contain 1 navigation, 1 single, 2 mixed tasks per length stratum')


def validate_rows(rows, stages, episodes=32, horizon=HORIZON):
    """Check native-monitor semantics, including failures and milestone times."""
    if len(rows) != episodes or [int(r['episode']) for r in rows] != list(range(episodes)):
        raise ValueError('incomplete or unordered episode rows')
    worlds = [int(r['world_seed']) for r in rows]
    if len(set(worlds)) != episodes:
        raise ValueError('duplicate worlds')
    for r in rows:
        success, violation = int(r['success']), int(r['violation'])
        completed, count, length = int(r['completed']), int(r['stages']), int(r['length'])
        if success not in (0, 1) or violation not in (0, 1):
            raise ValueError('nonbinary monitor outcome')
        if count != stages or not 0 <= completed <= count or not 0 <= length <= horizon:
            raise ValueError('invalid milestone count or episode length')
        health = float(r['health'])
        if not math.isfinite(health):
            raise ValueError('nonfinite health')
        if success != int(completed == count) or success and (violation or health <= 0):
            raise ValueError('inconsistent success')
        times = [int(r[f'stage{j}_at']) for j in range(8)]
        if any(t < 0 or t > length for t in times[:completed]):
            raise ValueError('milestone time outside episode')
        if any(b <= a for a, b in zip(times[:completed], times[1:completed])):
            raise ValueError('non-strict milestone times')
        if any(t != -1 for t in times[completed:]):
            raise ValueError('unreached milestone has a time')
        if success and times[completed - 1] != length:
            raise ValueError('success did not terminate at final milestone')
        if not success and not violation and health > 0 and length != horizon:
            raise ValueError('unterminated incomplete episode')
    return worlds


def summarize(rows, horizon=HORIZON):
    n = len(rows)
    if not n:
        raise ValueError('empty row group')
    return {'episodes': n, 'successes': sum(int(r['success']) for r in rows),
            'success_rate': sum(int(r['success']) for r in rows) / n,
            'violations': sum(int(r['violation']) for r in rows),
            'deaths': sum(float(r['health']) <= 0 for r in rows),
            'timeouts': sum(not int(r['success']) and not int(r['violation']) and
                            float(r['health']) > 0 and int(r['length']) == horizon for r in rows),
            'mean_progress': sum(int(r['completed']) / int(r['stages']) for r in rows) / n,
            'failure_penalized_steps': sum(int(r['length']) if int(r['success']) else horizon for r in rows) / n,
            'environment_steps': sum(int(r['length']) for r in rows)}


def crossed_bootstrap(shared, baseline, tasks, draws=DRAWS, seed=BOOTSTRAP_SEED,
                      resample_runs=True, resample_worlds=True, resample_tasks=True):
    """Macro-task contrast on [run, task, world] and [task, world] arrays.

    Each draw takes ONE world sample shared by all tasks and runs, independent
    run samples, and task samples within each length-by-kind cell. Fixed cell
    sizes preserve the predeclared macro-task weighting. Optional axis switches
    provide variance diagnostics, not alternative primary analyses.
    """
    import numpy as np
    shared, baseline = np.asarray(shared, dtype=float), np.asarray(baseline, dtype=float)
    if shared.ndim != 3 or baseline.shape != shared.shape[1:] or len(tasks) != shared.shape[1]:
        raise ValueError('expected shared[run,task,world], baseline[task,world]')
    if not np.isfinite(shared).all() or not np.isfinite(baseline).all() or draws < 2:
        raise ValueError('invalid bootstrap data or draw count')
    nr, nt, nw = shared.shape
    if min(nr, nt, nw) < 1:
        raise ValueError('empty bootstrap axis')
    rng = np.random.default_rng(seed)
    run_counts = (rng.multinomial(nr, [1 / nr] * nr, size=draws) / nr if resample_runs
                  else np.full((draws, nr), 1 / nr))
    world_counts = (rng.multinomial(nw, [1 / nw] * nw, size=draws) / nw if resample_worlds
                    else np.full((draws, nw), 1 / nw))
    cells = {}
    for i, task in enumerate(tasks):
        cells.setdefault((task['stratum'], task['kind']), []).append(i)
    task_counts = np.zeros((draws, nt))
    for indices in cells.values():
        n = len(indices)
        task_counts[:, indices] = (rng.multinomial(n, [1 / n] * n, size=draws) / nt
                                  if resample_tasks else np.full((draws, n), 1 / nt))
    differences = shared - baseline[None, :, :]
    samples = np.empty(draws)
    # Batches bound peak memory while retaining a truly crossed product sample.
    for start in range(0, draws, 256):
        end = min(start + 256, draws)
        weighted = np.einsum('br,rtw->btw', run_counts[start:end], differences, optimize=True)
        samples[start:end] = np.einsum('btw,bt,bw->b', weighted, task_counts[start:end],
                                      world_counts[start:end], optimize=True)
    lo, hi = np.quantile(samples, (.025, .975))
    return {'mean': float(differences.mean()), 'bootstrap_95': [float(lo), float(hi)],
            'bootstrap_sd': float(samples.std(ddof=1)), 'draws': draws, 'seed': seed,
            'independent_searches': nr, 'tasks': nt, 'paired_worlds': nw,
            'resampled_axes': [name for name, flag in (('search', resample_runs),
                                ('world', resample_worlds), ('stratified_task', resample_tasks)) if flag]}


def load_rows(tasks, root, selected):
    """Held-out rows for every declared cell; checks monitor semantics and world pairing."""
    meta = {t['id']: t for t in tasks}
    cells = [(a, t['id']) for a in (*CONTROLS, *SHARED) for t in tasks]
    cells += [('direct', t['id']) for t in tasks if t['direct_subset']]
    if set(selected['direct']) != {t['id'] for t in tasks if t['direct_subset']}:
        raise ValueError('selected per-task emitters differ from the declared subset')
    data, worlds = {}, None
    for arm, goal in sorted(cells):
        with (root / 'results/rows' / f'{arm}_{goal}.csv').open() as stream:
            rows = list(csv.DictReader(stream))
        current = validate_rows(rows, meta[goal]['length'])
        if worlds is None:
            worlds = current
        if current != worlds:
            raise ValueError('test worlds are not paired across tasks, arms and searches')
        data.setdefault(arm, {})[goal] = rows
    return data, worlds


def cost_manifest(root, selected):
    """Training, selection and baseline feedback groups seen by each search."""
    def entry(mode, key):
        search = root / ('synthesis' if mode == 'shared' else 'direct') / (f'run_{key}' if mode == 'shared' else key)
        selection = json.loads((search / 'selection.json').read_text())
        train_dirs = []
        for number in (1, 2, 3):
            r = json.loads((search / f'round_{number}/result.json').read_text())
            train_dirs += [f"{FEEDBACK}/{r['label']}/{g}_synthesized" for g in sorted(r['goals'])]
        val_dirs = [FEEDBACK + p[len('runs'):] for c in selection['candidates'] for p in c['run_dirs']]
        base_label = 'baseline_shared' if mode == 'shared' else f'baseline_direct_{key}'
        base_dirs = [str(p.relative_to(root)) for label in (base_label, base_label + '_incumbent')
                     for p in sorted((root / FEEDBACK / label).iterdir()) if (p / 'rows.csv').exists()]
        wall = search / 'agent_run.json'
        wall = json.loads(wall.read_text()) if wall.exists() else {}
        return {'run' if mode == 'shared' else 'goal': key, 'training_run_dirs': train_dirs,
                'selection_run_dirs': val_dirs, 'baseline_feedback_run_dirs': base_dirs,
                'coding_agent_seconds': wall.get('elapsed_seconds'), 'model_tokens': wall.get('model_tokens'),
                'agent_record': str((search / 'agent_run.json').relative_to(root))}
    return {'shared': [entry('shared', i) for i in range(1, 6)],
            'direct': [entry('direct', g) for g in sorted(selected['direct'])]}


def milestone_diagnostics(rows):
    """Marginal reach probability and mutually exclusive failure-stage counts."""
    n = len(rows)
    reach, failures, reasons = {}, {}, {'violation': 0, 'death': 0, 'timeout': 0}
    for stage in range(1, 9):
        eligible = [r for r in rows if int(r['stages']) >= stage]
        if eligible:
            reach[str(stage)] = {'eligible_episodes': len(eligible),
                'reached': sum(int(r['completed']) >= stage for r in eligible),
                'reach_rate': sum(int(r['completed']) >= stage for r in eligible) / len(eligible)}
    for r in rows:
        if int(r['success']):
            continue
        stage = str(int(r['completed']) + 1)
        failures[stage] = failures.get(stage, 0) + 1
        reason = 'violation' if int(r['violation']) else 'death' if float(r['health']) <= 0 else 'timeout'
        reasons[reason] += 1
    return {'episodes': n, 'milestones_reached': reach, 'first_uncompleted_stage': failures,
            'failure_reason_exclusive': reasons,
            'note': 'Later milestones condition on tasks having that many stages; failure stage is the first uncompleted stage.'}


def synthesis_costs(cost_manifest, root):
    """Count observed training and selection rows; deduplicate physical feedback."""
    root = Path(root).resolve()
    seen_feedback = set()
    def count(directories, seen=None):
        total = {'episodes': 0, 'environment_steps': 0, 'evaluation_seconds': 0.0, 'run_dirs': []}
        for name in directories:
            path = local(root, name)
            if seen is not None and str(path) in seen:
                continue
            if seen is not None:
                seen.add(str(path))
            summary = json.loads((path / 'summary.json').read_text())
            with (path / 'rows.csv').open() as stream:
                rows = list(csv.DictReader(stream))
            if len(rows) != summary['episodes']:
                raise ValueError('incomplete synthesis cost rows')
            total['episodes'] += len(rows)
            total['environment_steps'] += sum(int(r['length']) for r in rows)
            total['evaluation_seconds'] += float(summary['seconds'])
            total['run_dirs'].append(str(path.relative_to(root)))
        return total
    result = {'shared': [], 'direct': []}
    for group in ('shared', 'direct'):
        for item in cost_manifest.get(group, []):
            result[group].append({k: v for k, v in item.items() if not k.endswith('_run_dirs')} | {
                'candidate_training': count(item.get('training_run_dirs', [])),
                'candidate_selection': count(item.get('selection_run_dirs', [])),
                'baseline_feedback_logical_exposure': count(item.get('baseline_feedback_run_dirs', [])),
                'baseline_feedback_physical_unique': count(item.get('baseline_feedback_run_dirs', []), seen_feedback),
                'coding_agent_seconds': item.get('coding_agent_seconds'), 'model_tokens': None})
            search_root = (root / 'synthesis' / f"run_{item['run']}" if group == 'shared'
                           else root / 'direct' / item['goal'])
            records = [search_root / f'round_{number}/result.json' for number in (1, 2, 3)]
            if all(path.exists() for path in records):
                trials = [json.loads(path.read_text()) for path in records]
                result[group][-1]['successful_trial_preflight_seconds'] = [r.get('preflight_seconds') for r in trials]
                result[group][-1]['candidate_round_wall_seconds'] = [r['wall_seconds'] for r in trials]
                result[group][-1]['timing_note'] = ('Elapsed preflight/round times include concurrent cache and lock waits; '
                    'preflight timings cover completed trials, not failed repair attempts separately. These are not CPU or model times.')
    diagnostic = root / 'diagnostics/resource_failure_evidence.json'
    if diagnostic.exists():
        result['common_development_diagnostics'] = {'path': str(diagnostic.relative_to(root)),
            **json.loads(diagnostic.read_text())['cost']}
    result.update(model_tokens=None, test_time_tuning_episodes=0,
        interpretation='Observed rollout counts include candidate training and candidate selection. Baseline feedback is separate; physical feedback is deduplicated across runs, logical exposure is per search. Model tokens are unavailable and coding wall times are not total compute. No equivalence or unconditional total-cost saving claim.')
    return result


def analyze_arrays(data, tasks, draws=DRAWS, seed=BOOTSTRAP_SEED):
    """Pure analysis entry point for loaded rows; does not access evaluation files."""
    import numpy as np
    names = [t['id'] for t in tasks]
    def matrix(arm, group, metric='success'):
        def value(row):
            return int(row['success']) if metric == 'success' else int(row['length']) if int(row['success']) else HORIZON
        return np.array([[value(r) for r in data[arm][t['id']]] for t in group])
    shared = np.stack([matrix(a, tasks) for a in SHARED])
    aggregate = {a: summarize([r for name in names if name in goals for r in goals[name]]) for a, goals in data.items()}
    aggregate['shared'] = summarize([r for a in SHARED for name in names for r in data[a][name]])
    aggregate['shared']['independent_searches'] = 5
    aggregate['shared']['success_interval'] = crossed_bootstrap(shared, np.zeros_like(shared[0]), tasks, draws, seed)
    comparisons = {}
    for control in ('guards', 'reference', 'original'):
        comparisons[f'shared_minus_{control}'] = crossed_bootstrap(shared, matrix(control, tasks), tasks, draws, seed)
        comparisons[f'shared_minus_{control}']['failure_penalized_steps'] = crossed_bootstrap(
            np.stack([matrix(a, tasks, 'steps') for a in SHARED]), matrix(control, tasks, 'steps'), tasks, draws, seed)
    comparisons['shared_minus_guards']['status'] = 'primary predeclared contrast'
    for key in comparisons:
        if key != 'shared_minus_guards':
            comparisons[key]['status'] = 'secondary descriptive interval'
    by_length, by_kind = {}, {}
    for field, levels, output in (('stratum', STRATA, by_length), ('kind', KINDS, by_kind)):
        for level in levels:
            group = [t for t in tasks if t[field] == level]
            idx = [names.index(t['id']) for t in group]
            stats = {a: summarize([r for t in group for r in data[a][t['id']]]) for a in (*CONTROLS, *SHARED)}
            stats['shared'] = summarize([r for a in SHARED for t in group for r in data[a][t['id']]])
            stats['shared_minus_guards'] = crossed_bootstrap(shared[:, idx], matrix('guards', group), group, draws, seed)
            stats['shared_minus_guards']['status'] = 'secondary descriptive interval'
            output[level] = stats
    subset = [t for t in tasks if t['direct_subset']]
    idx = [names.index(t['id']) for t in subset]
    comparisons['shared_minus_direct_subset'] = crossed_bootstrap(shared[:, idx], matrix('direct', subset), subset, draws, seed)
    comparisons['shared_minus_direct_subset']['failure_penalized_steps'] = crossed_bootstrap(
        np.stack([matrix(a, subset, 'steps') for a in SHARED]), matrix('direct', subset, 'steps'), subset, draws, seed)
    comparisons['shared_minus_direct_subset']['status'] = 'secondary descriptive interval; predeclared 12-task subset only'
    per_run = {}
    for i, arm in enumerate(SHARED):
        per_run[arm] = {'aggregate': aggregate[arm],
            'success_interval': crossed_bootstrap(shared[i:i+1], np.zeros_like(shared[i]), tasks, draws, seed, resample_runs=False),
            'minus_guards': crossed_bootstrap(shared[i:i+1], matrix('guards', tasks), tasks, draws, seed, resample_runs=False)}
    rates = [aggregate[a]['success_rate'] for a in SHARED]
    primary_baseline = matrix('guards', tasks)
    variation = {'search_success_rate_min': min(rates), 'search_success_rate_max': max(rates),
                 'search_success_rate_sample_sd': float(np.std(rates, ddof=1)),
                 'task_mean_success_rate_range': [float(shared.mean((0, 2)).min()), float(shared.mean((0, 2)).max())],
                 'world_mean_success_rate_range': [float(shared.mean((0, 1)).min()), float(shared.mean((0, 1)).max())],
                 'axis_only_bootstrap_sd': {axis: crossed_bootstrap(shared, primary_baseline, tasks, draws, seed,
                     resample_runs=axis == 'search', resample_tasks=axis == 'task', resample_worlds=axis == 'world')['bootstrap_sd']
                     for axis in ('search', 'task', 'world')},
                 'note': 'Axis-only resampling is descriptive and does not provide an additive variance decomposition.'}
    diagnostics = {a: milestone_diagnostics([r for g in goals.values() for r in g]) for a, goals in data.items()}
    diagnostics['shared'] = milestone_diagnostics([r for a in SHARED for g in data[a].values() for r in g])
    by_goal = {t['id']: {'metadata': t, **{a: summarize(goals[t['id']]) for a, goals in data.items() if t['id'] in goals}} for t in tasks}
    return {'aggregate': aggregate, 'per_run': per_run, 'comparisons': comparisons,
            'by_length': by_length, 'by_kind': by_kind, 'by_goal': by_goal,
            'variation': variation, 'milestone_diagnostics': diagnostics}


def make_figures(result, tasks, output):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    plt.rcParams.update({'font.size': 10, 'axes.spines.top': False, 'axes.spines.right': False,
                         'pdf.fonttype': 42, 'ps.fonttype': 42, 'savefig.dpi': 200})
    directory = output / 'figures'; directory.mkdir(exist_ok=True)
    def save(fig, name):
        for ext in ('pdf', 'png'):
            fig.savefig(directory / f'{name}.{ext}', bbox_inches='tight')
        plt.close(fig)
    for key, levels, labels, name in (('by_length', STRATA, ['Short (2–4)', 'Medium (5–6)', 'Long (7–8)'], 'success_by_length'),
            ('by_kind', KINDS, ['Navigation', 'Single resource', 'Mixed resources'], 'success_by_kind')):
        fig, ax = plt.subplots(figsize=(7, 4))
        x = np.arange(3)
        for arm, color in (('original', '0.65'), ('guards', '#777777'), ('reference', '#e69f00')):
            ax.plot(x, [100 * result[key][v][arm]['success_rate'] for v in levels], 'o-', label=LABELS[arm], color=color, linewidth=1.3)
        run_rates = np.array([[100 * result[key][v][arm]['success_rate'] for v in levels] for arm in SHARED])
        for i, rates in enumerate(run_rates):
            ax.plot(x + (i - 2) * .012, rates, 'o', color='#0072b2', alpha=.6, markersize=4)
        ax.plot(x, run_rates.mean(0), 'o-', color='#0072b2', linewidth=2, label='Shared mean; dots = five searches')
        ax.fill_between(x, run_rates.min(0), run_rates.max(0), color='#0072b2', alpha=.12, label='Shared search range')
        ax.set(xticks=x, xticklabels=labels, ylabel='Task completion (%)', ylim=(-2, 102))
        ax.legend(fontsize=8, loc='best'); ax.grid(axis='y', alpha=.2)
        save(fig, name)
    costs = result['costs']
    if len(costs['shared']) != 5 or len(costs['direct']) != 12:
        raise ValueError('cost figure requires all five shared and twelve direct search records')
    direct_map = {v['goal']: v for v in costs['direct']}
    if set(direct_map) != {t['id'] for t in tasks if t['direct_subset']}:
        raise ValueError('cost records differ from predeclared direct subset')
    for group, expected in (('shared', 192), ('direct', 24)):
        if any(item[phase]['episodes'] != expected for item in costs[group]
               for phase in ('candidate_training', 'candidate_selection')):
            raise ValueError('incomplete candidate training or selection cost records')
    subset = [t for t in tasks if t['direct_subset']]
    def feedback_cost(item, metric):
        return sum(item[phase][metric] for phase in ('candidate_training', 'candidate_selection',
                                                    'baseline_feedback_logical_exposure'))
    x = np.arange(1, 13)
    fig, (ax, steps_ax, quality) = plt.subplots(1, 3, figsize=(14, 4.6))
    for axis, metric, scale, label in ((ax, 'episodes', 1, 'Feedback episodes'),
                                      (steps_ax, 'environment_steps', 1e6, 'Feedback environment steps (millions)')):
        direct_cost = np.cumsum([feedback_cost(direct_map[t['id']], metric) for t in subset]) / scale
        shared_cost = [feedback_cost(item, metric) / scale for item in costs['shared']]
        axis.step(x, direct_cost, where='mid', color='#d55e00', label='Per-task searches: cumulative')
        for i, value in enumerate(shared_cost):
            axis.axhline(value, color='#0072b2', alpha=.35, label='Shared upfront: each search' if i == 0 else None)
        axis.set(xlabel='Tasks in predeclared subset order', ylabel=label, xticks=[1, 4, 8, 12])
        axis.legend(fontsize=8); axis.grid(axis='y', alpha=.2)
    shared_quality = [[100 * np.mean([result['by_goal'][t['id']][arm]['success_rate'] for t in subset[:n]]) for n in x] for arm in SHARED]
    for i, rates in enumerate(shared_quality):
        quality.plot(x, rates, color='#0072b2', alpha=.35, label='Shared: five searches' if i == 0 else None)
    quality.plot(x, [100 * np.mean([result['by_goal'][t['id']]['direct']['success_rate'] for t in subset[:n]]) for n in x],
                 color='#d55e00', label='Per-task searches')
    quality.set(xlabel='Same observed subset prefix', ylabel='Paired test completion (%)', ylim=(-2, 102), xticks=[1, 4, 8, 12])
    quality.legend(fontsize=8); quality.grid(axis='y', alpha=.2)
    fig.suptitle('Observed 12-task comparison: candidate, selection and baseline feedback; model tokens unavailable', fontsize=11)
    fig.text(.5, -.01, 'Each shared search is charged its full baseline exposure; old-task diagnostics are common development overhead.',
             ha='center', fontsize=8)
    fig.tight_layout(rect=(0, .065, 1, .93))
    save(fig, 'observed_subset_cost_and_quality')


def markdown(result):
    lines = ['# Composition transfer with independent coding searches', '',
        'Five independent shared searches, 48 predeclared test tasks and 32 common test worlds per task. '
        'Three fixed controls use the same worlds. Twelve predeclared tasks also have fresh per-task searches. '
        'The action horizon is 50,000.', '',
        '| Method | Success | Violations | Failure-penalized steps |', '|---|---:|---:|---:|']
    for arm in (*CONTROLS, 'shared', 'direct'):
        d = result['aggregate'][arm]
        label = LABELS[arm] + (' (12-task subset)' if arm == 'direct' else '')
        lines.append(f"| {label} | {100*d['success_rate']:.2f}% ({d['successes']}/{d['episodes']}) | {d['violations']} | {d['failure_penalized_steps']:.0f} |")
    lines += ['', 'Shared episode totals pool five searches for display; the estimand averages searches and tasks. '
              'The searches are the independent coding units.', '',
              '| Contrast | Difference (percentage points) | Crossed bootstrap 95% interval | Status |', '|---|---:|---:|---|']
    for name, d in result['comparisons'].items():
        lo, hi = d['bootstrap_95']
        lines.append(f"| {name.replace('_', ' ')} | {100*d['mean']:+.2f} | [{100*lo:+.2f}, {100*hi:+.2f}] | {d['status']} |")
    lines += ['', 'Each of 10,000 deterministic bootstrap draws resamples five coding searches, 32 paired worlds '
              'with one common world sample across every task and arm, and tasks within each length × resource-kind cell. '
              'This preserves paired outcomes and the 6/5/5 task composition within each length stratum. '
              'Secondary intervals are descriptive and are not adjusted for multiple comparisons.', '',
              '| Shared search | Success | Task/world bootstrap 95% interval |', '|---|---:|---:|']
    for arm, values in result['per_run'].items():
        d = values['success_interval']; lo, hi = d['bootstrap_95']
        lines.append(f"| {arm} | {100*d['mean']:.2f}% | [{100*lo:.2f}, {100*hi:.2f}] |")
    v = result['variation']
    lines += ['', f"Across independent searches, test success ranges from {100*v['search_success_rate_min']:.2f}% "
              f"to {100*v['search_success_rate_max']:.2f}% (sample SD {100*v['search_success_rate_sample_sd']:.2f} percentage points).", '',
              'Milestone reach rates, failure-stage counts, task tables, stratified results and axis-specific variation '
              'diagnostics appear in [compare.json](compare.json). Unreached stages retain time −1; a success must finish '
              'every milestone without a reserve violation. Figures retain all five search outcomes.', '',
              'Candidate costs count training and validation/selection episodes. Baseline feedback is reported separately '
              'as physical unique episodes and logical exposure for each search. The cost figure charges each search its '
              'full baseline exposure, covers only the observed 12-task subset, and shows test quality alongside episodes '
              'and environment steps. Old-task diagnostics are common development overhead. Model tokens are unavailable; these data '
              'do not establish equal quality or lower total synthesis cost.', '',
              'Tasks were declared without outcome-based feasibility filtering. Results measure this task generator, '
              'fixed monitor, controller interface and game implementation. Acquisition attains inventory thresholds; '
              'it need not collect new units after stage activation. Five searches provide limited evidence about '
              'coding-agent variability. Each direct task has one search; its comparison conditions on those selected '
              'direct controllers. Sequence length counts milestones, some of which can be satisfied by existing inventory. '
              'All lengths share a fixed horizon, so length curves also reflect greater action demand. '
              'The direct subset is resource-heavy and has three lower-length and one upper-length task per stratum; '
              'its comparisons apply to those twelve tasks. '
              'Bootstrap intervals do not establish equivalence or arbitrary-program generalization.', '',
              '[Success by length](figures/success_by_length.pdf) · [Success by resource kind](figures/success_by_kind.pdf) · '
              '[Observed subset cost and quality](figures/observed_subset_cost_and_quality.pdf) · [Raw rows](rows/)', '']
    return '\n'.join(lines)


def analyze(taskmeta, *, root=HERE, output=None, draws=DRAWS, seed=BOOTSTRAP_SEED):
    """Read-only analysis of the retained held-out rows and search records."""
    root = Path(root).resolve(); output = Path(output or root / 'results').resolve()
    tasks = normalize_tasks(taskmeta); validate_design(tasks)
    selected = json.loads((root / 'selected.json').read_text())
    output.mkdir(parents=True, exist_ok=True)
    data, worlds = load_rows(tasks, root, selected)
    result = analyze_arrays(data, tasks, draws, seed)
    result.update(world_seeds=worlds, costs=synthesis_costs(cost_manifest(root, selected), root),
                  bootstrap={'method': 'crossed search × paired world × stratified task', 'draws': draws, 'seed': seed,
                             'primary': 'shared_minus_guards', 'secondary_intervals': 'descriptive; no multiplicity adjustment'})
    make_figures(result, tasks, output)
    write_json(output / 'compare.json', result)
    (output / 'compare.md').write_text(markdown(result))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--output', type=Path, default=HERE / 'results')
    args = parser.parse_args()
    tasks = json.loads((HERE / 'task_manifest.json').read_text())
    result = analyze(tasks, output=args.output)
    primary = result['comparisons']['shared_minus_guards']
    print(json.dumps({'shared_success': result['aggregate']['shared']['success_rate'],
                      'guards_success': result['aggregate']['guards']['success_rate'],
                      'primary_shared_minus_guards': primary['mean'], 'bootstrap_95': primary['bootstrap_95'],
                      'report': str((args.output / 'compare.md').relative_to(HERE.parent)) if args.output.resolve().is_relative_to(HERE.parent) else str(args.output)}, indent=2))


if __name__ == '__main__':
    main()
