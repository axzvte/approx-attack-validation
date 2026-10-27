"""Supply synthetic data to the historical fixed-CLI contract test."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_u2net_roi_cli import png

with tempfile.TemporaryDirectory() as tmp:
    data=Path(tmp);(data/'stage3/input').mkdir(parents=True);(data/'mask').mkdir()
    png(data/'stage3/input/image_21.jpg',[110+(x*3+y*5+x*y)%30 for y in range(16) for x in range(16)])
    png(data/'mask/roi_mask.jpg',[255 if 4<=x<12 and 4<=y<12 else 0 for y in range(16) for x in range(16)])
    subprocess.run(['bash',str(Path(__file__).with_name('test_sharpen_bitflip_fixed_cli.sh')),sys.argv[1],str(data)],check=True)
