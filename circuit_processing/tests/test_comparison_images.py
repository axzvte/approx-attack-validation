import math
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from comparison_images import save_comparison


class ComparisonImageTests(unittest.TestCase):
    def test_three_original_images_and_three_psnr_values(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            colors = ((30, 40, 50), (120, 130, 140), (220, 230, 240))
            for name, color in zip(('exact', 'baseline', 'processed'), colors):
                Image.new('RGB', (24, 16), color).save(root/f'{name}.png')
            path = root/'comparison.png'
            row = dict(application='sharpen_mixed', image='7.jpg', baseline_global_psnr=35.12345,
                       processed_global_psnr=36.6789, processed_roi_psnr=24.555)
            save_comparison(root, path, row)
            with Image.open(path) as image:
                self.assertEqual(image.mode, 'RGB')
                pixels = np.asarray(image)
                for color in colors:
                    self.assertEqual(int(np.all(pixels == color, axis=2).sum()), 24*16)
                self.assertEqual(image.info['application'], 'sharpen_mixed')
                self.assertEqual(image.info['image'], '7.jpg')
                for key in ('baseline_global_psnr', 'processed_global_psnr', 'processed_roi_psnr'):
                    self.assertEqual(float(image.info[key]), row[key])

    def test_infinity_and_dimension_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ('exact', 'baseline', 'processed'):
                Image.new('L', (16, 16), 0).save(root/f'{name}.png')
            row = dict(application='test', image='850.jpg', baseline_global_psnr='inf',
                       processed_global_psnr=32, processed_roi_psnr=20)
            save_comparison(root, root/'comparison.png', row)
            with Image.open(root/'comparison.png') as image:
                self.assertTrue(math.isinf(float(image.info['baseline_global_psnr'])))
            Image.new('L', (17, 16), 0).save(root/'processed.png')
            with self.assertRaises(ValueError):
                save_comparison(root, root/'invalid.png', row)
            self.assertFalse((root/'invalid.png').exists())


if __name__ == '__main__':
    unittest.main()
