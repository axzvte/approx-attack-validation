"""User-run only: shared ten-image application wrapper and report accounting.

Authored before the wrapper. No tests or fixture executables run by the author.
"""
import json
import math
import io
from contextlib import redirect_stderr
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parent))
import test_circuit_bitflip_ten_train_pipeline as ten_fixture
fixtures=ten_fixture.fixtures
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import run_circuit_bitflip_ten_train_batch as batch


class TenTrainBatchTests(unittest.TestCase):
    def setup_run(self,root):
        data,fixed,train=ten_fixture.TenTrainPipelineTests().fixture(root)
        argv=[str(data),str(root/'all'),'--roi-mode','legacy-fixed','--skip-build','--train-exe',str(train),'--fixed-exe',str(fixed)]
        return data,fixed,train,argv

    def test_summary_denominator_and_root_paths_preserve_missing_results(self):
        rows=[dict(status='numeric_eligible',global_psnr='30',roi_psnr='20',non_roi_psnr='32'),
              dict(status='quality_not_met',global_psnr='28',roi_psnr='24',non_roi_psnr='30'),
              dict(status='run_failed')]
        summary=batch.metrics(rows)
        self.assertEqual(summary['metric_images'],2);self.assertEqual(summary['numeric_test_successes'],1)
        self.assertEqual(summary['mean_roi_psnr'],22)
        self.assertEqual(batch.metrics([dict(status='no_legal_interval_state')])['mean_roi_psnr'],'')
        self.assertEqual(batch.prefixed_path('conv','test/h1/image_31/output.png'),'conv/test/h1/image_31/output.png')
        for value in ('../outside.png','/outside.png','C:\\outside.png','\\outside.png','test/../../outside.png'):
            with self.assertRaises(ValueError):batch.prefixed_path('conv',value)

    def test_remaining_three_apps_share_controls_and_keep_namespaced_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train,argv=self.setup_run(root)
            self.assertEqual(batch.main(argv),0);out=root/'all'
            run=json.loads((out/'RUN.json').read_text())
            self.assertTrue(run['completed']);self.assertTrue(run['all_succeeded'])
            self.assertEqual(run['applications'],['sharpen','conv','dct'])
            self.assertEqual(run['splits'],dict(train=list(range(1,11)),test=list(range(31,41))))
            apps=fixtures.read(out/'applications.csv');self.assertEqual(len(apps),3)
            for row in apps:
                self.assertEqual(row['status'],'completed');self.assertEqual(row['numeric_test_successes'],'10')
                self.assertEqual(row['metric_images'],'10');self.assertEqual(row['complete_candidates'],'1')
                self.assertEqual(row['training_scheduling'],'interleaved')
                self.assertEqual(row['actual_train_evaluations'],'10')
                self.assertAlmostEqual(float(row['mean_roi_psnr']),10*math.log10(65025/200))
                for field in ('hardware_selection_wall_seconds','test_wall_seconds','elapsed_seconds'):
                    self.assertGreaterEqual(float(row[field]),0)
                meta=json.loads((out/row['application']/'RUN.json').read_text())
                self.assertEqual(meta['options']['max_train_evaluations'],80)
                self.assertEqual(meta['options']['completion_reserve'],20)
            best=fixtures.read(out/'best_per_image.csv');images=fixtures.read(out/'images_all.csv')
            self.assertEqual(len(best),30);self.assertEqual(len(images),90)
            for row in images:
                self.assertTrue(row['path'].startswith(row['application']+'/'))
                self.assertTrue((out/row['path']).is_file())
            for row in best:
                for field in ('output_path','roi_comparison_path','background_comparison_path'):
                    self.assertTrue(row[field].startswith(row['application']+'/'))
                    self.assertTrue((out/row[field]).is_file())
            ranking=fixtures.read(out/'training_ranking_all.csv')
            self.assertEqual({r['application'] for r in ranking},{'sharpen','conv','dct'})
            before=(out/'RUN.json').read_bytes();repeat=root/'repeat'
            reused=list(argv);reused[1]=str(repeat)
            self.assertEqual(batch.main(reused+['--resume-from',str(out)]),0)
            self.assertTrue(all(int(r['cache_hits'])==11 for r in fixtures.read(repeat/'applications.csv')))
            self.assertEqual((out/'RUN.json').read_bytes(),before)

    def test_failure_is_visible_and_later_application_still_runs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);_,_,_,argv=self.setup_run(root);(root/'fail_train_conv').write_text('fail')
            errors=io.StringIO()
            with redirect_stderr(errors):self.assertEqual(batch.main(argv),1)
            self.assertIn('conv failed:',errors.getvalue());out=root/'all'
            apps={r['application']:r for r in fixtures.read(out/'applications.csv')}
            self.assertEqual(apps['conv']['status'],'failed');self.assertEqual(apps['conv']['mean_roi_psnr'],'')
            self.assertEqual(apps['dct']['status'],'completed');self.assertEqual(apps['sharpen']['status'],'completed')
            self.assertFalse(json.loads((out/'RUN.json').read_text())['all_succeeded'])
            self.assertTrue(any(r['application']=='conv' and r['status']=='failed' for r in fixtures.read(out/'run_status.csv')))

    def test_no_numeric_solution_has_blank_metrics_and_no_fake_images(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);_,_,_,argv=self.setup_run(root);(root/'no_numeric').write_text('none')
            self.assertEqual(batch.main(argv),0);out=root/'all'
            apps=fixtures.read(out/'applications.csv')
            self.assertTrue(all(r['selection_status']=='no_numeric_eligible_hardware' and
                                r['metric_images']=='0' and r['mean_roi_psnr']=='' for r in apps))
            self.assertFalse(fixtures.read(out/'best_per_image.csv'));self.assertFalse(list(out.rglob('*.png')))

    def test_guards_reject_invalid_inputs_before_creating_outputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train,argv=self.setup_run(root)
            for extra in (['--applications','conv,conv'],['--applications','bad'],['--train','1:9'],
                          ['--test','11:20'],['--initial-batch','0'],['--max-train-evaluations','9'],
                          ['--resume-from',str(root/'missing')]):
                self.assertEqual(batch.main(argv+extra),1);self.assertFalse((root/'all').exists())
            protected=list(argv);protected[1]=str(root/'sources'/'out')
            self.assertEqual(batch.main(protected),1);self.assertFalse((root/'sources').exists())
            self.assertEqual(batch.main([str(data),str(root/'all'),'--build-dir',str(root/'sources'/'build')]),1)
            self.assertFalse((root/'sources').exists());self.assertFalse((root/'all').exists())
            overlap=list(argv);overlap[1]=str(data/'out')
            self.assertEqual(batch.main(overlap),1);self.assertFalse((data/'out').exists())

    def test_build_once_and_explicit_sobel_control(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train,_=self.setup_run(root);build=root/'build';build.mkdir()
            shutil.copy2(fixed,build/'circuit_bitflip_fixed');shutil.copy2(train,build/'circuit_bitflip_ten_train')
            # Fixture executables look beside themselves for marker files only.
            with patch.object(batch,'process',return_value=0) as build_step:
                self.assertEqual(batch.main([str(data),str(root/'control'),'--build-dir',str(build),
                                           '--applications','sobel','--scheduling','sequential','--roi-mode','legacy-fixed']),0)
                self.assertEqual(build_step.call_count,2)
            row=fixtures.read(root/'control'/'applications.csv')[0]
            self.assertEqual(row['application'],'sobel');self.assertEqual(row['training_scheduling'],'sequential')

    def test_interrupt_does_not_start_next_application(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);_,_,_,argv=self.setup_run(root)
            with patch.object(batch.single,'main',return_value=130) as child:
                self.assertEqual(batch.main(argv),130);self.assertEqual(child.call_count,1)
            run=json.loads((root/'all'/'RUN.json').read_text())
            self.assertFalse(run['completed']);self.assertEqual(fixtures.read(root/'all'/'applications.csv')[0]['status'],'interrupted')

    def test_interrupted_application_retains_already_measured_phase_times(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);_,_,_,argv=self.setup_run(root)
            def interrupted(child_argv):
                output=Path(child_argv[1]);output.mkdir()
                (output/'RUN.json').write_text(json.dumps(dict(application='sharpen',
                    splits=dict(train=list(range(1,11)),test=list(range(31,41))),
                    training_wall_seconds=12,hardware_selection_wall_seconds=12,test_wall_seconds=3)))
                return 130
            with patch.object(batch.single,'main',side_effect=interrupted) as child:
                self.assertEqual(batch.main(argv),130);self.assertEqual(child.call_count,1)
            row=fixtures.read(root/'all'/'applications.csv')[0]
            self.assertEqual(row['status'],'interrupted')
            self.assertEqual(float(row['hardware_selection_wall_seconds']),12)
            self.assertEqual(float(row['test_wall_seconds']),3)

    def test_interruption_retains_observed_results_and_training_counts_in_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);_,_,_,argv=self.setup_run(root)
            def interrupted(child_argv):
                output=Path(child_argv[1]);output.mkdir()
                (output/'RUN.json').write_text(json.dumps(dict(application='sharpen',
                    splits=dict(train=list(range(1,11)),test=list(range(31,41))),
                    selection_status='selected',selected_hardware_id='h1',actual_train_evaluations=16,
                    training_wall_seconds=12,hardware_selection_wall_seconds=12,test_wall_seconds=3)))
                fixtures.table(output/'training_ranking.csv',[dict(hardware_id='h1',status='complete'),
                                                              dict(hardware_id='h2',status='budget_incomplete')])
                fixtures.table(output/'frozen_hardware.csv',[dict(node_id=n,monitor='input1',bit=1) for n in (0,3)])
                fixtures.table(output/'best_per_image.csv',[
                    dict(application='sharpen',image=31,status='numeric_eligible',global_psnr=31,roi_psnr=18,non_roi_psnr=33),
                    dict(application='sharpen',image=32,status='numeric_eligible',global_psnr=30.5,roi_psnr=22,non_roi_psnr=32)])
                return 130
            with patch.object(batch.single,'main',side_effect=interrupted):
                self.assertEqual(batch.main(argv),130)
            row=fixtures.read(root/'all'/'applications.csv')[0]
            self.assertEqual(row['status'],'interrupted');self.assertEqual(row['metric_images'],'2')
            self.assertEqual(float(row['mean_roi_psnr']),20);self.assertEqual(row['numeric_test_successes'],'2')
            self.assertEqual(row['installed_nodes'],'2');self.assertEqual(row['selected_hardware_id'],'h1')
            self.assertEqual(row['actual_train_evaluations'],'16');self.assertEqual(row['complete_candidates'],'1')
            self.assertEqual(row['budget_incomplete_candidates'],'1')
            self.assertEqual(len(fixtures.read(root/'all'/'best_per_image.csv')),2)


if __name__=='__main__':unittest.main()
