"""User-run Conv completion protocol fixtures. Authored first, unrun locally."""
import csv
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parent))
import test_circuit_bitflip_ten_train_pipeline as ten
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from bitflip_workflow import TaskCache
import complete_conv_ten_train as completion

ORDER=[1,10,5,3,8,2,9,4,7,6]


def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def table(path,rows):
    with path.open('w',newline='') as stream:
        writer=csv.DictWriter(stream,list(rows[0]));writer.writeheader();writer.writerows(rows)


def snapshot(root):return {p.relative_to(root).as_posix():digest(p) for p in root.rglob('*') if p.is_file()}


class CompletionTests(unittest.TestCase):
    def fixture(self,root):
        data,fixed,train=ten.TenTrainPipelineTests().fixture(root)
        for image in range(11,41):ten.fixtures.protocol.resolve_staged_image_path(data,image).unlink()
        fixed_code=fixed.read_text().replace('import sys\n','import sys\ncompletion_argv=list(sys.argv[1:])\n')
        fixed.write_text(fixed_code+'''
import json
with (Path(__file__).parent/'completion_calls.jsonl').open('a') as stream:
 stream.write(json.dumps(dict(image=image,argv=completion_argv))+'\\n')
with (out/'RUN.txt').open('a') as stream:
 for name,key in [('beam','--beam'),('refinement','--refinement'),('restarts','--restarts'),('boundary_budget','--boundary-budget'),('context_budget','--context-budget'),('max_full_evaluations','--max-full-evals'),('full_candidates','--full-candidates'),('global_psnr_threshold','--global-psnr'),('convergence_tolerance','--convergence-tolerance')]:
  stream.write(name+'='+opts[key]+'\\n')
 stream.write('continuous=1\\nwarm_seeds=0\\ninput_bits=signed12\\noutput_bits=signed13_non_sign_0..11\\nbaseline=original_application_default\\n')
if (Path(__file__).parent/'fail_completion').exists() and image==8:sys.exit(7)
''')
        source=root/'source';native=source/'train';native.mkdir(parents=True)
        def quality(g,r,n):
            result=dict(global_mse=g,roi_mse=r,non_roi_mse=n)
            for region in ('global','roi','non_roi'):result[region+'_psnr']=10*math.log10(65025/result[region+'_mse'])
            return result
        baseline=[dict(application='conv',image=i,**quality(20,20,20)) for i in range(1,11)]
        keys={'ten_h1':[(0,'input1',3),(7,'input2',3)],'ten_h2':[(n,'input1',4) for n in range(4)]}
        hardware=[dict(hardware_id=h,node_id=n,monitor=m,bit=b) for h,key in keys.items() for n,m,b in key]
        evaluations=[];configurations=[]
        for h,images in (('ten_h1',list(range(1,11))),('ten_h2',ORDER[:4])):
            for i in images:
                evaluations.append(dict(application='conv',hardware_id=h,image=i,status='numeric_eligible',
                    **quality(60,200 if h=='ten_h1' else 2000,40),baseline_roi_mse=20,
                    roi_drop_score=10 if h=='ten_h1' else 20,elapsed_seconds=.1,contexts_built=2,
                    full_evaluations=0,completed_sweeps=2,stopped_for_convergence=1,
                    reference_cache_hit=1,baseline_cache_hit=1,phase='reference' if h=='ten_h1' else 'exploration'))
                configurations.extend(dict(hardware_id=h,image=i,node_id=n,monitor=m,bit=b,lower=i,upper=i+1) for n,m,b in keys[h])
        ranking=[dict(hardware_id='ten_h1',rank=1,status='complete',complete=1,evaluated_images=10,train_images=10,
                      numeric_successes=10,worst_roi_drop_score=10,mean_roi_drop_score=10,installed_nodes=2,reason='complete'),
                 dict(hardware_id='ten_h2',rank=2,status='budget_incomplete',complete=0,evaluated_images=4,train_images=10,
                      numeric_successes=4,worst_roi_drop_score=20,mean_roi_drop_score=8,installed_nodes=4,reason='budget')]
        diag=dict(scheduling='interleaved',initial_batch=4,completion_reserve=20,effective_completion_reserve=20,
                  reference_evaluations=10,exploration_evaluations=4,completion_evaluations=0,completion_candidates=0,
                  evaluation_image_order=','.join(map(str,ORDER)))
        notes=dict(completed=1,application='conv',train_images=','.join(map(str,range(1,11))),selection_status='selected',
                   selected_hardware_id='ten_h1',actual_evaluations=14,reference_builds=10,stop_reason='fixture_stop',
                   preparation_seconds=.1,search_seconds=1,**diag)
        (native/'RUN.txt').write_text(''.join(f'{k}={v}\n' for k,v in notes.items()))
        for name,rows in [('training_baselines.csv',baseline),('candidate_hardware.csv',hardware),
                          ('training_evaluations.csv',evaluations),('training_configurations.csv',configurations),
                          ('training_ranking.csv',ranking),('search_stats.csv',[dict(actual_evaluations=14,
                           duplicate_structures_skipped=0,reference_builds=10,preparation_seconds=.1,search_seconds=1,stop_reason='fixture_stop',**diag)])]:
            table(native/name,rows)
        table(native/'frozen_hardware.csv',[dict(node_id=n,monitor=m,bit=b) for n,m,b in keys['ten_h1']])
        table(native/'frozen_seeds.csv',[dict(seed_id='train_01_ten',node_id=n,monitor=m,bit=b,lower=1,upper=2) for n,m,b in keys['ten_h1']])
        controls=dict(train='1:10',max_evaluations=80,max_candidates=24,initial_candidates=3,proposals_per_round=3,max_rounds=6,
                      patience=2,workers=4,scheduling='interleaved',initial_batch=4,completion_reserve=20,context_budget=64,
                      beam=4,refinement=1,restarts=2,boundary_budget=24,max_full_evals=256,full_candidates=8,
                      global_psnr=30,convergence_tolerance=.001,seed_limit=2)
        options={**controls,'train_context_budget':64,'max_train_evaluations':80}
        hashes={str(i):digest(ten.fixtures.protocol.resolve_staged_image_path(data,i)) for i in range(1,11)}
        mask=digest(data/'mask/roi_mask.jpg');fingerprint='0'*64
        run=dict(application='conv',workflow='ten_train',completed=True,all_succeeded=True,training_scheduling='interleaved',
                 splits=dict(train=list(range(1,11)),test=list(range(31,41))),selected_hardware_id='ten_h1',
                 actual_train_evaluations=14,options=options,training_input_hashes=hashes,roi_sha256=mask,
                 source_fingerprint=fingerprint,hardware_selection_wall_seconds=1.1)
        (source/'RUN.json').write_text(json.dumps(run))
        TaskCache(source).record('train',dict(schema=2,kind='ten_image_training',application='conv',inputs=hashes,
                                            roi=mask,source=fingerprint,executable=digest(train),controls=controls))
        identity=source/'test/ten_h1/image_31';identity.mkdir(parents=True)
        (identity/'TASK.json').write_text(json.dumps(dict(version=1,completed=True,signature=dict(kind='fixed_intervals',
            application='conv',executable_sha256=digest(fixed),source_sha256=fingerprint,mask_sha256=mask,entry_style='circuit'))))
        return data,source,train,fixed

    def args(self,data,source,train,fixed,out):
        return [str(data),str(source),str(out),'--train-exe',str(train),'--fixed-exe',str(fixed)]

    def test_only_missing_six_run_with_original_options_and_no_warm_seeds(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,train,fixed=self.fixture(root);before=snapshot(source);out=root/'done'
            self.assertEqual(completion.main(self.args(data,source,train,fixed,out)),0)
            calls=[json.loads(line) for line in (root/'completion_calls.jsonl').read_text().splitlines()]
            self.assertEqual(len(calls),6);self.assertEqual({r['image'] for r in calls},set(ORDER[4:]))
            for row in calls:
                argv=row['argv'];self.assertNotIn('--seeds',argv);self.assertIn('--continuous',argv)
                for flag,value in (('--context-budget','64'),('--beam','4'),('--refinement','1'),('--restarts','2'),('--image-mode','none')):
                    self.assertEqual(argv[argv.index(flag)+1],value)
            run=json.loads((out/'RUN.json').read_text())
            self.assertTrue(run['candidate_complete']);self.assertTrue(run['all_succeeded']);self.assertFalse(run['test_executed'])
            self.assertEqual(run['reused_candidate_evaluations'],4);self.assertEqual(run['supplementary_evaluations'],6)
            self.assertEqual(run['original_train_evaluations'],14);self.assertEqual(run['combined_train_evaluations'],20)
            rows=ten.fixtures.read(out/'completed_candidate_evaluations.csv')
            self.assertEqual(len(rows),10);self.assertEqual(sum(r['evidence_origin']=='original' for r in rows),4)
            old=ten.fixtures.read(source/'train/training_evaluations.csv')
            for row in rows:
                if row['evidence_origin']=='original':
                    saved=next(r for r in old if r['hardware_id']=='ten_h2' and r['image']==row['image'])
                    for key,value in saved.items():self.assertEqual(row[key],value)
            ranking=ten.fixtures.read(out/'ranking_completed.csv')
            self.assertEqual(ranking[0]['hardware_id'],'ten_h2');self.assertEqual(ranking[0]['complete'],'1')
            self.assertEqual(snapshot(source),before);self.assertFalse(list(out.rglob('*.png')))

    def test_provenance_and_readonly_guards_fail_before_output_or_calls(self):
        for fault in ('input','mask','fixed_binary','train_binary','artifact','options','source_overlap','sources'):
            with self.subTest(fault=fault),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);data,source,train,fixed=self.fixture(root);out=root/'bad'
                if fault=='input':ten.fixtures.protocol.resolve_staged_image_path(data,1).write_bytes(b'changed')
                elif fault=='mask':(data/'mask/roi_mask.jpg').write_bytes(b'changed')
                elif fault=='fixed_binary':fixed.write_text(fixed.read_text()+'\n#changed\n')
                elif fault=='train_binary':train.write_text(train.read_text()+'\n#changed\n')
                elif fault=='artifact':(source/'train/training_ranking.csv').write_text('changed')
                elif fault=='options':
                    run=json.loads((source/'RUN.json').read_text());run['options']['train_context_budget']=128
                    (source/'RUN.json').write_text(json.dumps(run))
                elif fault=='source_overlap':out=source/'new'
                else:out=root/'sources'/'bad'
                self.assertEqual(completion.main(self.args(data,source,train,fixed,out)),1)
                self.assertFalse(out.exists());self.assertFalse((root/'completion_calls.jsonl').exists())

    def test_process_failure_is_visible_and_cannot_complete_candidate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,source,train,fixed=self.fixture(root);(root/'fail_completion').write_text('fail');out=root/'failed'
            self.assertEqual(completion.main(self.args(data,source,train,fixed,out)),1)
            run=json.loads((out/'RUN.json').read_text());self.assertFalse(run['candidate_complete']);self.assertFalse(run['all_succeeded'])
            statuses=ten.fixtures.read(out/'run_status.csv');self.assertEqual(len(statuses),6)
            self.assertTrue(any(r['status']=='run_failed' and r['returncode']=='7' for r in statuses))
            rank=next(r for r in ten.fixtures.read(out/'ranking_completed.csv') if r['hardware_id']=='ten_h2')
            self.assertEqual(rank['complete'],'0');self.assertEqual(rank['evaluated_images'],'9')


if __name__=='__main__':unittest.main()
