"""Pair provenance, forwarding, held-out isolation, and cache invalidation."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image

sys.path.insert(0,str(Path(__file__).resolve().parent))
import test_circuit_bitflip_ten_train_pipeline as legacy
import test_circuit_bitflip_hardware_pipeline as fixtures
runner=legacy.runner


class U2netPipelineTests(unittest.TestCase):
    def fixture(self,root):
        data,fixed,train=legacy.TenTrainPipelineTests().fixture(root)
        roi=data/'roi';roi.mkdir()
        for image_id in [*range(1,11),*range(31,41)]:
            image=fixtures.protocol.resolve_staged_image_path(data,image_id)
            Image.new('L',(16,16),120).save(image)
            mask=Image.new('L',(16,16));mask.putdata([255 if x<(image_id%10)+2 else 0 for y in range(16) for x in range(16)])
            mask.save(roi/f'image_{image_id:02d}_mask.png')
        # Controlled solver must report receiving the new native mode.
        code=train.read_text()
        code+="\nwith (out/'RUN.txt').open('a') as stream:stream.write('roi_mode=per_image\\nroi_dir='+opts['--roi-dir']+'\\n')\n"
        train.write_text(code)
        code=fixed.read_text()+"\n# ROI contract\nwith (out/'RUN.txt').open('a') as stream:stream.write('roi_mode=per_image\\nroi='+opts['--roi-mask']+'\\n')\n"
        fixed.write_text(code)
        (data/'mask/roi_mask.jpg').unlink()
        return data,fixed,train

    def args(self,data,fixed,train,out):
        return [str(data),str(out),'--application','sharpen','--skip-build','--train-exe',str(train),'--fixed-exe',str(fixed)]

    def test_default_uses_per_image_masks_and_masks_invalidate_correct_tasks(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root);out=root/'run'
            commands=[];original=runner.process
            def record(command,*a,**kw):commands.append(list(map(str,command)));return original(command,*a,**kw)
            with patch.object(runner,'process',side_effect=record):
                self.assertEqual(runner.main(self.args(data,fixed,train,out)),0)
            self.assertIn('--roi-dir',commands[0])
            meta=json.loads((out/'RUN.json').read_text())
            self.assertEqual(meta['roi_mode'],'per-image')
            self.assertEqual(set(meta['training_roi_hashes']),set(map(str,range(1,11))))
            self.assertEqual(set(meta['test_roi_hashes']),set(map(str,range(31,41))))
            task=next(out.glob('test/*/image_31/TASK.json'))
            self.assertIn(meta['test_roi_hashes']['31'],task.read_text())
            image=data/'roi/image_31_mask.png';mask=Image.open(image);mask.putpixel((15,15),255);mask.save(image)
            second=root/'second'
            self.assertEqual(runner.main(self.args(data,fixed,train,second)+['--resume-from',str(out)]),0)
            reused=json.loads((second/'RUN.json').read_text())
            self.assertTrue(reused['training_cache_hit']);self.assertEqual(reused['cache_hits'],10)
            self.assertEqual(meta['training_roi_hashes'],reused['training_roi_hashes'])

    def test_bad_training_mask_fails_before_solver_and_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root);out=root/'run'
            Image.new('L',(8,8),255).save(data/'roi/image_01_mask.png')
            with patch.object(runner,'process') as solve:
                self.assertEqual(runner.main(self.args(data,fixed,train,out)),1);solve.assert_not_called()
            self.assertFalse(out.exists())

    def test_identical_relocated_roi_paths_miss_cache_instead_of_failing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root);out=root/'run'
            self.assertEqual(runner.main(self.args(data,fixed,train,out)),0)
            relocated=root/'relocated';data.rename(relocated)
            second=root/'second'
            self.assertEqual(runner.main(self.args(relocated,fixed,train,second)+['--resume-from',str(out)]),0)
            meta=json.loads((second/'RUN.json').read_text())
            self.assertFalse(meta['training_cache_hit']);self.assertEqual(meta['cache_hits'],0)

    def test_test_masks_not_decoded_before_hardware_freezes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root);out=root/'run'
            (data/'roi/image_31_mask.png').write_bytes(b'invalid held-out mask')
            (root/'no_numeric').write_text('none')
            self.assertEqual(runner.main(self.args(data,fixed,train,out)),0)
            meta=json.loads((out/'RUN.json').read_text())
            self.assertFalse(meta['test_executed']);self.assertEqual(meta['test_roi_hashes'],{})

    def test_output_may_not_overlap_external_roi_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root);roi=root/'external_roi';(data/'roi').rename(roi)
            with patch.object(runner,'process') as solve:
                self.assertEqual(runner.main(self.args(data,fixed,train,roi/'result')+['--roi-dir',str(roi)]),1)
                solve.assert_not_called()

    def test_changed_training_mask_is_not_cached(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);data,fixed,train=self.fixture(root);out=root/'run';original=runner.process
            def mutate(command,*a,**kw):
                result=original(command,*a,**kw)
                mask_path=data/'roi/image_01_mask.png'
                with Image.open(mask_path) as pic:mask=pic.copy()
                mask.putpixel((15,15),255);mask.save(mask_path)
                return result
            with patch.object(runner,'process',side_effect=mutate):
                self.assertEqual(runner.main(self.args(data,fixed,train,out)),1)
            self.assertFalse((out/'train/TASK.json').exists())

    def test_interval_executable_must_confirm_roi_and_mutations_are_not_cached(self):
        import bitflip_calibration
        for kind in ('wrong_binary','mutating_mask'):
            with self.subTest(kind=kind),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp);data,fixed,train=self.fixture(root);out=root/'run'
                if kind=='wrong_binary':
                    fixed.write_text(fixed.read_text().split("\n# ROI contract")[0])
                original=bitflip_calibration.process
                def mutate(command,*a,**kw):
                    result=original(command,*a,**kw)
                    roi_path=Path(command[command.index('--roi-mask')+1])
                    with Image.open(roi_path) as pic:mask=pic.copy()
                    mask.putpixel((15,15),255);mask.save(roi_path)
                    return result
                context=patch.object(bitflip_calibration,'process',side_effect=mutate) if kind=='mutating_mask' else patch.object(bitflip_calibration,'process',side_effect=original)
                with context:self.assertEqual(runner.main(self.args(data,fixed,train,out)),1)
                self.assertFalse(list((out/'test').glob('*/image_*/TASK.json')))


if __name__=='__main__':unittest.main()
