import argparse
import csv
import json
from pathlib import Path
import tempfile
import unittest

import numpy as np
from PIL import Image

import union_frequency_rectangles as m


class UnionFrequencyTests(unittest.TestCase):
    def test_exact_count_threshold_and_empty_rectangle(self):
        counts = np.array([[612, 611], [0, 850]], dtype=np.uint32)
        mask, rectangle, row = m.threshold_rectangle(counts, 850, 72)
        np.testing.assert_array_equal(mask, [[True, False], [False, True]])
        self.assertTrue(rectangle.all())
        self.assertEqual(row['bbox_half_open'], [0, 0, 2, 2])
        self.assertEqual(row['fill_ratio'], .5)
        mask, rectangle, row = m.threshold_rectangle(np.zeros((2, 2), int), 850, 90)
        self.assertFalse(mask.any() or rectangle.any())
        self.assertIsNone(row['bbox_half_open'])
        self.assertIsNone(row['rectangle_area_fraction'])

    def test_union_adds_only_previously_missing_evidence(self):
        a = np.array([[1, 0], [0, 1]], bool)
        b = np.array([[1, 1], [0, 0]], bool)
        union, added = m.combine(a, b)
        np.testing.assert_array_equal(union, [[1, 1], [0, 1]])
        np.testing.assert_array_equal(added, [[0, 1], [0, 0]])

    def fixture(self, root):
        images = root / 'datasets/imgs/pascal'
        masks = root / 'pred_masks'
        cache = root / 'cache'
        for directory in (images, masks, cache):
            directory.mkdir(parents=True)
        for image_id in (1, 2):
            a = np.zeros((4, 6), np.uint8)
            a[:2, :2] = 255
            Image.fromarray(a).save(masks / f'{image_id}_mask.png')
            Image.fromarray(a).save(images / f'{image_id}.jpg')
            pc = np.zeros((4, 6), np.uint16)
            pc[2:, 4:] = 50000
            Image.fromarray(pc).save(cache / f'{image_id}_pc16.png')
        return argparse.Namespace(data=root, mask_dir='pred_masks', image_dir='datasets/imgs/pascal',
                                  pc_dir=cache, output=root/'result', start=1, count=2, grid=6,
                                  preview_count=1, thresholds=[50, 72, 95])

    def test_end_to_end_cached_inputs_and_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.fixture(Path(directory))
            m.run(args)
            out = args.output
            protocol = json.loads((out/'protocol.json').read_text(encoding='utf-8'))
            self.assertEqual(protocol['status'], 'completed')
            counts = np.load(out/'frequencies/union_count.npy')
            a = np.load(out/'frequencies/u2net_count.npy')
            added = np.load(out/'frequencies/added_count.npy')
            np.testing.assert_array_equal(counts, a+added)
            self.assertTrue((counts>=a).all())
            with np.load(out/'pc_responses/1.npz') as saved:
                self.assertEqual(saved['pc'].shape, (4, 6))
                self.assertAlmostEqual(float(saved['pc'].max()), 50000/65535, places=6)
            with (out/'rectangles.csv').open(encoding='utf-8-sig') as handle:
                rows=list(csv.DictReader(handle))
            self.assertEqual(len(rows), 9)
            u=next(row for row in rows if row['method']=='union' and row['frequency_percent']=='72')
            self.assertEqual(json.loads(u['bbox_half_open']), [0,0,6,6])
            self.assertEqual(u['component_count'], '2')
            for threshold in (50,72,95):
                for name in ('u2net','pc','union'):
                    for suffix in ('region_mask','rectangle_mask','preview'):
                        self.assertTrue((out/f'thresholds/{name}/{threshold:02d}_{suffix}.png').is_file())
            self.assertTrue((out/'union_overview.png').is_file())
            self.assertTrue((out/'examples/1.png').is_file())
            with self.assertRaises(ValueError):
                m.run(args)

    def test_rejects_bad_config_and_invalid_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            args=self.fixture(Path(directory))
            args.thresholds=[0]
            with self.assertRaises(ValueError): m.run(args)
            args.thresholds=[72]
            args.output=args.pc_dir/'out'
            with self.assertRaises(ValueError): m.run(args)
            args.output=args.data/'result'
            Image.fromarray(np.zeros((4,6),np.uint8)).save(args.pc_dir/'1_pc16.png')
            with self.assertRaises(ValueError): m.run(args)
            self.assertEqual(json.loads((args.output/'protocol.json').read_text())['status'],'failed')

    def test_default_percentages(self):
        self.assertEqual(m.DEFAULT_THRESHOLDS, sorted([*range(10,100,5),72]))

    def test_equal_image_and_pooled_coverage_are_distinct(self):
        with tempfile.TemporaryDirectory() as directory:
            args=self.fixture(Path(directory))
            Image.fromarray(np.zeros((4,6),np.uint16)).save(args.pc_dir/'2_pc16.png')
            m.run(args)
            rows=json.loads((args.output/'rectangles.json').read_text())
            row=next(r for r in rows if r['method']=='u2net' and r['frequency_percent']==72)
            self.assertAlmostEqual(row['mean_union_coverage_nonempty_images'],.75)
            self.assertAlmostEqual(row['pooled_union_coverage'],2/3)

    def test_missing_input_and_dimension_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            args=self.fixture(Path(directory))
            image_path=args.data/'datasets/imgs/pascal/1.jpg'
            image_path.unlink()
            with self.assertRaises(ValueError): m.run(args)
            self.assertFalse(args.output.exists())
            Image.new('L',(3,3)).save(image_path)
            with self.assertRaises(ValueError): m.run(args)
            self.assertEqual(json.loads((args.output/'protocol.json').read_text())['status'],'failed')

    def test_real_pc_computation_and_constant_cache(self):
        with tempfile.TemporaryDirectory() as directory:
            args=self.fixture(Path(directory))
            (args.pc_dir/'1_pc16.png').unlink()
            Image.fromarray(np.zeros((4,6),np.uint16)).save(args.pc_dir/'2_pc16.png')
            m.run(args)
            with (args.output/'per_image.csv').open(encoding='utf-8-sig') as handle:
                rows=list(csv.DictReader(handle))
            self.assertEqual(rows[0]['pc_source'],'computed')
            self.assertEqual(rows[1]['pc_otsu_threshold'],'')
            self.assertEqual(float(rows[1]['pc_native_area_fraction']),0)


if __name__=='__main__':
    unittest.main()
