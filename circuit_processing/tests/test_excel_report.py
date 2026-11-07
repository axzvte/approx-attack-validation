import importlib.util
import math
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from excel_report import export_psnr_workbook

NS = {'s': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}


class ExcelReportTests(unittest.TestCase):
    def row(self, name, image, values, status='completed'):
        keys = ('baseline_global_psnr', 'processed_global_psnr', 'baseline_roi_psnr', 'processed_roi_psnr')
        return dict(application=name, image=image, status=status, **dict(zip(keys, values)))

    def test_each_application_has_numeric_columns_and_cached_average(self):
        rows = [self.row('sharpen', '1.jpg', [34, 36, 35, 24]),
                self.row('conv', '1.jpg', [32, 34, 33, 22]),
                self.row('sharpen', '2.jpg', [36, 38, 37, 26])]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'psnr_results.xlsx'
            export_psnr_workbook(rows, path)
            with zipfile.ZipFile(path) as workbook:
                self.assertIsNone(workbook.testzip())
                meta = ET.fromstring(workbook.read('xl/workbook.xml'))
                self.assertEqual([s.attrib['name'] for s in meta.findall('s:sheets/s:sheet', NS)], ['sharpen', 'conv'])
                sheet = ET.fromstring(workbook.read('xl/worksheets/sheet1.xml'))
                cells = {c.attrib['r']: c for c in sheet.findall('.//s:c', NS)}
                self.assertEqual(cells['B4'].find('s:v', NS).text, '34')
                self.assertEqual(cells['E5'].find('s:v', NS).text, '26')
                self.assertEqual(float(cells['B6'].find('s:v', NS).text), 35)
                self.assertIsNotNone(cells['B6'].find('s:f', NS))
                self.assertEqual(sheet.find('s:sheetViews/s:sheetView/s:pane', NS).attrib['ySplit'], '3')

    def test_infinity_failure_and_long_case_colliding_sheet_names(self):
        name = 'a'*40
        rows = [self.row(name, '1.jpg', ['inf', 32, 'inf', 20]),
                self.row(name, '2.jpg', [0, 0, 0, 0], status='failed'),
                self.row(name.upper(), '1.jpg', [30, 32, 31, 20])]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'report.xlsx'
            export_psnr_workbook(rows, path)
            with zipfile.ZipFile(path) as workbook:
                meta = ET.fromstring(workbook.read('xl/workbook.xml'))
                names = [s.attrib['name'] for s in meta.findall('s:sheets/s:sheet', NS)]
                self.assertTrue(all(len(name) <= 31 for name in names))
                self.assertEqual(len(set(name.lower() for name in names)), 2)
                sheet = ET.fromstring(workbook.read('xl/worksheets/sheet1.xml'))
                cells = {c.attrib['r']: c for c in sheet.findall('.//s:c', NS)}
                self.assertEqual(cells['B4'].find('s:is/s:t', NS).text, '∞')
                self.assertNotIn('B5', cells)
                self.assertEqual(cells['B6'].find('s:v', NS).text, '∞')
                self.assertEqual(float(cells['C6'].find('s:v', NS).text), 32)


if __name__ == '__main__':
    unittest.main()
