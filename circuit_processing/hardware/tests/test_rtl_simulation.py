"""Native-IR oracle versus the registered stencil RTL, including frame protocol."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'hardware/scripts')]
BINARY=Path(os.environ.get('FIXED_ROI_VERIFY',ROOT/'build/fixed_roi_verify'))


def run(command):
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    if result.returncode: raise AssertionError(result.stdout+result.stderr)
    return result.stdout


@unittest.skipUnless(shutil.which('iverilog') and shutil.which('vvp') and BINARY.is_file(),'Requires Icarus and built native verifier')
class StencilSimulationTests(unittest.TestCase):
    def test_three_stencils_all_versions_match_native_on_frames_and_gaps(self):
        from generate_rtl import default_configuration
        from rtl_stencil import emit_stencil,position_rtl
        from rtl_units import generate_units
        from fixed_roi_pipeline import project
        config=default_configuration()
        rng=np.random.default_rng(63)
        cases=[rng.integers(0,256,(18,22),dtype=np.uint8),
               ((np.indices((17,19))[0]*15+np.indices((17,19))[1]*7)%256).astype(np.uint8),
               np.full((1,1),127,np.uint8),np.zeros((3,2),np.uint8),np.full((18,22),128,np.uint8)]
        with tempfile.TemporaryDirectory() as directory:
            work=Path(directory)
            (work/'units.sv').write_text(generate_units(ROOT))
            (work/'position.sv').write_text(position_rtl())
            for app in ('sharpen','conv','sobel'):
                profile=config['input_circuits'][f'{app}_mixed']; plan=config['circuits'][f'{app}_mixed']
                profile_path=work/f'{app}.json'; profile_path.write_text(json.dumps(profile))
                samples=[]; expected={v:[] for v in ('baseline','compensated','processed')}
                boundaries=[]
                for index,image in enumerate(cases):
                    h,w=image.shape; offset=len(samples)
                    path=work/f'image{index}.png'; Image.fromarray(image).save(path)
                    dest=work/f'{app}_oracle{index}'; dest.mkdir()
                    box=project(config['bbox_half_open'],320,w,h)
                    if box[0]==box[2] or box[1]==box[3]:
                        # Native CLI requires a nonempty ROI. These tiny frames
                        # have no interior pixels, so neither ROI can flip.
                        assert min(w,h)<3
                        box=(0,0,1,1)
                    run([BINARY,'ir',path,dest,*box,plan['node'],plan['bit'],*plan['exact_nodes'],'--circuit',profile_path])
                    for version in expected:
                        expected[version].extend(np.asarray(Image.open(dest/f'{version}.png')).ravel().tolist())
                    for y in range(h):
                        for x in range(w):
                            packed=sum(int(image[min(h-1,max(0,y+item['dy'])),min(w-1,max(0,x+item['dx']))])<<(8*i) for i,item in enumerate(profile['ir']['inputs']))
                            samples.append(packed)
                    boundaries.append((w,h,offset,w*h))
                size=len(samples); bits=8*len(profile['ir']['inputs'])
                (work/'samples.hex').write_text('\n'.join(f'{x:0{bits//4}x}' for x in samples)+'\n')
                for version in expected:
                    with self.subTest(app=app,version=version):
                        (work/'expected.hex').write_text('\n'.join(f'{x:02x}' for x in expected[version])+'\n')
                        (work/'top.sv').write_text(emit_stencil(profile,plan,app,version,config['bbox_half_open']))
                        invocations='\n'.join(f'run_frame({w},{h},{offset},{count});' for w,h,offset,count in boundaries)
                        tb=f'''module tb;
reg clk=0; always #5 clk=~clk;
reg rst_n=0,frame_start=0,in_valid=0;
reg [15:0] frame_width=0,frame_height=0;
reg [{bits-1}:0] samples=0;
wire in_ready,out_valid; wire [7:0] pixel;
reg [{bits-1}:0] vectors[0:{size-1}]; reg [7:0] expected[0:{size-1}];
{app}_{version} dut(.*);
task run_frame;
 input integer w,h,offset,count;
 integer i;
 begin
  @(negedge clk);frame_start=1;in_valid=1;frame_width=w;frame_height=h;
  @(posedge clk);#1;if(out_valid!==0) $fatal(1,"frame_start accepted stale sample");
  @(negedge clk);frame_start=0;in_valid=0;
  for(i=0;i<count;i=i+1) begin
   if(i%7==0) begin
    @(negedge clk);in_valid=0;samples=0;
    @(posedge clk);#1;if(out_valid!==0) $fatal(1,"gap produced output");
   end
   @(negedge clk);in_valid=1;samples=vectors[offset+i];
   // Dimensions are latched at frame_start, not read continuously.
   frame_width=0;frame_height=0;
   if(in_ready!==1) $fatal(1,"unexpected input backpressure");
   @(posedge clk);#1;
   if(out_valid!==1 || pixel!==expected[offset+i])
    $fatal(1,"{app}/{version}: sample %0d got %0d expected %0d",i,pixel,expected[offset+i]);
  end
  @(negedge clk);in_valid=1;
  @(posedge clk);#1;if(out_valid!==0 || in_ready!==0) $fatal(1,"accepted past frame end");
 end
endtask
initial begin
 $readmemh("{(work/'samples.hex').as_posix()}",vectors);
 $readmemh("{(work/'expected.hex').as_posix()}",expected);
 repeat(2) @(negedge clk);rst_n=1;
 // Abort a partially consumed frame; subsequent start must reset coordinates.
 frame_start=1;frame_width=22;frame_height=18;
 @(negedge clk);frame_start=0;in_valid=1;samples=vectors[0];
 repeat(4) @(negedge clk);
 {invocations}
 $display("PASS {app}/{version}");$finish;
end
initial begin #2000000;$fatal(1,"timeout");end
endmodule
'''
                        (work/'tb.sv').write_text(tb)
                        run(['iverilog','-g2012','-s','tb','-o',work/'sim',work/'units.sv',work/'position.sv',work/'top.sv',work/'tb.sv'])
                        self.assertIn('PASS',run(['vvp',work/'sim']))

if __name__=='__main__': unittest.main()
