import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import run_u2net_other_apps as launcher

class LauncherTests(unittest.TestCase):
    def fixture(self,root):
        source=root/'source';source.mkdir();data=root/'data';data.mkdir();build=root/'build';build.mkdir()
        (source/'RUN.json').write_text(json.dumps(dict(completed=True,all_succeeded=True,roi_mode='per-image',data=str(data))))
        for name in ('circuit_bitflip_ten_train','circuit_bitflip_fixed'):(build/name).write_text('fixture')
        roi=data/'roi';roi.mkdir()
        for i in [*range(1,11),*range(31,41)]:
            folder=data/f'stage{(i-1)//10+1}/input';folder.mkdir(parents=True,exist_ok=True)
            (folder/f'image_{i:02d}.jpg').write_bytes(b'not decoded during preflight')
            (roi/f'image_{i:02d}_mask.png').write_bytes(b'not decoded during preflight')
        return source,data,build
    def test_preflight_requires_complete_per_image_run_and_fresh_separate_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source,data,build=self.fixture(root);out=root/'new'
            plan=launcher.preflight(source,out,build)
            self.assertEqual(plan['applications'],['conv','sobel','dct']);self.assertFalse(out.exists())
            for bad in (source/'out',data/'out',build/'out',root/'sources/out'):
                with self.assertRaises(ValueError):launcher.preflight(source,bad,build)
            out.mkdir()
            with self.assertRaises(FileExistsError):launcher.preflight(source,out,build)
            (source/'RUN.json').write_text(json.dumps(dict(completed=True,all_succeeded=True,roi_mode='legacy-fixed',data=str(data))))
            with self.assertRaises(ValueError):launcher.preflight(source,root/'other',build)
    def test_worker_forwards_three_apps_and_full_images_and_records_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);source,data,build=self.fixture(root);out=root/'new';out.mkdir()
            plan=launcher.preflight(source,root/'unused',build);plan['output']=str(out)
            with patch.object(launcher.subprocess,'run') as run:
                run.return_value.returncode=1
                self.assertEqual(launcher.worker(plan),1)
            command=list(map(str,run.call_args.args[0]))
            self.assertEqual(command[command.index('--applications')+1],'conv,sobel,dct')
            self.assertEqual(command[command.index('--image-mode')+1],'full')
            self.assertEqual(command[command.index('--roi-dir')+1],str(data/'roi'))
            self.assertIn('FAILED',(out/'STATUS.txt').read_text())
if __name__=='__main__':unittest.main()
