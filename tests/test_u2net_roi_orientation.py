"""JPEG EXIF must not transform strict-mode input away from offline U2-Net ROI."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
from PIL import Image

exe=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory() as tmp:
    root=Path(tmp);data=root/'data';data.mkdir()
    hardware=root/'H.csv';hardware.write_text('node_id,monitor,bit\n0,input1,1\n3,input1,1\n')
    image=Image.new('L',(16,12));image.putdata([110+(3*x+5*y+x*y)%30 for y in range(12) for x in range(16)])
    mask=root/'mask.png';pic=Image.new('L',image.size)
    pic.putdata([255 if x<5 and y<7 else 0 for y in range(12) for x in range(16)]);pic.save(mask)
    hashes=[]
    for orientation in (1,2,3,6):
        source=root/f'orient{orientation}.jpg';exif=Image.Exif();exif[274]=orientation
        image.save(source,quality=95,exif=exif)
        output=root/f'run{orientation}'
        result=subprocess.run(list(map(str,[exe,'sharpen',data,output,'--hardware',hardware,
                              '--input-image',source,'--roi-mask',mask,'--image',41,
                              '--context-budget',4,'--beam',1,'--refinement',0,'--restarts',1,
                              '--boundary-budget',2,'--global-psnr',0,'--image-mode','full'])),capture_output=True,text=True)
        assert result.returncode==0,(orientation,result.stdout,result.stderr)
        hashes.append(tuple(hashlib.sha256((output/name).read_bytes()).hexdigest() for name in ('exact.png','baseline.png','roi_mask.png')))
    assert len(set(hashes))==1,'EXIF silently rotated/reflected strict ROI input'
print('EXIF orientation 1/2/3/6 preserves raw U2-Net coordinate alignment')
