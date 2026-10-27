"""User-run continuation protocol checks with controlled external executables.

Authored first; no author-side execution, including syntax checks.
"""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parent))
import test_circuit_bitflip_hardware_pipeline as fixtures

protocol=fixtures.protocol
batch=fixtures.batch

class ContinuationPipelineTests(unittest.TestCase):
    def fixture(self,root):
        data,source,fixed=fixtures.CircuitHardwareTests().fixture(root)
        code=fixed.read_text()
        code=code.replace('import sys\n','import sys\ncontinuous="--continuous" in sys.argv\nif continuous:sys.argv.remove("--continuous")\n')
        code=code.replace('out.mkdir()','out.mkdir()\n(out/"continuous_seen.txt").write_text(str(continuous))\nwith (Path(__file__).parent/"events.txt").open("a") as event:event.write("fixed,"+str(image)+"\\n")')
        code=code.replace('table("search_stats.csv",["contexts_built","context_budget_exhausted"],[dict(contexts_built=2,context_budget_exhausted=1)])',
                          'table("search_stats.csv",["contexts_built","context_budget_exhausted","continuous","completed_sweeps","stopped_for_convergence"],[dict(contexts_built=2,context_budget_exhausted=0,continuous=int(continuous),completed_sweeps=2,stopped_for_convergence=int(continuous))])')
        fixed.write_text(code)
        discovery=root/'discovery'
        discovery.write_text('''#!/usr/bin/env python3
import csv,json,sys
from pathlib import Path
if '--sensitivity-order' in sys.argv:sys.argv.remove('--sensitivity-order')
application=sys.argv[1];out=Path(sys.argv[3]);opts=dict(zip(sys.argv[4::2],sys.argv[5::2]));image=int(opts['--image']);out.mkdir()
with (Path(__file__).parent/'events.txt').open('a') as event:event.write('discovery,'+str(image)+'\\n')
def table(name,fields,rows):
 with (out/name).open('w',newline='') as stream:
  writer=csv.DictWriter(stream,fields);writer.writeheader();writer.writerows(rows)
numeric=not((Path(__file__).parent/'no_pilots').exists() and image<=2)
summaries=[dict(application=application,image=image,candidate_id='baseline',status='reference',global_mse=20,roi_mse=20)]
configs=[]
if numeric:
 summaries.append(dict(application=application,image=image,candidate_id='n2_rank1',status='numeric_eligible',global_mse=60,roi_mse=200))
 configs=[dict(candidate_id='n2_rank1',node_id=n,monitor='input1',bit=1,lower=image,upper=image+1) for n in (9,10)]
table('summary.csv',list(summaries[0]),summaries)
table('configurations.csv',['candidate_id','node_id','monitor','bit','lower','upper'],configs)
(out/'RUN.txt').write_text('completed=1\\napplication='+application+'\\nimage='+str(image)+'\\n')
(out/'arguments.json').write_text(json.dumps(opts))
''',encoding='utf-8');discovery.chmod(0o755)
        return data,source,fixed,discovery

    def args(self,data,fixed,discovery,out):
        return [str(data),str(out),'--skip-build','--fixed-exe',str(fixed),'--discovery-exe',str(discovery),
                '--workflow','continuation','--train','1:3','--screen','4:5','--test','6:7','--screen-workers','1','--max-hardware','0']

    def test_current_train_calibration_precedes_local_discovery_and_native_calls_are_single(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,fixed,discovery=self.fixture(root);out=root/'trial'
            self.assertEqual(protocol.main(self.args(data,fixed,discovery,out),application='sobel'),0)
            events=(root/'events.txt').read_text().splitlines()
            self.assertLess(events.index('fixed,3'),events.index('discovery,3'))
            opts=json.loads((out/'training/image_03/arguments.json').read_text())
            self.assertEqual(opts['--structure-radius'],'1')
            self.assertEqual(opts['--exploration-contexts'],'16')
            seeds=fixtures.read(Path(opts['--structure-seeds']))
            self.assertTrue(seeds)
            self.assertEqual({int(r['lower']) for r in seeds},{3},'Local discovery must use current-image calibrated intervals')
            attempts=fixtures.read(out/'interval_attempts.csv')
            identities={(r['phase'],r['hardware_id'],r['image']) for r in attempts}
            self.assertEqual(len(identities),len(attempts),'One native invocation per continuous interval task')
            self.assertTrue(all(r['completed_sweeps']=='2' and r['continuous']=='1' for r in attempts))
            self.assertTrue(all(path.read_text()=='True' for path in out.glob('test/*/image_*/continuous_seen.txt')))
            frozen=fixtures.read(out/'frozen_seeds.csv')
            self.assertTrue(all(r['seed_id'].startswith(('train_01_','train_02_','train_03_')) for r in frozen))
            self.assertTrue(json.loads((out/'RUN.json').read_text())['seed_bank_unchanged_after_test'])
            before=(root/'events.txt').read_text();repeat=root/'resumed'
            self.assertEqual(protocol.main(self.args(data,fixed,discovery,repeat)+['--resume-from',str(out)],application='sobel'),0)
            self.assertEqual((root/'events.txt').read_text(),before,'Identical local seed CONTENT must reuse tasks despite a new output path')
            statuses=fixtures.read(repeat/'run_status.csv')
            self.assertTrue(all(r['cache_hit']=='1' for r in statuses if r['status']=='completed'))

    def test_empty_prior_pool_uses_bounded_global_discovery(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,fixed,discovery=self.fixture(root);(root/'no_pilots').write_text('empty')
            out=root/'fallback'
            self.assertEqual(protocol.main(self.args(data,fixed,discovery,out),application='sobel'),0)
            opts=json.loads((out/'training/image_03/arguments.json').read_text())
            self.assertNotIn('--structure-seeds',opts)
            self.assertEqual(opts['--max-contexts'],'64')

    def test_continuation_reuse_is_policy_specific_and_wrapper_forwards_flags(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,fixed,discovery=self.fixture(root);out=root/'first'
            args=fixtures.CircuitHardwareTests().argv(data,source,fixed,out)+['--workflow','continuation','--test','5:6']
            self.assertEqual(protocol.main(args,application='sobel'),0)
            second=root/'second';args[1]=str(second)
            self.assertEqual(protocol.main(args+['--resume-from',str(out)],application='sobel'),0)
            rows=fixtures.read(second/'run_status.csv')
            self.assertTrue(all(r['cache_hit']=='1' for r in rows if r['status']=='completed'))
            third=root/'balanced';args[1]=str(third);args[args.index('--workflow')+1]='balanced'
            self.assertEqual(protocol.main(args+['--resume-from',str(out)],application='sobel'),0)
            self.assertEqual(json.loads((third/'RUN.json').read_text())['cache_hits'],0)
            parsed=protocol.protocol_parser(root,'sobel').parse_args(args)
            protocol.protocol_splits(parsed)
            child=batch.child_arguments(parsed,'sobel',root/'child',fixed,discovery)
            self.assertIn('--structure-radius',child)
            self.assertIn('--global-exploration-fraction',child)

    def test_continuation_failure_blocks_freeze_and_sharpen_mode_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,fixed,discovery=self.fixture(root);out=root/'failed'
            (root/'fail_screen').write_text('fail')
            args=fixtures.CircuitHardwareTests().argv(data,source,fixed,out)+['--workflow','continuation']
            self.assertEqual(protocol.main(args,application='sobel'),1)
            self.assertFalse((out/'frozen_hardware.csv').exists())
            args[1]=str(root/'sharpen')
            self.assertEqual(protocol.main(args,application='sharpen'),1)
            self.assertFalse((root/'sharpen').exists())

if __name__=='__main__':unittest.main()
