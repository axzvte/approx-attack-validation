"""User-run tests: new test pixels may change L/U only, never the frozen H.

Authored before implementation. The author does not execute this file.
"""
import csv
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_circuit_bitflip_ten_train_pipeline as ten_fixture
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import retest_circuit_bitflip_frozen as retest
from run_sharpen_bitflip_batch import atomic_csv, atomic_json, read_csv


def rows(path):
    return read_csv(path, [])


class FrozenRetestTests(unittest.TestCase):
    def fixture(self, root, apps=('sharpen', 'conv', 'sobel', 'dct')):
        data, fixed, train = ten_fixture.TenTrainPipelineTests().fixture(root)
        (data / 'stage5').mkdir()
        for name in ('10.jpg', '2.jpg', '1.jpg'):
            (data / 'stage5' / name).write_bytes(('new pixels ' + name).encode())
        controls = dict(train='1:10', max_train_evaluations=80, max_candidates=24,
                        scheduling='interleaved', completion_reserve=20, initial_batch=4,
                        beam=4, refinement=1, restarts=2, boundary_budget=24,
                        context_budget=128, max_full_evals=256, full_candidates=8,
                        global_psnr=30, convergence_tolerance=.001)
        argv = [str(data), str(root / 'out'), '--fixed-exe', str(fixed)]
        for app in apps:
            source = root / ('source_' + app)
            source.mkdir()
            # Build valid historical evidence using the existing controlled
            # solver fixture. This setup is outside the process spy below.
            command = [train, app, data, source / 'train', '--train', '1:10',
                       '--scheduling', 'interleaved', '--completion-reserve', '20',
                       '--initial-batch', '4', '--max-evaluations', '80']
            subprocess.run(list(map(str, command)), check=True, capture_output=True)
            for name in ('frozen_hardware.csv', 'frozen_seeds.csv'):
                shutil.copyfile(source / 'train' / name, source / name)
            key = retest.protocol.hardware_key(rows(source / 'frozen_hardware.csv'),
                                              retest.protocol.APPLICATION_NODES[app])
            hashes = {name: retest.protocol.digest(source / name)
                      for name in ('frozen_hardware.csv', 'frozen_seeds.csv')}
            protocol = dict(schema='ten_train_v1', application=app, hardware_id='h0001',
                            hardware_key=key, splits=dict(train=list(range(1, 11)), test=list(range(31, 41))),
                            options=controls, frozen_hashes=hashes,
                            roi_sha256=retest.protocol.digest(data / 'mask/roi_mask.jpg'))
            atomic_json(source / 'frozen_protocol.json', protocol)
            atomic_json(source / 'RUN.json', dict(application=app, workflow='ten_train',
                        completed=True, all_succeeded=True, selection_status='selected',
                        selected_hardware_id='h0001', options=controls, splits=protocol['splits'],
                        frozen_hashes=hashes, roi_sha256=protocol['roi_sha256']))
            argv.extend(['--source', app + '=' + str(source)])
        return data, fixed, argv

    def fake_process(self, command, log, cwd):
        command = list(map(str, command))
        self.commands.append(command)
        self.assertEqual(Path(command[0]), self.fixed)
        self.assertIn('--input-image', command)
        self.assertIn('--continuous', command)
        tail = [v for v in command[4:] if v != '--continuous']
        flags = dict(zip(tail[::2], tail[1::2]))
        self.assertEqual(flags['--image-mode'], 'comparisons')
        self.assertEqual(flags['--context-budget'], '128')
        self.assertEqual(flags['--max-full-evals'], '256')
        self.assertEqual(flags['--global-psnr'], '30.0')
        app = command[1]
        image = int(flags['--image'])
        if self.interrupt and len(self.commands) == 2:
            raise KeyboardInterrupt
        if self.fail and image == 2:
            return 9
        out = Path(command[3])
        out.mkdir(parents=True)
        hardware = rows(Path(flags['--hardware']))
        seeds = rows(Path(flags['--seeds']))
        self.assertTrue(all(r['seed_id'].startswith('train_') for r in seeds))
        self.assertEqual({(r['node_id'], r['monitor'], r['bit']) for r in seeds},
                         {(r['node_id'], r['monitor'], r['bit']) for r in hardware})
        selected_global = 80 if self.numeric_failure and image == 2 else 60
        selected_status = 'quality_not_met' if selected_global == 80 else 'numeric_eligible'
        summary = [dict(application=app, image=image, candidate_id='baseline', status='reference',
                        installed_nodes=0, triggered_nodes=0, visual_status='reference',
                        search_status='completed', global_mse=20, roi_mse=20, non_roi_mse=20),
                   dict(application=app, image=image, candidate_id='selected', status=selected_status,
                        installed_nodes=len(hardware), triggered_nodes=len(hardware),
                        visual_status='pending_user_review' if selected_status == 'numeric_eligible' else 'not_eligible',
                        search_status='completed', images_exported=1, global_mse=selected_global,
                        roi_mse=200, non_roi_mse=40)]
        import math
        for row in summary:
            for region in ('global', 'roi', 'non_roi'):
                row[region + '_psnr'] = 10 * math.log10(65025 / row[region + '_mse'])
        no_state = self.no_state and image == 2
        if no_state:
            summary[1].update(status='no_legal_interval_state', visual_status='not_available', images_exported=0)
            for region in ('global', 'roi', 'non_roi'):
                summary[1][region + '_mse'] = ''
                summary[1][region + '_psnr'] = ''
        atomic_csv(out / 'summary.csv', summary, list(summary[0]))
        configs = [dict(application=app, image=image, candidate_id='selected', **r,
                        lower=0, upper=100, dynamic_triggers=1) for r in hardware]
        atomic_csv(out / 'configurations.csv', [] if no_state else configs, list(configs[0]))
        stat = dict(application=app, image=image, contexts_built=2, context_budget_exhausted=0,
                    full_budget_exhausted=0, full_evaluations=4, continuous=1,
                    scoring='full_joint_image_reconstruction' if app == 'dct' else 'exact_conditional_pixel_replay')
        atomic_csv(out / 'search_stats.csv', [stat], list(stat))
        manifest = [dict(candidate_id='selected', image_kind=kind, path='selected_' + kind + '.png')
                    for kind in ('comparison', 'roi_comparison')]
        manifest_fields = list(manifest[0])
        if no_state:
            manifest = []
        atomic_csv(out / 'images.csv', manifest, manifest_fields)
        for row in manifest:
            (out / row['path']).write_bytes(b'controlled image placeholder')
        notes = dict(completed=1, application=app, image=image, input_image=flags['--input-image'],
                     roi=str(self.data / 'mask/roi_mask.jpg'), image_mode='comparisons',
                     continuous=1, input_bits='signed12', output_bits='signed13_non_sign_0..11',
                     baseline='original_application_default', reference='application_exact_output',
                     warm_seeds=len({r['seed_id'] for r in seeds}))
        for flag in ('beam', 'refinement', 'restarts', 'boundary-budget', 'context-budget',
                     'full-candidates', 'convergence-tolerance'):
            notes[flag.replace('-', '_')] = flags['--' + flag]
        notes['max_full_evaluations'] = flags['--max-full-evals']
        notes['global_psnr_threshold'] = flags['--global-psnr']
        if self.wrong_input:
            notes['input_image'] = str(self.data / 'stage1/input/image_01.jpg')
        (out / 'RUN.txt').write_text(''.join(f'{k}={v}\n' for k, v in notes.items()))
        return 0

    def run_fixture(self, root, apps=('sharpen', 'conv', 'sobel', 'dct')):
        self.data, self.fixed, argv = self.fixture(root, apps)
        self.commands = []
        self.fail = self.interrupt = self.numeric_failure = self.wrong_input = self.no_state = False
        return argv

    def test_four_frozen_hardware_only_and_two_triptychs_in_numeric_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            argv = self.run_fixture(root)
            # Regression: sharing the evaluator's parent is safe. Protect the
            # executable file itself, not every sibling output directory.
            self.assertEqual(self.fixed.parent, Path(argv[1]).parent)
            self.assertFalse(retest.overlap(Path(argv[1]), self.fixed))
            before = {p: p.read_bytes() for p in root.glob('source_*/**/*') if p.is_file()}
            with patch.object(retest, 'process', side_effect=self.fake_process):
                self.assertEqual(retest.main(argv), 0)
            self.assertEqual(len(self.commands), 12)
            self.assertEqual([Path(c[c.index('--input-image') + 1]).name for c in self.commands[:3]],
                             ['1.jpg', '2.jpg', '10.jpg'])
            out = root / 'out'
            self.assertEqual(len(list(out.rglob('*.png'))), 24)
            self.assertEqual(len(rows(out / 'best_per_image.csv')), 12)
            self.assertEqual(len(rows(out / 'images_all.csv')), 24)
            for row in rows(out / 'best_per_image.csv'):
                for field in ('comparison_path', 'roi_comparison_path'):
                    self.assertTrue((out / row[field]).is_file())
                self.assertIn('baseline_roi_psnr', row)
            run = json.loads((out / 'RUN.json').read_text())
            self.assertTrue(run['completed'] and run['all_succeeded'])
            self.assertEqual(run['training_evaluations'], 0)
            self.assertEqual(run['test_images'], ['1.jpg', '2.jpg', '10.jpg'])
            self.assertTrue(all(row['hardware_selection_wall_seconds'] == '0'
                                for row in rows(out / 'applications.csv')))
            self.assertEqual(before, {p: p.read_bytes() for p in before})

    def test_numeric_failure_keeps_two_images_and_actual_status(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            argv = self.run_fixture(root, ('conv',))
            self.numeric_failure = True
            with patch.object(retest, 'process', side_effect=self.fake_process):
                self.assertEqual(retest.main(argv), 0)
            results = rows(root / 'out/best_per_image.csv')
            self.assertEqual(results[1]['status'], 'quality_not_met')
            self.assertEqual(len(list((root / 'out').rglob('*.png'))), 6)
            self.assertEqual(rows(root / 'out/applications.csv')[0]['numeric_test_successes'], '2')

    def test_operational_failure_and_wrong_input_remain_visible(self):
        for case in ('fail', 'wrong_input'):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                argv = self.run_fixture(root, ('conv',))
                setattr(self, case, True)
                with patch.object(retest, 'process', side_effect=self.fake_process):
                    self.assertEqual(retest.main(argv), 1)
                self.assertEqual(len(self.commands), 3)
                run = json.loads((root / 'out/RUN.json').read_text())
                self.assertFalse(run['all_succeeded'])
                self.assertTrue(any(r['status'] == 'run_failed' for r in rows(root / 'out/best_per_image.csv')))

    def test_no_legal_state_does_not_fabricate_images_or_quality(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            argv = self.run_fixture(root, ('dct',))
            self.no_state = True
            with patch.object(retest, 'process', side_effect=self.fake_process):
                self.assertEqual(retest.main(argv), 0)
            results = rows(root / 'out/best_per_image.csv')
            self.assertEqual(results[1]['status'], 'no_legal_interval_state')
            self.assertEqual(results[1]['comparison_path'], '')
            self.assertEqual(results[1]['global_psnr'], '')
            self.assertEqual(len(list((root / 'out').rglob('*.png'))), 4)
            self.assertEqual(rows(root / 'out/applications.csv')[0]['metric_images'], '2')

    def test_changed_image_during_call_invalidates_its_result(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            argv = self.run_fixture(root, ('conv',))
            def mutate(command, log, cwd):
                result = self.fake_process(command, log, cwd)
                image = Path(command[command.index('--input-image') + 1])
                image.write_bytes(b'changed while evaluator ran')
                return result
            with patch.object(retest, 'process', side_effect=mutate):
                self.assertEqual(retest.main(argv), 1)
            self.assertTrue(all(r['status'] == 'run_failed' for r in rows(root / 'out/best_per_image.csv')))
            self.assertFalse(json.loads((root / 'out/RUN.json').read_text())['all_succeeded'])

    def test_interruption_stops_launches_and_preserves_partial_timing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            argv = self.run_fixture(root, ('conv', 'dct'))
            self.interrupt = True
            with patch.object(retest, 'process', side_effect=self.fake_process):
                self.assertEqual(retest.main(argv), 130)
            self.assertEqual(len(self.commands), 2)
            run = json.loads((root / 'out/RUN.json').read_text())
            self.assertFalse(run['completed'])
            self.assertGreaterEqual(run['total_wall_seconds'], 0)
            self.assertTrue(any(r['status'] == 'interrupted' for r in rows(root / 'out/run_status.csv')))

    def test_invalid_source_roi_duplicate_name_and_overlap_rejected_before_calls(self):
        for case in ('hardware', 'mask', 'duplicate', 'overlap', 'evaluator_overlap',
                     'source_overlap', 'protected_sources', 'source_key', 'options'):
            with self.subTest(case=case), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                argv = self.run_fixture(root, ('conv',))
                source = root / 'source_conv'
                if case == 'hardware':
                    (source / 'frozen_hardware.csv').write_text('changed')
                elif case == 'mask':
                    (self.data / 'mask/roi_mask.jpg').write_bytes(b'changed')
                elif case == 'duplicate':
                    (self.data / 'stage5/1.png').write_bytes(b'duplicate')
                elif case == 'overlap':
                    argv[1] = str(self.data / 'out')
                elif case == 'evaluator_overlap':
                    argv[1] = str(self.fixed / 'out')
                elif case == 'source_overlap':
                    argv[1] = str(source / 'out')
                elif case == 'protected_sources':
                    argv[1] = str(root / 'sources/out')
                else:
                    path = source / 'frozen_protocol.json'
                    metadata = json.loads(path.read_text())
                    if case == 'source_key':
                        metadata['hardware_key'][0][2] = 11
                    else:
                        metadata['options']['context_budget'] = 0
                    atomic_json(path, metadata)
                with patch.object(retest, 'process') as child:
                    self.assertEqual(retest.main(argv), 1)
                    child.assert_not_called()
                self.assertFalse((root / 'out').exists())


if __name__ == '__main__':
    unittest.main()
