"""Pixel-center geometry, frame boundaries and counter acceptance tests."""
from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'hardware/scripts'))
from rtl_stencil import position_rtl

@unittest.skipUnless(shutil.which('iverilog') and shutil.which('vvp'),'Requires Icarus')
class PositionTests(unittest.TestCase):
 def test_projection_acceptance_restart_and_step8(self):
  with tempfile.TemporaryDirectory() as td:
   d=Path(td); (d/'position.sv').write_text(position_rtl())
   assertions=[]
   for w,h in [(1,1),(2,3),(319,321),(320,320),(65535,65535)]:
    box=[(2*c*n+319)//640 for c,n in [(120,w),(135,h),(193,w),(201,h)]]
    assertions.append(f'''@(negedge clk); frame_start=1;frame_width=16'd{w};frame_height=16'd{h};advance=1;
@(posedge clk);#1;if(x!==0 || y!==0 || roi_left!==16'd{box[0]} || roi_top!==16'd{box[1]} || roi_right!==16'd{box[2]} || roi_bottom!==16'd{box[3]}) $fatal(1,"projection mismatch {w}x{h}");
@(negedge clk);frame_start=0;advance=0;
@(posedge clk);#1;if(x!==0 || y!==0) $fatal(1,"gap advanced counter");''')
   tb='''module tb;
reg clk=0;always #5 clk=~clk;
reg rst_n=0,frame_start=0,advance=0;
reg [15:0] frame_width=0,frame_height=0;
wire active;wire [15:0] x,y,width,height,roi_left,roi_top,roi_right,roi_bottom;
roi_position #(.STEP(8)) dut(.*);
initial begin
 repeat(2) @(negedge clk);rst_n=1;
'''+ '\n'.join(assertions)+'''
 @(negedge clk);frame_start=1;frame_width=9;frame_height=9;
 @(negedge clk);frame_start=0;advance=1;
 @(posedge clk);#1;if(x!==8 || y!==0) $fatal(1,"step8 col");
 @(posedge clk);#1;if(x!==0 || y!==8) $fatal(1,"step8 row");
 @(posedge clk);#1;if(x!==8 || y!==8) $fatal(1,"step8 last");
 @(posedge clk);#1;if(active!==0) $fatal(1,"frame did not end");
 @(posedge clk);#1;if(active!==0) $fatal(1,"ended frame restarted itself");
 @(negedge clk);frame_start=1;frame_width=0;frame_height=12;
 @(posedge clk);#1;if(active!==0) $fatal(1,"zero dimension active");
 $display("PASS position");$finish;
end
endmodule
'''
   (d/'tb.sv').write_text(tb)
   p=subprocess.run(['iverilog','-g2012','-s','tb','-o',str(d/'sim'),str(d/'position.sv'),str(d/'tb.sv')],capture_output=True,text=True)
   self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run(['vvp',str(d/'sim')],capture_output=True,text=True)
   self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   self.assertIn('PASS',p.stdout)
if __name__=='__main__': unittest.main()
