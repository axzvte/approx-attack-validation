"""User-run whole-flow regression checks with a controlled external solver."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parent))
import test_sharpen_bitflip_hardware_pipeline as fixtures

pipeline=fixtures.module

class WorkflowPipelineTests(unittest.TestCase):
    def test_ranking_sum_is_independent_of_scheduling_order(self):
        h=dict(hardware_id='h1',key=((0,'input1',1),(1,'input1',1)))
        rows=[dict(hardware_id='h1',image=i,status='numeric_eligible',roi_drop_score=score)
              for i,score in ((1,99.),(2,1e-14),(3,1e-14))]
        self.assertEqual(pipeline.rank_hardware([h],rows,[1,2,3]),pipeline.rank_hardware([h],list(reversed(rows)),[1,2,3]))

    def setup_run(self,root,out):
        data,source,exe=fixtures.PipelineTests().fixture(root)
        # Candidate bit1 is the complete incumbent; bit2 loses already on first
        # image by worst-score bound. This deliberately differs from old fixture.
        exe.write_text(exe.read_text().replace('ok=not(bit==2 and image==4)','ok=True').replace('200 if bit==1 else 2000','200 if bit==1 else 100'))
        rows=fixtures.read(source/'summary_all.csv')
        for row in rows:
            if row['status']=='numeric_eligible':row['roi_mse']=300 if row['candidate_id']=='n2_rank1' else 100
        fixtures.table(source/'summary_all.csv',list(rows[0]),rows)
        args=fixtures.PipelineTests().args(data,source,exe,out)
        args[args.index('--workflow')+1]='balanced'
        return data,source,exe,args

    def test_progressive_and_exhaustive_match_winner_with_honest_denominators(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);out=root/'progressive';data,source,exe,args=self.setup_run(root,out)
            args+=['--screen-workers','1','--adaptive-intervals','off']
            self.assertEqual(pipeline.main(args),0)
            ranking=fixtures.read(out/'screening_ranking.csv')
            self.assertEqual(ranking[0]['evaluated_images'],'2')
            loser=next(r for r in ranking if r['hardware_id']!=ranking[0]['hardware_id'])
            self.assertEqual(loser['screening_status'],'bound_eliminated')
            self.assertEqual(loser['evaluated_images'],'1')
            self.assertEqual(loser['complete'],'0')
            run=json.loads((out/'RUN.json').read_text())
            self.assertEqual(run['screen_task_count'],3)
            self.assertEqual(run['screen_planned_task_count'],4)
            self.assertEqual(run['screen_bound_skipped_tasks'],1)
            other=root/'exhaustive';args[1]=str(other)
            self.assertEqual(pipeline.main(args+['--screening','exhaustive']),0)
            self.assertEqual((out/'frozen_hardware.csv').read_bytes(),(other/'frozen_hardware.csv').read_bytes())
            self.assertEqual((out/'frozen_seeds.csv').read_bytes(),(other/'frozen_seeds.csv').read_bytes())

    def test_read_only_resume_reuses_matching_tasks_and_image_change_invalidates(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);out=root/'first';data,source,exe,args=self.setup_run(root,out)
            args+=['--screen-workers','1','--adaptive-intervals','off']
            self.assertEqual(pipeline.main(args),0)
            before=(out/'RUN.json').read_bytes()
            second=root/'second';args[1]=str(second)
            self.assertEqual(pipeline.main(args+['--resume-from',str(out)]),0)
            self.assertEqual((out/'RUN.json').read_bytes(),before)
            statuses=fixtures.read(second/'run_status.csv')
            self.assertTrue(all(r['cache_hit']=='1' for r in statuses if r['status']=='completed'))
            pipeline.resolve_staged_image_path(data,5).write_bytes(b'changed test pixels')
            third=root/'third';args[1]=str(third)
            self.assertEqual(pipeline.main(args+['--resume-from',str(out)]),0)
            statuses=fixtures.read(third/'run_status.csv')
            row=next(r for r in statuses if r['phase']=='test' and r['image']=='5')
            self.assertEqual(row['cache_hit'],'0')
            self.assertEqual((out/'RUN.json').read_bytes(),before)

    def test_adaptive_retains_best_executed_attempt_and_only_three_images(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);out=root/'adaptive';data,source,exe,args=self.setup_run(root,out)
            # More budget can move the heuristic search to a worse solution.
            exe.write_text(exe.read_text().replace('200 if bit==1 else 100','(500 if int(opts["--context-budget"])<=32 else 300) if bit==1 else 100'))
            self.assertEqual(pipeline.main(args+['--screen-workers','1']),0)
            best=fixtures.read(out/'best_per_image.csv')
            self.assertTrue(all(float(r['roi_mse'])==500 for r in best))
            self.assertEqual(len(list((out/'test').rglob('*.png'))),6)
            attempts=fixtures.read(out/'interval_attempts.csv')
            self.assertTrue(all(sum(r['selected']=='1' for r in attempts if r['phase']==phase and r['hardware_id']==hid and r['image']==image)==1
                                for phase,hid,image in {(r['phase'],r['hardware_id'],r['image']) for r in attempts}))

    def test_program_failure_is_never_a_bound_elimination(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);out=root/'failed';data,source,exe,args=self.setup_run(root,out)
            (root/'fail_screen').write_text('fail')
            self.assertEqual(pipeline.main(args),1)
            self.assertFalse((out/'frozen_hardware.csv').exists())
            run=json.loads((out/'RUN.json').read_text());self.assertTrue(run['failed_tasks'])

    def test_failed_effort_keeps_cost_and_never_falls_back_to_successful_probe(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);out=root/'failed_effort';data,source,exe,args=self.setup_run(root,out)
            exe.write_text(exe.read_text()+'\nif image==3 and int(opts["--context-budget"])>=64:sys.exit(9)\n')
            self.assertEqual(pipeline.main(args+['--screen-workers','1']),1)
            rows=fixtures.read(out/'interval_attempts.csv')
            failed=[r for r in rows if r['status']=='run_failed']
            self.assertTrue(failed)
            self.assertTrue(all(r['returncode']=='9' and r['context_budget']=='64' and float(r['elapsed_seconds'])>=0 for r in failed))
            self.assertTrue(any(r['image']=='3' and r['status']=='numeric_eligible' for r in rows))
            self.assertFalse((out/'frozen_hardware.csv').exists())

    def test_single_train_image_does_not_require_two_pilots(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);out=root/'single';data,source,exe,args=self.setup_run(root,out)
            for name in ('summary_all.csv','configurations_all.csv'):
                rows=[r for r in fixtures.read(source/name) if r['image']=='1']
                fixtures.table(source/name,list(rows[0]),rows)
            args[args.index('--train')+1]='1:1';args[args.index('--workflow')+1]='reference'
            self.assertEqual(pipeline.main(args),0)

    def test_retest_inherits_adaptive_interval_policy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);out=root/'adaptive_origin';data,source,exe,args=self.setup_run(root,out)
            self.assertEqual(pipeline.main(args+['--screen-workers','1']),0)
            repeated=root/'retest'
            self.assertEqual(fixtures.retest.main([str(data),str(out),str(repeated),'--test','5:6','--skip-build','--fixed-exe',str(exe)]),0)
            attempts=fixtures.read(repeated/'interval_attempts.csv')
            self.assertEqual(len(attempts),4)
            run=json.loads((repeated/'RUN.json').read_text())
            self.assertTrue(run['interval_policy_inherited'])
            self.assertEqual(run['options']['workflow'],'balanced')

    def test_shared_training_uses_only_prior_train_evidence_and_reduced_exploration(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);out=root/'fresh';data,source,exe,args=self.setup_run(root,out)
            discovery=root/'discovery'
            # This fixture emulates the legacy positional Sharpen discovery CLI.
            discovery.write_text('''#!/usr/bin/env python3
import csv,json,sys
from pathlib import Path
data,out=map(Path,sys.argv[1:3]);image=int(sys.argv[3]);out.mkdir()
def table(name,fields,rows):
 with (out/name).open('w',newline='') as f:
  w=csv.DictWriter(f,fields);w.writeheader();w.writerows(rows)
summaries=[dict(candidate_id='baseline',status='reference',global_mse=20,roi_mse=20),dict(candidate_id='n2_rank1',status='numeric_eligible',global_mse=60,roi_mse=200)]
configs=[dict(candidate_id='n2_rank1',node_id=n,monitor='input1',bit=1,lower=image,upper=image+1) for n in (0,1)]
table('summary.csv',list(summaries[0]),summaries);table('configurations.csv',list(configs[0]),configs)
(out/'RUN.txt').write_text('completed=1\\nimage='+str(image)+'\\n')
(out/'arguments.json').write_text(json.dumps(sys.argv[3:]))
''',encoding='utf-8');discovery.chmod(0o755)
            pos=args.index('--training-report');del args[pos:pos+2]
            args[args.index('--train')+1]='1:3';args[args.index('--screen')+1]='4:5';args[args.index('--test')+1]='6:7'
            self.assertEqual(pipeline.main(args+['--discovery-exe',str(discovery),'--screen-workers','1']),0)
            train=fixtures.read(out/'training/summary_all.csv')
            self.assertEqual({int(r['image']) for r in train},{1,2,3})
            self.assertTrue(any(r['candidate_id'].startswith('shared_') and r['image']=='3' for r in train))
            first=json.loads((out/'training/image_01/arguments.json').read_text())
            later=json.loads((out/'training/image_03/arguments.json').read_text())
            self.assertIn('--sensitivity-order',first)
            self.assertLess(int(later[later.index('--context-budget')+1]),int(first[first.index('--context-budget')+1]))
            seeds=fixtures.read(out/'training_shared_hardware/shared_02_01/seeds.csv')
            self.assertTrue(all(r['seed_id'].startswith(('train_01_','train_02_')) for r in seeds))
            frozen=fixtures.read(out/'frozen_seeds.csv')
            self.assertTrue(all(r['seed_id'].startswith(('train_01_','train_02_','train_03_')) for r in frozen))

if __name__=='__main__':unittest.main()
