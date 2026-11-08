"""All four applications enter the same source/IR/selection/execution workflow."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
PIPELINE = ROOT/'scripts/fixed_roi_pipeline.py'
BINARY = Path(os.environ.get('FIXED_ROI_VERIFY', ROOT/'build/fixed_roi_verify'))


@unittest.skipUnless(BINARY.is_file(), 'Build the native verifier first')
class UnifiedWorkflowTests(unittest.TestCase):
    def test_four_source_backed_profiles_share_selector_and_native_ir(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            roi=root/'roi'
            roi.mkdir()
            (roi/'protocol.json').write_text(json.dumps(dict(status='completed',feature='rc',coordinate_mode='stretch',grid=320,count=850)))
            (roi/'rectangles.json').write_text(json.dumps([dict(method='union',frequency_percent=85,bbox_half_open=[120,135,193,201])]))
            freeze=[sys.executable,str(PIPELINE),'freeze','--roi-run',str(roi),'--output',str(root/'frozen.json')]
            for app in ('sharpen','conv','sobel','dct'):
                freeze.extend(['--circuit',str(ROOT/f'configs/circuits/{app}_mixed.json')])
            result=subprocess.run(freeze,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            config=json.loads((root/'frozen.json').read_text())
            self.assertEqual(config['schema'],3)
            for name,profile in config['input_circuits'].items():
                self.assertEqual(profile['application'],'ir')
                self.assertEqual(profile['source']['frontend'],'pycparser')
                self.assertEqual(config['circuits'][name]['selection_method'],'structural_greedy_v1')
                self.assertLessEqual(config['circuits'][name]['analysis_evaluations'],512)
                self.assertNotIn('examined_subsets',config['circuits'][name])
            self.assertGreater(len(config['input_circuits']['dct_mixed']['ir']['operations']),1000)
            images=root/'inputs'
            images.mkdir()
            Image.fromarray(np.random.default_rng(24).integers(0,256,(160,192),dtype=np.uint8)).save(images/'1.jpg')
            result=subprocess.run([sys.executable,str(PIPELINE),'verify','--config',str(root/'frozen.json'),
                '--images',str(images),'--count','1','--binary',str(BINARY),'--output',str(root/'verify')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr+result.stdout)
            summary=json.loads((root/'verify/summary.json').read_text())
            self.assertFalse(summary['failures'])
            self.assertEqual(len(summary['applications']),4)
            self.assertTrue(all(app['completed']==1 and app['global_pass']==1 for app in summary['applications'].values()))
            self.assertEqual(len(list((root/'verify/comparisons').glob('*.png'))),4)
            self.assertTrue((root/'verify/psnr_results.xlsx').is_file())


if __name__=='__main__':
    unittest.main()
