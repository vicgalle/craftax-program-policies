import json
from pathlib import Path
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE))
from audit import audit


class AuditTests(unittest.TestCase):
    def test_incumbent_and_reference_allowed(self):
        for name in ('incumbent.py', 'reference.py'):
            self.assertEqual(audit(HERE / name)['checked_goal_structures'], 16)

    def test_semantic_and_external_mutations_rejected(self):
        (HERE / 'build/tests').mkdir(parents=True, exist_ok=True)
        base = (HERE / 'incumbent.py').read_text()
        variants = [
            "import os\n" + base,
            base.replace('if(sq_done())return -1;', 'a.intent.stage++; if(sq_done())return -1;'),
            base.replace('if(sq_done())return -1;', 'def int sq_done(){return 1;}'),
            base.replace('"defs": defs', '"defs": defs + goal["name"]'),
            base.replace('ANCHOR = "rule r_strategic [build] {"', 'ANCHOR = "rule r_wrong [build] {"'),
        ]
        with tempfile.TemporaryDirectory(dir=HERE / 'build/tests') as temp:
            for i, text in enumerate(variants):
                path = Path(temp) / f'bad_{i}.py'
                path.write_text(text)
                with self.assertRaises(ValueError):
                    audit(path)


if __name__ == '__main__':
    unittest.main(verbosity=2)
