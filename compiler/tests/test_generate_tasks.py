"""Declaration tests; these never execute controllers or evaluate worlds."""
import collections
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from compiler import parse_goal
from generate_tasks import DEFAULT_SEED, PAIRS, SPECS, canonical_sequence, generate, previous_sequences, sequence_hash, write_tasks


class GeneratorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.goals, cls.manifest = generate()

    def test_exact_distribution(self):
        self.assertEqual({k:len(v) for k,v in self.goals.items()}, {'train':16, 'validation':8, 'test':48})
        records = self.manifest['tasks']
        for split, stratum, size, counts, lengths in SPECS:
            rows = [r for r in records if r['split'] == split and r['stratum'] == stratum]
            self.assertEqual(len(rows), size)
            self.assertEqual(collections.Counter(r['kind'] for r in rows),
                             dict(zip(('navigation','single','mixed'), counts)))
            self.assertTrue(all(r['length'] in lengths for r in rows))
        self.assertFalse(self.manifest['reachability_certified'])

    def test_syntax_semantics_and_cumulative_reserves(self):
        lookup = {g['name']:g for goals in self.goals.values() for g in goals}
        for row in self.manifest['tasks']:
            goal = lookup[row['id']]; parsed = parse_goal(goal)
            self.assertRegex(goal['name'], r'^t_[0-9a-f]{16}$')
            self.assertEqual(sequence_hash(goal), row['sequence_sha256'])
            reaches = [s['reach'] for s in parsed['sequence'] if 'reach' in s]
            self.assertTrue(all(0 <= f <= 3 for f in reaches)); self.assertNotEqual(reaches[0], 0)
            for a,b in zip(parsed['sequence'], parsed['sequence'][1:]):
                if 'reach' in a and 'reach' in b: self.assertNotEqual(a['reach'], b['reach'])
            acquisitions = [s for s in parsed['sequence'] if 'acquire' in s]
            if row['kind'] == 'navigation':
                self.assertGreaterEqual(len(reaches), 2); self.assertFalse(acquisitions)
                continue
            self.assertTrue(acquisitions); self.assertTrue(reaches)
            resources = {r for s in acquisitions for r,n in s['acquire'].items() if n}
            self.assertNotIn('diamond', resources)
            if row['kind'] == 'single': self.assertEqual(len(resources), 1)
            else:
                self.assertIn(tuple(sorted(resources)), [tuple(sorted(p)) for p in PAIRS])
                maintained = {r for s in acquisitions for r,n in s['maintain'].items() if n}
                self.assertEqual(resources, maintained)
            reserves = {}
            for stage in acquisitions:
                for resource, reserve in reserves.items(): self.assertGreaterEqual(stage['acquire'][resource], reserve)
                for resource in resources:
                    need = stage['acquire'][resource]; keep = stage['maintain'][resource]
                    low, high = (6,14) if resource == 'wood' else (2,6)
                    klow, khigh = (2,8) if resource == 'wood' else (1,3)
                    self.assertTrue(low <= need <= high); self.assertTrue(klow <= keep <= min(khigh, need))
                    reserves[resource] = max(reserves.get(resource,0), keep)

    def test_uniqueness_and_previous_exclusion(self):
        canonical = [canonical_sequence(g) for goals in self.goals.values() for g in goals]
        self.assertEqual(len(canonical), len(set(canonical)))
        self.assertTrue(set(canonical).isdisjoint(previous_sequences()))
        g = self.goals['train'][0]; renamed = {**g, 'name':'renamed'}
        self.assertEqual(canonical_sequence(g), canonical_sequence(renamed))
        self.assertEqual(len({g['name'] for goals in self.goals.values() for g in goals}), 72)

    def test_determinism_and_emission(self):
        self.assertEqual(generate(DEFAULT_SEED), (self.goals, self.manifest))
        self.assertNotEqual(generate(DEFAULT_SEED+1), (self.goals, self.manifest))
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); self.assertEqual(write_tasks(root=root), self.manifest)
            original = {str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*.json')}
            write_tasks(root=root)
            self.assertEqual(original, {str(p.relative_to(root)):p.read_bytes() for p in root.rglob('*.json')})
            self.assertEqual(len(original), 73)
            self.assertEqual(json.loads((root/'task_manifest.json').read_text()), self.manifest)

    def test_direct_subset_and_position_variety(self):
        subset = self.manifest['direct_subset']; self.assertEqual(len(subset),12)
        self.assertEqual(len(set(subset)),12)
        records = {r['id']:r for r in self.manifest['tasks']}
        for stratum in ('short','medium','long'):
            chosen = [records[name] for name in subset if records[name]['stratum'] == stratum]
            self.assertTrue(all(r['split'] == 'test' for r in chosen))
            self.assertEqual(collections.Counter(r['kind'] for r in chosen), {'navigation':1,'single':1,'mixed':2})
        for split in ('train','validation','test'):
            resource = [g for g in self.goals[split] if any('acquire' in s for s in g['sequence'])]
            self.assertTrue(any('acquire' in g['sequence'][0] for g in resource))
            self.assertTrue(any('reach' in g['sequence'][0] for g in resource))


if __name__ == '__main__': unittest.main(verbosity=2)
