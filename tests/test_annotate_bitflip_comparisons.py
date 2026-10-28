"""Rendering-only fixtures; never execute native programs or read user datasets."""
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from PIL import Image

SCRIPT=Path(__file__).resolve().parents[1]/'scripts/annotate_bitflip_comparisons.py'

class AnnotationTests(unittest.TestCase):
    def load(self):
        self.assertTrue(SCRIPT.is_file(),'Missing the annotation-only image generator')
        spec=importlib.util.spec_from_file_location('annotator',SCRIPT)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        return module

    def fixture(self,root):
        source=root/'source';(source/'images').mkdir(parents=True)
        (source/'reports').mkdir()
        im=Image.new('L',(21,29))
        for i,value in enumerate((11,22,33)):
            im.paste(value,(i*7,24,(i+1)*7,29))
        im.save(source/'images/sharpen_01_overall.png')
        roi=Image.new('L',(18,54))
        for i,value in enumerate((11,22,33)):
            roi.paste(value,(i*6,48,(i+1)*6,54))
        roi.save(source/'images/sharpen_01_roi.png')
        row=dict(application='sharpen',image=1,status='quality_not_met',comparison_path='images/sharpen_01_overall.png',
                 roi_comparison_path='images/sharpen_01_roi.png',baseline_global_psnr=31.23456,baseline_roi_psnr=36.12345,
                 attack_global_psnr=29.12345,attack_roi_psnr=20.98765)
        for key in ('comparison','roi_comparison'):
            row[key+'_sha256']=hashlib.sha256((source/row[key+'_path']).read_bytes()).hexdigest()
        with (source/'per_image_metrics.csv').open('w',newline='') as f:
            w=csv.DictWriter(f,fieldnames=list(row));w.writeheader();w.writerow(row)
        (source/'RUN.json').write_text(json.dumps(dict(completed=True,all_succeeded=True,
             workflow='stage6_offline_u2net_frozen_hardware_retest',completed_tasks=1,
             input_images=[dict(image=1,width=7,height=5)])))
        return source

    def test_annotation_only_preserves_pixels_flat_names_and_absolute_metrics(self):
        module=self.load()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=self.fixture(root);target=root/'annotated'
            original={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in source.rglob('*') if p.is_file()}
            self.assertEqual(module.main([str(source),str(target)]),0)
            self.assertEqual(sorted(p.name for p in target.glob('*.png')),['sharpen_01_overall.png','sharpen_01_roi.png'])
            self.assertFalse(any(p.is_dir() for p in target.iterdir()))
            manifest=json.loads((target/'annotation_manifest.json').read_text())
            self.assertIn('Global PSNR: 31.235 dB',manifest['images'][0]['labels'][1])
            self.assertIn('ROI PSNR: 36.123 dB',manifest['images'][0]['labels'][1])
            self.assertIn('Global PSNR: 29.123 dB',manifest['images'][0]['labels'][2])
            self.assertIn('quality_not_met',manifest['images'][0]['labels'][2])
            for record in manifest['images']:
                with Image.open(target/record['output']) as image:
                    width=record['panel_width'];top=record['header_height']
                    for i,value in enumerate((11,22,33)):
                        left=i*width+record['content_left']
                        crop=image.crop((left,top,left+record['content_width'],image.height))
                        self.assertEqual(set(crop.tobytes()),{value})
            self.assertEqual(original,{p:hashlib.sha256(p.read_bytes()).hexdigest() for p in original})
            self.assertEqual(module.main([str(source),str(target)]),1)

    def test_rejects_overlap_and_corrupted_source_without_output(self):
        module=self.load()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=self.fixture(root)
            self.assertEqual(module.main([str(source),str(source/'new')]),1)
            target=root/'annotated'
            (source/'images/sharpen_01_overall.png').write_bytes(b'corrupted')
            self.assertEqual(module.main([str(source),str(target)]),1)
            self.assertFalse(target.exists())

    def test_check_does_not_create_output_and_nonfinite_metrics_are_rejected(self):
        module=self.load()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=self.fixture(root);target=root/'annotated'
            self.assertEqual(module.main([str(source),str(target),'--check']),0)
            self.assertFalse(target.exists())
            report=source/'per_image_metrics.csv'
            with report.open(newline='') as f:
                rows=list(csv.DictReader(f))
            rows[0]['attack_roi_psnr']='nan'
            with report.open('w',newline='') as f:
                w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
            self.assertEqual(module.main([str(source),str(target)]),1)
            self.assertFalse(target.exists())

    def test_rejects_image_path_escape(self):
        module=self.load()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=self.fixture(root);target=root/'annotated'
            report=source/'per_image_metrics.csv'
            with report.open(newline='') as f:
                rows=list(csv.DictReader(f))
            rows[0]['comparison_path']='../outside.png'
            with report.open('w',newline='') as f:
                w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
            self.assertEqual(module.main([str(source),str(target)]),1)
            self.assertFalse(target.exists())

    def test_nonuniform_pixels_are_preserved_in_every_panel(self):
        module=self.load()
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'pattern.png';original=Image.new('L',(21,29))
            for y in range(5):
                for x in range(21):
                    original.putpixel((x,y+24),(13*x+17*y)%256)
            original.save(source)
            labels=module.label_lines(dict(baseline_global_psnr=31,baseline_roi_psnr=35,
                attack_global_psnr=30,attack_roi_psnr=22,status='numeric_eligible'))
            result,layout=module.draw_comparison(source,24,labels)
            for i in range(3):
                left=i*layout['panel_width']+layout['content_left']
                actual=result.crop((left,layout['header_height'],left+7,result.height))
                self.assertEqual(actual.tobytes(),original.crop((i*7,24,(i+1)*7,29)).tobytes())

    def test_positive_infinity_is_shown_and_negative_infinity_rejected(self):
        module=self.load()
        self.assertEqual(module.psnr('inf'),'inf')
        with self.assertRaises(ValueError):
            module.psnr('-inf')

    def test_no_legal_state_is_recorded_without_fabricating_images(self):
        module=self.load()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source=self.fixture(root);target=root/'annotated'
            report=source/'per_image_metrics.csv'
            with report.open(newline='') as f:
                rows=list(csv.DictReader(f))
            rows[0].update(status='no_legal_interval_state',comparison_path='',roi_comparison_path='')
            with report.open('w',newline='') as f:
                w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
            self.assertEqual(module.main([str(source),str(target)]),0)
            self.assertFalse(list(target.glob('*.png')))
            manifest=json.loads((target/'annotation_manifest.json').read_text())
            self.assertEqual(len(manifest['skipped']),1)

if __name__=='__main__':unittest.main()
