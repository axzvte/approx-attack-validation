"""Shared DCT scheduler verified against the original C forward transform."""
import copy
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'hardware/scripts')]

class DctTests(unittest.TestCase):
    def test_scheduler_has_one_core_and_full_width_flip(self):
        from rtl_dct import emit_dct,core_profile,selected_phase
        from generate_rtl import default_configuration
        config=default_configuration();p=config['input_circuits']['dct_mixed'];choice=config['circuits']['dct_mixed']
        text=emit_dct(ROOT,p,choice,'processed',config['bbox_half_open'])
        self.assertEqual(text.count(' arithmetic('),1)
        _,site=selected_phase(p,core_profile(ROOT,p['baseline']),choice)
        self.assertIn(f"wire signed [31:0] {site} = {site}_raw ^ (flip_enable ? 32'sd{1<<choice['bit']} : 32'sd0);",text)
        bad=copy.deepcopy(p); marked=[x for x in bad['ir']['operations'] if 'unit_node' in x]
        marked[32]['unit_node']=31
        with self.assertRaises(ValueError): emit_dct(ROOT,bad,choice,'processed',config['bbox_half_open'])
        bad=copy.deepcopy(p)
        next(x for x in bad['ir']['operations'] if x['op']=='scale' and x.get('factor')==11585)['factor']=11586
        with self.assertRaises(ValueError): emit_dct(ROOT,bad,choice,'processed',config['bbox_half_open'])


def run(command):
    result=subprocess.run(list(map(str,command)),capture_output=True,text=True)
    if result.returncode or result.stderr: raise AssertionError(result.stdout+result.stderr)
    return result.stdout


@unittest.skipUnless(all(shutil.which(x) for x in ('gcc','iverilog','vvp')),'Requires GCC and Icarus')
class DctSimulationTests(unittest.TestCase):
    def test_all_coefficients_against_c_with_roi_gaps_aborts_and_partial_tiles(self):
        import ctypes
        from generate_rtl import default_configuration
        from rtl_dct import emit_dct,selected_phase,core_profile
        from rtl_stencil import position_rtl
        from rtl_units import generate_units
        cfg=default_configuration();p=cfg['input_circuits']['dct_mixed'];plan=cfg['circuits']['dct_mixed'];bbox=cfg['bbox_half_open']
        phase,_=selected_phase(p,core_profile(ROOT,p['baseline']),plan)
        rng=random.Random(9182)
        cases=[(1,1,0),(9,17,128),(17,9,255),(80,56,None),(81,57,None)]
        vectors=[];frames=[];eligible=[]
        for w,h,constant in cases:
            start=len(vectors);edges=[(2*b*n+319)//640 for b,n in zip(bbox,(w,h,w,h))]
            for y in range(0,h,8):
                for x in range(0,w,8):
                    vectors.append([constant if constant is not None else rng.randrange(256) for _ in range(64)])
                    eligible.append(x>=edges[0] and y>=edges[1] and x+8<=edges[2] and y+8<=edges[3])
            frames.append((w,h,start,len(vectors)-start))
        self.assertTrue(any(eligible));self.assertTrue(any(not x for x in eligible))
        with tempfile.TemporaryDirectory() as directory:
            d=Path(directory)
            header=(ROOT/'kernels/circuit_intrinsics.h').read_text().replace('static inline int circuit_add(int id, int a, int b) { (void)id; return a+b; }','int circuit_add(int id, int a, int b);')
            (d/'circuit_intrinsics.h').write_text(header)
            (d/'original.c').write_text((ROOT/'kernels/dct.c').read_text())
            units=sorted((ROOT/'third_party/evoapproxlib').glob('add12se_*.c'))
            decl='\n'.join(f'extern unsigned long long {x.stem}(unsigned long long,unsigned long long);' for x in units)
            calls=[]
            for item in p['baseline']:
                calls.append(f'case {item["node"]}: v=adapt({"0" if item["unit"]=="exact" else "add12se_"+item["unit"]}(b&4095,a&4095)); break;' if item['unit']!='exact' else f'case {item["node"]}: v=a+b;break;')
            source=f'''#include <stdint.h>
#include "original.c"
{decl}
static int version,inside,invocation,selected_phase,selected_group,selected_bit;
static unsigned exact_mask;
static int adapt(unsigned long long v) {{ int x=v&8191;return (x&4096)?x-8192:x; }}
int circuit_add(int id,int a,int b) {{ int v;
 if(version && ((exact_mask>>id)&1)) v=a+b;
 else switch(id) {{ {''.join(calls)} default: v=a+b; }}
 if(version==2 && inside && invocation==selected_phase && id==selected_group) v=(int)((uint32_t)v^(1u<<selected_bit));
 return v;
}}
void forward(int *input,int *output,int v,int roi,int ph,int grp,int bit,unsigned mask) {{
 int rows[64],x[8],y[8];version=v;inside=roi;selected_phase=ph;selected_group=grp;selected_bit=bit;exact_mask=mask;
 for(int r=0;r<8;r++) {{ for(int c=0;c<8;c++) x[c]=input[r*8+c]-128; invocation=r;dct1d(x,y);for(int k=0;k<8;k++)rows[r*8+k]=y[k]; }}
 for(int c=0;c<8;c++) {{ for(int r=0;r<8;r++) x[r]=rows[r*8+c];invocation=8+c;dct1d(x,y);for(int k=0;k<8;k++)output[k*8+c]=y[k]; }}
}}
'''
            (d/'oracle.c').write_text(source)
            run(['gcc','-shared','-fPIC','-O2','-o',d/'oracle.so',d/'oracle.c',*units,'-lm'])
            library=ctypes.CDLL(str(d/'oracle.so'));oracle=library.forward
            oracle.argtypes=[ctypes.POINTER(ctypes.c_int)]*2+[ctypes.c_int]*5+[ctypes.c_uint]
            packed=lambda values,bits:sum((int(x)&((1<<bits)-1))<<(i*bits) for i,x in enumerate(values))
            (d/'samples.hex').write_text('\n'.join(f'{packed(v,8):0128x}' for v in vectors)+'\n')
            (d/'units.sv').write_text(generate_units(ROOT));(d/'position.sv').write_text(position_rtl())
            # Exercise every row and column phase at an exact32 group and bit11.
            marked=[x for x in p['ir']['operations'] if 'unit_node' in x]
            selections=[('baseline',plan,phase),('compensated',plan,phase),('processed',plan,phase)]
            for ph in range(16):
                alternate=copy.deepcopy(plan)
                chosen=next(x for x in marked[ph*32:(ph+1)*32] if x['unit_node']==0)
                alternate.update(node=chosen['id'],unit_node=0,bit=11)
                selections.append(('processed',alternate,ph))
            for version,choice,ph in selections:
                with self.subTest(version=version,phase=ph):
                    expected=[]
                    for block,roi in zip(vectors,eligible):
                        inputs=(ctypes.c_int*64)(*block);outputs=(ctypes.c_int*64)()
                        oracle(inputs,outputs,('baseline','compensated','processed').index(version),roi,ph,choice['unit_node'],choice['bit'],sum(1<<g for g in choice['exact_nodes']))
                        expected.append(packed(outputs,32))
                    (d/'expected.hex').write_text('\n'.join(f'{v:0512x}' for v in expected)+'\n')
                    (d/'top.sv').write_text(emit_dct(ROOT,p,choice,version,bbox))
                    invocations='\n'.join(f'run_frame({w},{h},{offset},{count});' for w,h,offset,count in frames)
                    tb=f'''module tb;
reg clk=0;always #5 clk=~clk;
reg rst_n=0,frame_start=0,in_valid=0;reg[15:0]frame_width=0,frame_height=0;
reg[511:0]samples=0;wire in_ready,out_valid;wire[2047:0]coefficients;
reg[511:0]vectors[0:{len(vectors)-1}];reg[2047:0]expected[0:{len(vectors)-1}];
{ 'dct_'+version } dut(.*);
task run_frame;input integer w,h,offset,count;integer i,j;begin
 @(negedge clk);frame_start=1;in_valid=1;frame_width=w;frame_height=h;
 #1;if(in_ready!==0)$fatal(1,"ready on start");
 @(posedge clk);#1;if(out_valid!==0)$fatal(1,"output on start");
 @(negedge clk);frame_start=0;in_valid=0;frame_width=0;frame_height=0;
 for(i=0;i<count;i=i+1) begin
  if(i%3==0) begin
   @(negedge clk);in_valid=0;@(posedge clk);#1;if(out_valid!==0)$fatal(1,"gap produced output");
  end
  @(negedge clk);in_valid=1;samples=vectors[offset+i];#1;
  if(in_ready!==1)$fatal(1,"not ready for block");
  @(posedge clk);#1;if(in_ready!==0 || out_valid!==0)$fatal(1,"accept protocol");
  for(j=1;j<=16;j=j+1) begin
   @(negedge clk);in_valid=1;samples=~samples;
   @(posedge clk);#1;
   if(j<16 && (out_valid!==0 || in_ready!==0))$fatal(1,"busy/latency");
   if(j==16 && (out_valid!==1 || coefficients!==expected[offset+i]))
    $fatal(1,"{version} phase{ph} block %0d coeff mismatch got %h expected %h",offset+i,coefficients,expected[offset+i]);
  end
 end
 @(negedge clk);in_valid=1;
 @(posedge clk);#1;if(in_ready!==0 || out_valid!==0)$fatal(1,"past frame end");
end endtask
initial begin
 $readmemh("{(d/'samples.hex').as_posix()}",vectors);$readmemh("{(d/'expected.hex').as_posix()}",expected);
 repeat(2)@(negedge clk);rst_n=1;
 frame_start=1;frame_width=80;frame_height=56;
 @(negedge clk);frame_start=0;in_valid=1;samples=vectors[0];
 repeat(7)@(negedge clk);
 {invocations}
 $display("PASS {version} phase{ph} {len(vectors)} blocks");$finish;
end
initial begin #1000000;$fatal(1,"timeout");end
endmodule
'''
                    (d/'tb.sv').write_text(tb)
                    run(['iverilog','-g2012','-s','tb','-o',d/'sim',d/'units.sv',d/'position.sv',d/'top.sv',d/'tb.sv'])
                    self.assertIn('PASS',run(['vvp',d/'sim']))


if __name__=='__main__': unittest.main()

