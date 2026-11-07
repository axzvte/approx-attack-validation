"""Integration tests run after cmake --build circuit_processing/build."""
import csv
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
PIPELINE = ROOT / 'scripts/fixed_roi_pipeline.py'
BINARY = Path(os.environ.get('FIXED_ROI_VERIFY', ROOT / 'build/fixed_roi_verify'))


@unittest.skipUnless(BINARY.is_file(), 'Build fixed_roi_verify first')
class ProfileExecutionTests(unittest.TestCase):
    def run_command(self, *args):
        command = list(map(str, args))
        if len(command) > 2 and command[1] == str(PIPELINE) and command[2] == 'verify':
            command.extend(['--binary', str(BINARY)])
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        return result

    def test_custom_graph_exact_reference_and_region_gating(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            image = np.random.default_rng(7).integers(0, 256, (32, 40), dtype=np.uint8)
            path = root / 'input.png'
            Image.fromarray(image).save(path)
            profile = ROOT / 'configs/circuits/custom_sharpen.json'
            self.run_command(BINARY, 'graph', path, root, 10, 8, 20, 16, 3, 4,
                             0, 1, 2, '--circuit', profile)
            padded = np.pad(image.astype(np.int32), 1, mode='edge')
            expected = np.clip(5*padded[1:-1, 1:-1] - padded[:-2, 1:-1]
                               - padded[2:, 1:-1] - padded[1:-1, :-2] - padded[1:-1, 2:], 0, 255)
            exact = np.asarray(Image.open(root/'exact.png'))
            np.testing.assert_array_equal(exact, expected)
            processed = np.asarray(Image.open(root/'processed.png'))
            compensated = np.asarray(Image.open(root/'compensated.png'))
            mask = np.zeros(image.shape, bool)
            mask[8:16, 10:20] = True
            np.testing.assert_array_equal(processed[~mask], compensated[~mask])
            self.assertTrue(np.any(processed[mask] != compensated[mask]))

    def test_mixed_profiles_freeze_execute_and_reject_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            roi = root/'roi'
            roi.mkdir()
            (roi/'protocol.json').write_text(json.dumps(dict(status='completed', feature='rc',
                coordinate_mode='stretch', grid=320, count=850)))
            (roi/'rectangles.json').write_text(json.dumps([dict(method='union', frequency_percent=85,
                bbox_half_open=[120, 135, 193, 201])]))
            images = root/'inputs'
            images.mkdir()
            Image.fromarray(np.random.default_rng(4).integers(0, 256, (320, 320), dtype=np.uint8)).save(images/'1.jpg')
            config = root/'frozen.json'
            self.run_command(sys.executable, PIPELINE, 'freeze', '--roi-run', roi,
                '--circuit', ROOT/'configs/circuits/sharpen_mixed.json',
                '--circuit', ROOT/'configs/circuits/custom_sharpen.json', '--output', config)
            frozen = json.loads(config.read_text())
            self.assertEqual(set(frozen['input_circuits']), {'sharpen_mixed', 'custom_sharpen'})
            self.run_command(sys.executable, PIPELINE, 'verify', '--config', config,
                '--images', images, '--count', 1, '--output', root/'verified')
            summary = json.loads((root/'verified/summary.json').read_text())
            self.assertTrue(all(row['global_pass'] == 1 for row in summary['applications'].values()))
            frozen['input_circuits']['sharpen_mixed']['baseline'][0]['unit'] = '5Z0'
            config.write_text(json.dumps(frozen))
            result = subprocess.run([sys.executable, str(PIPELINE), 'verify', '--config', str(config),
                '--images', str(images), '--count', '1', '--output', str(root/'tampered')], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((root/'tampered').exists())


if __name__ == '__main__':
    unittest.main()
