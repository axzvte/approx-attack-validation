"""Strict ROI batch defaults, forwarding and application isolation."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
import test_u2net_roi_pipeline as fixture
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import run_circuit_bitflip_ten_train_batch as batch

class OtherAppsTests(unittest.TestCase):
    def test_default_includes_all_four_applications(self):
        self.assertEqual(batch.DEFAULT_APPLICATIONS,('sharpen','conv','sobel','dct'))

    def test_three_apps_use_same_per_image_roi_with_independent_hardware_and_reports(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=fixture.U2netPipelineTests().fixture(root)
            roi=root/'external_roi';(data/'roi').rename(roi);out=root/'batch'
            self.assertEqual(batch.main([str(data),str(out),'--applications','conv,sobel,dct',
                '--roi-dir',str(roi),'--skip-build','--train-exe',str(train),'--fixed-exe',str(fixed)]),0)
            meta=json.loads((out/'RUN.json').read_text())
            self.assertEqual(meta['applications'],['conv','sobel','dct'])
            self.assertEqual(meta['roi_mode'],'per-image')
            self.assertEqual(meta['roi_dir'],str(roi))
            self.assertFalse(meta['u2net_runtime_hardware'])
            for app,last in (('conv',7),('sobel',10),('dct',31)):
                child=json.loads((out/app/'RUN.json').read_text())
                self.assertTrue(child['seed_bank_unchanged_after_test'])
                self.assertEqual(len(child['training_roi_hashes']),10)
                self.assertEqual(len(child['test_roi_hashes']),10)
                self.assertEqual({int(r['node_id']) for r in fixture.fixtures.read(out/app/'frozen_hardware.csv')},{0,last})
            for row in fixture.fixtures.read(out/'images_all.csv'):
                self.assertTrue(row['path'].startswith(row['application']+'/'))
                self.assertTrue((out/row['path']).is_file())
            self.assertEqual(len(fixture.fixtures.read(out/'best_per_image.csv')),30)

if __name__=='__main__':unittest.main()
