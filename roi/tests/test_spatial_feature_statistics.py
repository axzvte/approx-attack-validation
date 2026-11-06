import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

import spatial_feature_statistics as stats


class SpatialStatisticsTests(unittest.TestCase):
    def test_padding_does_not_count_as_background(self):
        wide, valid_wide = stats.sample_mask(np.ones((2, 4), bool), 4, 'letterbox')
        square, valid_square = stats.sample_mask(np.zeros((4, 4), bool), 4, 'letterbox')
        frequency, counts = stats.frequency_map([wide, square], [valid_wide, valid_square])
        np.testing.assert_array_equal(counts[:, 0], [1, 2, 2, 1])
        np.testing.assert_array_equal(frequency[:, 0], [0, .5, .5, 0])

    def test_stretch_and_source_pixel_centers(self):
        mask = np.array([[1, 0]], bool)
        mapped, valid = stats.sample_mask(mask, 4, 'stretch')
        np.testing.assert_array_equal(mapped, np.tile([1, 1, 0, 0], (4, 1)))
        self.assertTrue(valid.all())

    def test_unsupported_cells_are_nan(self):
        mask, valid = stats.sample_mask(np.ones((1, 4), bool), 4, 'letterbox')
        frequency, counts = stats.frequency_map([mask], [valid])
        self.assertTrue(np.isnan(frequency[counts == 0]).all())

    def test_agreement_and_empty_thresholds(self):
        a = np.array([[0., .5], [1., np.nan]])
        b = np.array([[0., 1.], [.5, np.nan]])
        result = stats.compare_maps(a, b)
        self.assertAlmostEqual(result['mae'], 1 / 3)
        self.assertAlmostEqual(result['pearson_r'], .5)
        self.assertEqual(result['iou_ge_0.50'], 1)
        empty = stats.compare_maps(np.zeros((2, 2)), np.zeros((2, 2)))
        self.assertIsNone(empty['pearson_r'])
        self.assertIsNone(empty['iou_ge_0.50'])

    def test_no_common_support(self):
        result = stats.compare_maps(np.array([[np.nan, 1]]), np.array([[1, np.nan]]))
        self.assertEqual(result['common_cells'], 0)
        self.assertIsNone(result['mae'])

    def test_splits_are_reproducible_disjoint_and_complete(self):
        first = stats.half_splits(7, 5, 2026)
        self.assertEqual(first, stats.half_splits(7, 5, 2026))
        for left, right in first:
            self.assertEqual(len(left), 3)
            self.assertEqual(len(right), 4)
            self.assertFalse(set(left) & set(right))
            self.assertEqual(sorted(left + right), list(range(7)))

    def test_binary_modes_and_reject_grayscale(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'mask.png'
            for values in ([0, 1], [0, 255]):
                Image.fromarray(np.array([values], np.uint8)).save(path)
                np.testing.assert_array_equal(stats.read_mask(path), [[False, True]])
            Image.fromarray(np.array([[False, True]])).save(path)
            self.assertTrue(stats.read_mask(path)[0, 1])
            for array in (np.array([[0, 100]], np.uint8), np.zeros((2, 2, 3), np.uint8),
                          np.array([[0, 1, 255]], np.uint8)):
                Image.fromarray(array).save(path)
                with self.assertRaises(ValueError):
                    stats.read_mask(path)

    def make_dataset(self, root):
        images = root / 'datasets/imgs/pascal'
        masks = root / 'pred_masks'
        images.mkdir(parents=True)
        masks.mkdir()
        for index, fraction in enumerate([0, 1, 2, 4], 1):
            array = np.zeros((4, 4), np.uint8)
            array[:fraction] = 255
            Image.fromarray(array).save(masks / f'{index}_mask.png')
            Image.fromarray(array).save(images / f'{index}.jpg')

    def run_cli(self, root, output, *extra):
        return subprocess.run([sys.executable, str(Path(stats.__file__)), '--data', str(root),
                               '--count', '4', '--grid', '8', '--repeats', '2',
                               '--output', str(output), *extra], capture_output=True, text=True)

    def test_cli_outputs_original_areas_and_rejects_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_dataset(root)
            output = root / 'results'
            result = self.run_cli(root, output)
            self.assertEqual(result.returncode, 0, result.stderr)
            summary = json.loads((output / 'summary.json').read_text(encoding='utf-8'))
            self.assertEqual(summary['empty_masks'], 1)
            self.assertEqual(summary['full_masks'], 1)
            self.assertAlmostEqual(summary['source_area_fraction']['mean'], .4375)
            self.assertEqual(json.loads((output / 'protocol.json').read_text(encoding='utf-8'))['status'], 'completed')
            np.testing.assert_array_equal(np.load(output / 'all_frequency.npy')[:, 0],
                                          [.75, .75, .5, .5, .25, .25, .25, .25])
            self.assertTrue((output / 'area_distribution.png').is_file())
            self.assertTrue((output / 'splits/repeat_01_comparison.png').is_file())
            before = (output / 'summary.json').read_bytes()
            self.assertNotEqual(self.run_cli(root, output).returncode, 0)
            self.assertEqual(before, (output / 'summary.json').read_bytes())

    def test_missing_image_and_mismatched_size_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_dataset(root)
            path = root / 'datasets/imgs/pascal/4.jpg'
            path.unlink()
            self.assertNotEqual(self.run_cli(root, root / 'missing').returncode, 0)
            Image.new('L', (2, 2)).save(path)
            self.assertNotEqual(self.run_cli(root, root / 'mismatch').returncode, 0)

    def test_bad_counts_and_output_inside_input_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_dataset(root)
            self.assertNotEqual(self.run_cli(root, root / 'bad', '--count', '1').returncode, 0)
            self.assertNotEqual(self.run_cli(root, root / 'pred_masks/out').returncode, 0)


if __name__ == '__main__':
    unittest.main()
