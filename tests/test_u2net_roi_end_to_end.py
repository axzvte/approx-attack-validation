"""Synthetic cross-repository smoke run; not user research data."""
import argparse
import csv
import json
from pathlib import Path
import sys
from PIL import Image

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--u2net-dir',required=True,type=Path)
parser.add_argument('--build-dir',required=True,type=Path)
parser.add_argument('--output',required=True,type=Path)
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(args.u2net_dir.resolve()))
from export_attack_dataset import export_dataset
sys.path.insert(0,str(root/'scripts'))
import run_circuit_bitflip_ten_train as runner

runroot=args.output.resolve();runroot.mkdir(parents=True,exist_ok=False)
source=runroot/'source';source.mkdir()
rows=[]
for i in [*range(1,11),*range(31,41)]:
    image=source/f'source_{i}.png';mask=source/f'mask_{i}.png'
    pic=Image.new('L',(16,16));pic.putdata([110+(x*3+y*5+x*y+i)%30 for y in range(16) for x in range(16)]);pic.save(image)
    pic=Image.new('L',(16,16));pic.putdata([1 if x<(i%10)+3 else 0 for y in range(16) for x in range(16)]);pic.save(mask)
    rows.append((i,str(image),str(mask)))
manifest=runroot/'pairs.csv'
with manifest.open('x',newline='') as f:
    writer=csv.writer(f);writer.writerow(['image_id','image','mask']);writer.writerows(rows)
data=runroot/'prepared';export_dataset(manifest,data)
build=args.build_dir.resolve();out=runroot/'run'
code=runner.main([str(data),str(out),'--application','sharpen','--skip-build',
                 '--train-exe',str(build/'circuit_bitflip_ten_train'),'--fixed-exe',str(build/'circuit_bitflip_fixed'),
                 '--max-train-evaluations','30','--max-candidates','2','--initial-candidates','1',
                 '--proposals-per-round','1','--max-rounds','1','--workers','1',
                 '--train-context-budget','8','--context-budget','8','--boundary-budget','4',
                 '--beam','1','--restarts','1','--refinement','0','--max-full-evals','16',
                 '--full-candidates','1','--global-psnr','0','--image-mode','none'])
meta=json.loads((out/'RUN.json').read_text())
print(json.dumps({k:meta.get(k) for k in ('completed','all_succeeded','selection_status','test_executed','failed_tasks','actual_train_evaluations','seed_bank_unchanged_after_test')},indent=2))
assert code==0 and meta['completed'] and meta['test_executed'] and meta['all_succeeded']
assert len(meta['training_roi_hashes'])==len(meta['test_roi_hashes'])==10
print('Synthetic exporter -> real ten-image training -> real fixed-H test calibration passed')
