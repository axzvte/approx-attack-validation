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


if __name__ == '__main__': unittest.main()
