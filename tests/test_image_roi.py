"""Strict offline pair contract; generated images only."""
import sys
from pathlib import Path
import tempfile
import unittest

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from image_roi import resolve_roi_path, validate_pair


class ImageRoiTests(unittest.TestCase):
    def fixture(self, root, values=(0, 255), size=(8, 6), mode='L'):
        image = root / 'image.jpg'; mask = root / 'image_01_mask.png'
        Image.new('RGB', (8, 6), (120, 130, 140)).save(image)
        pic = Image.new(mode, size)
        pic.putdata([values[(x + y) % len(values)] for y in range(size[1]) for x in range(size[0])])
        pic.save(mask)
        return image, mask

    def test_pair_reports_dimensions_counts_and_hashes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); image, mask = self.fixture(root)
            row = validate_pair(image, mask)
            self.assertEqual((row['width'], row['height'], row['roi_pixels']), (8, 6, 24))
            self.assertEqual(row['roi_fraction'], .5)
            self.assertEqual(len(row['mask_sha256']), 64)
            self.assertEqual(resolve_roi_path(root, 1), mask)

    def test_accepts_zero_one_and_pillow_one_bit(self):
        for mode in ('L', '1'):
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as tmp:
                self.assertEqual(validate_pair(*self.fixture(Path(tmp), (0, 1), mode=mode))['roi_pixels'], 24)

    def test_rejects_wrong_size_nonbinary_empty_full_rgb_and_lossy(self):
        cases = [dict(size=(4, 3)), dict(values=(0, 127, 255)), dict(values=(0,)),
                 dict(values=(255,)), dict(values=(0, 1, 255)), dict(mode='RGB', values=((0,0,0),(255,255,255)))]
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(ValueError): validate_pair(*self.fixture(Path(tmp), **case))
        with tempfile.TemporaryDirectory() as tmp:
            image, mask = self.fixture(Path(tmp)); lossy = mask.with_suffix('.jpg')
            Image.open(mask).save(lossy)
            with self.assertRaises(ValueError): validate_pair(image, lossy)

    def test_no_shared_mask_fallback(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); self.fixture(root)
            with self.assertRaises(FileNotFoundError): resolve_roi_path(root, 2)

    def test_changing_only_mask_changes_pair_identity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); image, mask = self.fixture(root); before = validate_pair(image, mask)
            pic = Image.open(mask); pic.putpixel((0, 0), 255); pic.save(mask)
            after = validate_pair(image, mask)
            self.assertEqual(before['image_sha256'], after['image_sha256'])
            self.assertNotEqual(before['mask_sha256'], after['mask_sha256'])


if __name__ == '__main__': unittest.main()
