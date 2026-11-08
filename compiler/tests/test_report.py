"""Analysis tests use synthetic outcomes, never held-out game evaluation."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('composition_report', ROOT / 'report.py')
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class CrossedBootstrapTests(unittest.TestCase):
    def setUp(self):
        self.tasks = [{'id': f't{i}', 'stratum': 'short', 'kind': 'navigation'} for i in range(4)]
        self.zero = np.zeros((4, 8))

    def test_search_variability_is_an_independent_axis(self):
        # Every goal and world is identical within a search. Bootstrapping
        # individual rows would falsely treat these 160 rows as independent.
        values = np.broadcast_to(np.array([0, 0, 0, 1, 1])[:, None, None], (5, 4, 8))
        full = report.crossed_bootstrap(values, self.zero, self.tasks, draws=5000)
        fixed_search = report.crossed_bootstrap(values, self.zero, self.tasks, draws=5000, resample_runs=False)
        self.assertAlmostEqual(full['mean'], .4)
        self.assertGreater(full['bootstrap_sd'], .20)
        self.assertLess(full['bootstrap_sd'], .24)
        self.assertLess(fixed_search['bootstrap_sd'], 1e-12)
        self.assertLessEqual(full['bootstrap_95'][0], .01)
        self.assertGreaterEqual(full['bootstrap_95'][1], .79)

    def test_one_world_sample_is_shared_across_all_tasks_and_searches(self):
        values = np.broadcast_to(np.array([0, 0, 0, 0, 1, 1, 1, 1])[None, None, :], (5, 4, 8))
        result = report.crossed_bootstrap(values, self.zero, self.tasks, draws=5000)
        self.assertGreater(result['bootstrap_sd'], .16)
        self.assertLess(result['bootstrap_sd'], .19)
        # Exact pairing must cancel all uncertainty, including world clusters.
        paired = report.crossed_bootstrap(values, values[0], self.tasks, draws=1000)
        self.assertEqual(paired['mean'], 0)
        self.assertEqual(paired['bootstrap_95'], [0, 0])

    def test_stratification_preserves_declared_cell_weights(self):
        tasks = [dict(t, kind='navigation' if i < 3 else 'mixed') for i, t in enumerate(self.tasks)]
        values = np.broadcast_to(np.array([0, 0, 0, 1])[None, :, None], (5, 4, 8))
        result = report.crossed_bootstrap(values, self.zero, tasks, draws=1000)
        self.assertEqual(result['mean'], .25)
        self.assertEqual(result['bootstrap_95'], [.25, .25])
        unstratified = report.crossed_bootstrap(values, self.zero, self.tasks, draws=1000)
        self.assertGreater(unstratified['bootstrap_sd'], .2)

    def test_task_uncertainty_persists_when_searches_and_worlds_identical(self):
        values = np.broadcast_to(np.array([0, 0, 1, 1])[None, :, None], (5, 4, 8))
        full = report.crossed_bootstrap(values, self.zero, self.tasks, draws=4000)
        fixed_tasks = report.crossed_bootstrap(values, self.zero, self.tasks, draws=4000, resample_tasks=False)
        self.assertGreater(full['bootstrap_sd'], .23)
        self.assertLess(fixed_tasks['bootstrap_sd'], 1e-12)

    def test_reproducible_and_validates_dimensions(self):
        values = np.ones((5, 4, 8))
        self.assertEqual(report.crossed_bootstrap(values, self.zero, self.tasks, draws=100),
                         report.crossed_bootstrap(values, self.zero, self.tasks, draws=100))
        with self.assertRaises(ValueError):
            report.crossed_bootstrap(values, np.zeros((4, 9)), self.tasks)


class IntegrityTests(unittest.TestCase):
    def good(self):
        return {'episode': '0', 'world_seed': '123', 'success': '1', 'violation': '0',
                'completed': '2', 'stages': '2', 'length': '7', 'health': '8',
                **{f'stage{j}_at': str([0, 7, -1, -1, -1, -1, -1, -1][j]) for j in range(8)}}

    def test_success_requires_full_progress_without_violation(self):
        self.assertEqual(report.validate_rows([self.good()], 2, episodes=1), [123])
        for changes in ({'completed': '1'}, {'violation': '1'}, {'health': '0'}, {'stages': '3'}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                report.validate_rows([self.good() | changes], 2, episodes=1)

    def test_strict_order_sentinel_and_final_time(self):
        for changes in ({'stage1_at': '0'}, {'stage1_at': '8'}, {'stage2_at': '7'}, {'stage1_at': '6'}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                report.validate_rows([self.good() | changes], 2, episodes=1)

    def test_incomplete_rows_require_terminal_reason(self):
        fail = self.good() | {'success': '0', 'completed': '1', 'stage1_at': '-1'}
        with self.assertRaises(ValueError):
            report.validate_rows([fail], 2, episodes=1)
        report.validate_rows([fail | {'health': '0'}], 2, episodes=1)
        report.validate_rows([fail | {'length': str(report.HORIZON)}], 2, episodes=1)
        report.validate_rows([fail | {'violation': '1'}], 2, episodes=1)

    def test_failure_stage_and_milestone_denominators(self):
        success = self.good()
        death = success | {'success': '0', 'completed': '1', 'stage1_at': '-1', 'health': '0'}
        violation = death | {'violation': '1', 'health': '5'}
        d = report.milestone_diagnostics([success, death, violation])
        self.assertEqual(d['milestones_reached']['1']['reach_rate'], 1)
        self.assertAlmostEqual(d['milestones_reached']['2']['reach_rate'], 1 / 3)
        self.assertEqual(d['first_uncompleted_stage'], {'2': 2})
        self.assertEqual(d['failure_reason_exclusive'], {'death': 1, 'violation': 1, 'timeout': 0})

    def test_costs_include_selection_and_deduplicate_feedback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name, lengths in [('train', [4, 5]), ('selection', [6]), ('feedback', [2, 3])]:
                path = root / name; path.mkdir()
                with (path / 'rows.csv').open('w') as stream:
                    writer = csv.DictWriter(stream, ['length']); writer.writeheader()
                    writer.writerows({'length': n} for n in lengths)
                (path / 'summary.json').write_text(json.dumps({'rows_sha256': report.digest(path / 'rows.csv'),
                    'episodes': len(lengths), 'seconds': 1.5}))
            item = {'training_run_dirs': ['train'], 'selection_run_dirs': ['selection'],
                    'baseline_feedback_run_dirs': ['feedback']}
            costs = report.synthesis_costs({'shared': [item | {'run': 1}, item | {'run': 2}]}, root)
            a, b = costs['shared']
            self.assertEqual(a['candidate_training']['episodes'], 2)
            self.assertEqual(a['candidate_selection']['environment_steps'], 6)
            self.assertEqual(a['baseline_feedback_physical_unique']['episodes'], 2)
            self.assertEqual(b['baseline_feedback_physical_unique']['episodes'], 0)
            self.assertEqual(b['baseline_feedback_logical_exposure']['episodes'], 2)
            self.assertIsNone(costs['model_tokens'])
            (root / 'train/rows.csv').write_text('length\n999\n')
            with self.assertRaises(ValueError):
                report.synthesis_costs({'shared': [item]}, root)


if __name__ == '__main__':
    unittest.main()
