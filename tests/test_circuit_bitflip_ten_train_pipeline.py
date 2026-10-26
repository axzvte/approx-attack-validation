"""User-run ten-image protocol checks; controlled external solvers only.

Tests authored before the runner, unrun by the author per AGENTS.md.
"""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parent))
import test_circuit_bitflip_hardware_pipeline as fixtures
scripts=Path(__file__).resolve().parents[1]/'scripts';sys.path.insert(0,str(scripts))
spec=importlib.util.spec_from_file_location('ten_train_runner',scripts/'run_circuit_bitflip_ten_train.py')
runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)

class TenTrainPipelineTests(unittest.TestCase):
    def fixture(self,root):
        data,_,fixed=fixtures.CircuitHardwareTests().fixture(root)
        code=fixed.read_text().replace('import sys\n','import sys\nif "--continuous" in sys.argv:sys.argv.remove("--continuous")\n')
        code=code.replace('table("search_stats.csv",["contexts_built","context_budget_exhausted"],[dict(contexts_built=2,context_budget_exhausted=1)])',
                          'table("search_stats.csv",["contexts_built","context_budget_exhausted","continuous","completed_sweeps","stopped_for_convergence"],[dict(contexts_built=2,context_budget_exhausted=0,continuous=1,completed_sweeps=2,stopped_for_convergence=1)])')
        fixed.write_text(code)
        train=root/'train_solver'
        train.write_text('''#!/usr/bin/env python3
import csv,math,sys
from pathlib import Path
app=sys.argv[1];out=Path(sys.argv[3]);opts=dict(zip(sys.argv[4::2],sys.argv[5::2]))
if (Path(__file__).parent/'fail_train').exists():sys.exit(7)
first,last=map(int,opts['--train'].split(':'));images=list(range(first,last+1));out.mkdir()
def table(name,fields,rows):
 with (out/name).open('w',newline='') as f:
  writer=csv.DictWriter(f,fields);writer.writeheader();writer.writerows(rows)
base=[dict(application=app,image=i,global_mse=20,roi_mse=20,non_roi_mse=20) for i in images]
h=[dict(hardware_id='h0001',node_id=n,monitor='input1',bit=1) for n in (0,10)]
es=[dict(application=app,hardware_id='h0001',image=i,status='numeric_eligible',global_mse=60,roi_mse=200,non_roi_mse=40,baseline_roi_mse=20,roi_drop_score=10,elapsed_seconds=.1,contexts_built=2,full_evaluations=0,completed_sweeps=2,stopped_for_convergence=1,reference_cache_hit=1,baseline_cache_hit=1) for i in images]
for row in base+es:
 for region in ('global','roi','non_roi'):row[region+'_psnr']=10*math.log10(65025/row[region+'_mse'])
schedule=opts['--scheduling'];reserve=int(opts['--completion-reserve'])
order=[images[i] for i in (0,9,4,2,7,1,8,3,6,5)] if schedule=='interleaved' else images
diag=dict(scheduling=schedule,initial_batch=opts['--initial-batch'],completion_reserve=reserve,effective_completion_reserve=min(reserve,max(0,int(opts['--max-evaluations'])-len(images)-int(opts['--initial-batch']))) if schedule=='interleaved' else 0,reference_evaluations=len(images),exploration_evaluations=0,completion_evaluations=0,completion_candidates=0,evaluation_image_order=','.join(map(str,order)))
for row in es:row['phase']='reference' if schedule=='interleaved' else 'sequential'
cs=[dict(hardware_id='h0001',image=i,node_id=n,monitor='input1',bit=1,lower=i,upper=i+1) for i in images for n in (0,10)]
partial=(Path(__file__).parent/'partial').exists()
no_numeric=(Path(__file__).parent/'no_numeric').exists()
if no_numeric:
 for row in es:
  row.update(status='quality_not_met',global_mse=80,global_psnr=10*math.log10(65025/80),roi_drop_score=0)
rank=[dict(hardware_id='h0001',rank=1,status='budget_incomplete' if partial else 'complete',complete=0 if partial else 1,evaluated_images=len(images),train_images=len(images),numeric_successes=len(images),worst_roi_drop_score=10,mean_roi_drop_score=10,installed_nodes=2,reason='')]
if no_numeric:rank[0].update(numeric_successes=0,worst_roi_drop_score=0,mean_roi_drop_score=0)
table('training_baselines.csv',list(base[0]),base);table('candidate_hardware.csv',list(h[0]),h)
table('training_evaluations.csv',list(es[0]),es);table('training_configurations.csv',list(cs[0]),cs)
table('training_ranking.csv',list(rank[0]),rank)
if not no_numeric:table('frozen_hardware.csv',['node_id','monitor','bit'],[{k:v for k,v in r.items() if k!='hardware_id'} for r in h])
sid='train_31_ten' if (Path(__file__).parent/'leak_seed').exists() else 'train_%02d_ten'%images[0]
seeds=[dict(seed_id=sid,**{k:v for k,v in r.items() if k not in ('hardware_id','image')}) for r in cs if r['image']==images[0]]
if not no_numeric:table('frozen_seeds.csv',list(seeds[0]),seeds)
stat=dict(actual_evaluations=len(images),duplicate_structures_skipped=0,reference_builds=len(images),preparation_seconds=.1,search_seconds=1,stop_reason='budget',**diag)
table('search_stats.csv',list(stat),[stat])
(out/'RUN.txt').write_text('completed=1\\napplication='+app+'\\ntrain_images='+','.join(map(str,images))+'\\nselection_status='+('no_numeric_hardware' if no_numeric else 'selected')+'\\nselected_hardware_id='+('' if no_numeric else 'h0001')+'\\nactual_evaluations='+str(len(images))+'\\nreference_builds='+str(len(images))+'\\nstop_reason=budget\\npreparation_seconds=.1\\nsearch_seconds=1\\n')
with (out/'RUN.txt').open('a') as stream:
 for key,value in diag.items():stream.write(str(key)+'='+str(value)+'\\n')
''',encoding='utf-8');train.chmod(0o755)
        return data,fixed,train

    def args(self,data,fixed,train,out):
        return [str(data),str(out),'--application','sobel','--skip-build','--train-exe',str(train),'--fixed-exe',str(fixed)]

    def test_only_ten_images_select_h_then_frozen_test_and_full_task_reuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root)
            for i in range(11,21):fixtures.protocol.resolve_staged_image_path(data,i).unlink()
            out=root/'run';self.assertEqual(runner.main(self.args(data,fixed,train,out)),0)
            meta=json.loads((out/'RUN.json').read_text());self.assertTrue(meta['all_succeeded'])
            self.assertTrue(meta['screening_disabled']);self.assertEqual(meta['splits']['train'],list(range(1,11)))
            self.assertNotIn('screen',meta['splits']);self.assertEqual(meta['screen_wall_seconds'],0)
            self.assertEqual(meta['training_scheduling'],'interleaved')
            self.assertEqual(meta['reference_evaluations'],10)
            self.assertEqual(meta['effective_completion_reserve'],20)
            self.assertEqual(fixtures.read(out/'applications.csv')[0]['evaluation_image_order'],
                             ','.join(map(str,meta['evaluation_image_order'])))
            self.assertEqual(len(fixtures.read(out/'best_per_image.csv')),10)
            self.assertEqual(len(fixtures.read(out/'images_all.csv')),30)
            self.assertEqual(len(fixtures.read(out/'interval_attempts.csv')),10)
            before=(out/'RUN.json').read_bytes();repeat=root/'repeat'
            self.assertEqual(runner.main(self.args(data,fixed,train,repeat)+['--resume-from',str(out)]),0)
            repeated=json.loads((repeat/'RUN.json').read_text());self.assertTrue(repeated['training_cache_hit'])
            self.assertEqual(repeated['cache_hits'],11)
            self.assertEqual((out/'RUN.json').read_bytes(),before)

    def test_schedule_options_change_training_cache_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root);out=root/'first'
            self.assertEqual(runner.main(self.args(data,fixed,train,out)),0)
            changed=root/'changed'
            self.assertEqual(runner.main(self.args(data,fixed,train,changed)+['--resume-from',str(out),
                             '--scheduling','sequential','--initial-batch','2','--completion-reserve','0']),0)
            meta=json.loads((changed/'RUN.json').read_text())
            self.assertFalse(meta['training_cache_hit']);self.assertEqual(meta['training_scheduling'],'sequential')
            self.assertEqual(meta['effective_completion_reserve'],0)

    def test_missing_schedule_metadata_and_invalid_options_stop_before_test(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root)
            train.write_text(train.read_text().replace("for key,value in diag.items():stream.write", "for key,value in {}.items():stream.write"))
            out=root/'stale'
            self.assertEqual(runner.main(self.args(data,fixed,train,out)),1)
            self.assertFalse((out/'frozen_hardware.csv').exists())
            for name,value in (('--initial-batch','0'),('--initial-batch','11'),('--completion-reserve','-1')):
                target=root/('invalid_'+value)
                self.assertEqual(runner.main(self.args(data,fixed,train,target)+[name,value]),1)
                self.assertFalse(target.exists())

    def test_wrong_training_only_permutation_stops_before_freeze(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root)
            train.write_text(train.read_text().replace("order=[images[i] for i in (0,9,4,2,7,1,8,3,6,5)] if schedule=='interleaved' else images", "order=images"))
            out=root/'wrong_order'
            self.assertEqual(runner.main(self.args(data,fixed,train,out)),1)
            self.assertFalse((out/'frozen_hardware.csv').exists())

    def test_partial_or_out_of_train_seed_never_freezes(self):
        for marker in ('partial','leak_seed'):
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);data,fixed,train=self.fixture(root);(root/marker).write_text('bad')
                out=root/'bad';self.assertEqual(runner.main(self.args(data,fixed,train,out)),1)
                self.assertFalse((out/'frozen_hardware.csv').exists())
                self.assertFalse(fixtures.read(out/'best_per_image.csv'))

    def test_training_program_failure_and_split_overlap_stop_before_test(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root);(root/'fail_train').write_text('fail')
            out=root/'failed';self.assertEqual(runner.main(self.args(data,fixed,train,out)),1)
            self.assertFalse((out/'frozen_hardware.csv').exists())
            overlap=root/'overlap'
            self.assertEqual(runner.main(self.args(data,fixed,train,overlap)+['--test','5:14']),1)
            self.assertFalse(overlap.exists())

    def test_complete_no_numeric_hardware_is_normal_without_fake_test_or_freeze(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root);(root/'no_numeric').write_text('none')
            out=root/'none';self.assertEqual(runner.main(self.args(data,fixed,train,out)),0)
            meta=json.loads((out/'RUN.json').read_text());self.assertTrue(meta['all_succeeded'])
            self.assertEqual(meta['selection_status'],'no_numeric_eligible_hardware')
            self.assertFalse(meta['test_executed']);self.assertFalse((out/'frozen_hardware.csv').exists())
            self.assertFalse(fixtures.read(out/'best_per_image.csv'))

    def test_frozen_bank_change_stops_next_test(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root)
            fixed.write_text(fixed.read_text()+'\nif image==31:Path(opts["--seeds"]).write_text("tampered")\n')
            out=root/'changed';self.assertEqual(runner.main(self.args(data,fixed,train,out)),1)
            self.assertEqual([r['image'] for r in fixtures.read(out/'best_per_image.csv')],['31'])

    def test_synced_sources_and_nonpilot_test_split_rejected_before_writes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root)
            out=root/'sources'/'new_run'
            self.assertEqual(runner.main(self.args(data,fixed,train,out)),1)
            self.assertFalse((root/'sources').exists())
            self.assertEqual(runner.main([str(data),str(root/'build_run'),'--build-dir',str(root/'sources'/'build')]),1)
            self.assertFalse((root/'sources').exists());self.assertFalse((root/'build_run').exists())
            other=root/'other_split'
            self.assertEqual(runner.main(self.args(data,fixed,train,other)+['--test','11:20']),1)
            self.assertFalse(other.exists())

if __name__=='__main__':unittest.main()
