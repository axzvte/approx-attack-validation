from pathlib import Path
import tempfile
import unittest

import numpy as np
from PIL import Image

import mask_utils as stats


class MaskUtilsTests(unittest.TestCase):
    def test_stretch_and_source_pixel_centers(self):
        mask = np.array([[1, 0]], bool)
        mapped, valid = stats.sample_mask(mask, 4, 'stretch')
        np.testing.assert_array_equal(mapped, np.tile([1, 1, 0, 0], (4, 1)))
        self.assertTrue(valid.all())

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


if __name__ == '__main__':
    unittest.main()
