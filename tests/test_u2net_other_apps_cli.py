"""Real native Conv/Sobel/DCT strict ROI training and fixed-H tests."""
from pathlib import Path
import sys
import tempfile
from test_u2net_roi_cli import png,read,run

train,fixed=map(lambda p:Path(p).resolve(),sys.argv[1:])
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);data=root/'data';roi=root/'roi';roi.mkdir();(data/'stage1/input').mkdir(parents=True)
    for i in range(1,11):
        png(data/f'stage1/input/image_{i:02d}.jpg',[112+(x*3+y*5+x*y)%24 for y in range(16) for x in range(16)])
        png(roi/f'image_{i:02d}_mask.png',[255 if x<i+2 else 0 for y in range(16) for x in range(16)])
    for app,last in (('conv',7),('sobel',10),('dct',31)):
        out=root/app
        command=[train,app,data,out,'--roi-dir',roi,'--max-evaluations',10,'--max-candidates',1,
                 '--initial-candidates',1,'--workers',1,'--context-budget',4,'--boundary-budget',2,
                 '--beam',1,'--restarts',1,'--refinement',0,'--global-psnr',0,'--max-full-evals',8,'--full-candidates',1]
        run(command)
        assert [int(r['roi_pixels']) for r in read(out/'roi_inputs.csv')]==[(i+2)*16 for i in range(1,11)]
        assert 'roi_mode=per_image' in (out/'RUN.txt').read_text()
        h=root/f'{app}_hardware.csv';h.write_text(f'node_id,monitor,bit\n0,input1,1\n{last},input1,1\n')
        args=['--hardware',h,'--roi-mask',roi/'image_01_mask.png','--context-budget',4,
              '--boundary-budget',2,'--beam',1,'--restarts',1,'--refinement',0,'--global-psnr',0,
              '--max-full-evals',8,'--full-candidates',1,'--image-mode','full']
        run([fixed,app,data,root/f'{app}_fixed',*args])
        assert (root/f'{app}_fixed/roi_mask.png').is_file()
        assert (root/f'{app}_fixed/exact.png').is_file()
        assert 'roi_mode=per_image' in (root/f'{app}_fixed/RUN.txt').read_text()
        bad=list(command);bad[3]=root/f'{app}_bad';bad[bad.index('--roi-dir')+1]=root/'missing'
        run(bad,ok=False)
        print(app+' native strict ROI checks passed')
