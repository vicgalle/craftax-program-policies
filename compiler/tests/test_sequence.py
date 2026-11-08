import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

HERE=Path(__file__).resolve().parents[1];sys.path.insert(0,str(HERE))
from compiler import link,parse_goal,RESOURCES
from paths import digest,MACHINE,TRC
from run import CC,CXX,command,compile_tr

class SequenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (HERE/'build/tests').mkdir(parents=True,exist_ok=True)
        cls.tmp=tempfile.TemporaryDirectory(dir=HERE/'build/tests');cls.d=Path(cls.tmp.name)
        cls.goal=parse_goal({'name':'fixture','sequence':[{'reach':2},{'acquire':{r:5 for r in RESOURCES},'maintain':{r:5 for r in RESOURCES}},{'reach':0}]})
        arms=['original','guards','reference','synthesized']
        cls.programs={a:link(cls.goal,cls.d/a,a,HERE/'incumbent.py' if a=='synthesized' else None) for a in arms}
        cls.images={a:compile_tr(p) for a,p in cls.programs.items()}
    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()
    def test_original_image_unchanged(self):
        self.assertEqual(digest(self.images['original']/'program.trb'),'48594d878b2cc090ca1cfb34a9db3b8e109da84b0eb8b4a1499f5afed78e11b0')
    def test_shared_hard_constraints(self):
        expected=(self.programs['guards']/'sequence.tr').read_bytes()
        for arm in self.programs:
            if arm!='original':self.assertEqual((self.programs[arm]/'sequence.tr').read_bytes(),expected)
        self.assertNotIn('sr_sequence',(self.programs['guards']/'floor8.tr').read_text())
    def test_monitor(self):
        exe=self.d/'monitor';command([*CC,'-I',HERE,HERE/'tests/test_monitor.c','-o',exe]);self.assertIn('PASS',command([exe]))
    def test_invalid_goals(self):
        bad=[{}, {'name':'x','sequence':[]},{'name':'x','sequence':[{'reach':True}]},
             {'name':'x','sequence':[{'reach':9}]},{'name':'x','sequence':[{'acquire':{'wood':2},'maintain':{'wood':3}}]},
             {'name':'x','sequence':[{'acquire':{'water':2}}]},{'name':'x','sequence':[{'acquire':{}}]},
             {'name':'x','sequence':[{'reach':0}]*9}]
        for raw in bad:
            with self.assertRaises(ValueError):parse_goal(raw)
    def test_output_isolation(self):
        for p in (HERE.parent/'factorization/new',HERE/'goals/new',HERE,self.d/'original'):
            with self.assertRaises(ValueError):link(self.goal,p,'guards')
    def test_actual_tr_state_and_engine_costs(self):
        costs_exe=self.d/'costs';command([*CC,'-I',HERE,HERE/'tests/engine_costs.c','-o',costs_exe,'-lm'])
        costs=self.d/'costs.txt';costs.write_text(command([costs_exe]))
        spec=importlib.util.spec_from_file_location('fixture_trc',TRC);trc=importlib.util.module_from_spec(spec);spec.loader.exec_module(trc)
        for arm in self.programs:
            if arm=='original':continue
            d=self.d/('host_'+arm);d.mkdir()
            for p in MACHINE.iterdir():
                if p.is_file():shutil.copyfile(p,d/p.name)
            shutil.copyfile(self.images[arm]/'program.gen.hpp',d/'program.gen.hpp');(d/'tr_config.h').write_text('/* interpreted */\n')
            program=self.programs[arm]
            lw=trc.compile_file([str(program/n) for n in (program/'ORDER').read_text().splitlines()],str(MACHINE))
            names=('sq_observe','sq_done','sq_reserve')
            (d/'def_ids.h').write_text('\n'.join(f"#define TEST_{n.upper()} {lw.defs[n]['id']}" for n in names)+'\n')
            exe=d/'test';command([*CXX,'-I',d,HERE/'tests/sequence_state.cpp','-o',exe,'-lm'])
            self.assertIn('PASS',command([exe,costs],env={'TR_PROGRAM':str(self.images[arm]/'program.trb')}))

if __name__=='__main__':unittest.main(verbosity=2)
