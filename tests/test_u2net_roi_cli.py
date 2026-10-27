"""Real train/fixed-H CLI checks with generated PNGs; no model or user data."""
import csv
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def png(path, pixels, width=16, height=16):
    def chunk(kind, payload):
        return struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(kind+payload)&0xffffffff)
    payload=b''.join(b'\0'+bytes(pixels[y*width:(y+1)*width]) for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0,0,0,0))+
                     chunk(b'IDAT',zlib.compress(payload))+chunk(b'IEND',b''))


def read(path):
    with path.open(newline='') as stream:return list(csv.DictReader(stream))


def run(command, ok=True):
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    assert (result.returncode==0)==ok,(command,result.stdout,result.stderr)
    return result


def main():
    train,fixed,legacy_fixed=map(lambda x:Path(x).resolve(),sys.argv[1:])
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp);data=root/'data';roi=root/'roi';roi.mkdir()
        for image_id in range(1,11):
            folder=data/'stage1/input';folder.mkdir(parents=True,exist_ok=True)
            png(folder/f'image_{image_id:02d}.jpg',[110+(x*3+y*5+x*y)%30 for y in range(16) for x in range(16)])
            png(roi/f'image_{image_id:02d}_mask.png',[255 if x<image_id+2 else 0 for y in range(16) for x in range(16)])
        # No shared mask and no held-out image exists.
        training=[train,'sharpen',data,root/'train','--roi-dir',roi,'--max-evaluations',10,
                  '--max-candidates',1,'--initial-candidates',1,'--workers',1,'--context-budget',4,
                  '--boundary-budget',2,'--beam',1,'--restarts',1,'--refinement',0,'--global-psnr',0]
        run(training)
        pairs=read(root/'train/roi_inputs.csv')
        assert [int(r['image']) for r in pairs]==list(range(1,11))
        assert [int(r['roi_pixels']) for r in pairs]==[(i+2)*16 for i in range(1,11)]
        assert len({r['roi_path'] for r in pairs})==10
        notes=(root/'train/RUN.txt').read_text();assert 'roi_mode=per_image' in notes
        hardware=root/'H.csv';hardware.write_text('node_id,monitor,bit\n0,input1,1\n3,input1,1\n')
        options=['--hardware',hardware,'--roi-dir',roi,'--image',1,'--context-budget',4,
                 '--boundary-budget',2,'--beam',1,'--restarts',1,'--refinement',0,'--image-mode','none','--global-psnr',0]
        run([fixed,'sharpen',data,root/'fixed',*options])
        assert f'roi={roi}/image_01_mask.png' in (root/'fixed/RUN.txt').read_text()
        explicit=root/'custom_mask.png';png(explicit,[1 if x>=8 else 0 for y in range(16) for x in range(16)])
        direct=[str(x) for x in options];k=direct.index('--roi-dir');direct[k:k+2]=['--roi-mask',str(explicit)]
        run([fixed,'sharpen',data,root/'direct',*direct])
        assert 'roi_mode=per_image' in (root/'direct/RUN.txt').read_text()
        run([legacy_fixed,data,root/'legacy_entry',*options])
        # Invalid per-image masks must fail even if a historical shared mask exists.
        (data/'mask').mkdir();png(data/'mask/roi_mask.jpg',[255 if x<8 else 0 for y in range(16) for x in range(16)])
        target=roi/'image_01_mask.png'
        for number,kind in enumerate(('missing','size','nonbinary','empty','full')):
            if kind=='missing':target.unlink()
            else:png(target,[127 if kind=='nonbinary' else 255 if kind=='full' else 0]*(4 if kind=='size' else 256),
                     2 if kind=='size' else 16,2 if kind=='size' else 16)
            run([fixed,'sharpen',data,root/f'bad{number}',*options],ok=False)
            broken=list(training);broken[3]=root/f'badtrain{number}';run(broken,ok=False)
        run([fixed,'sharpen',data,root/'ambiguous',*options,'--roi-mask',explicit],ok=False)
        print('U2-Net ROI train/fixed-H native integration passed')


if __name__=='__main__':main()
