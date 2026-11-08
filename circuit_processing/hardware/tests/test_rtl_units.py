import ctypes, random, subprocess, tempfile, unittest
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'hardware/scripts'))
from rtl_units import generate_units
class Units(unittest.TestCase):
 def test_c_oracle(self):
  with tempfile.TemporaryDirectory() as td:
   d=Path(td); sv=d/'units.sv'; sv.write_text(generate_units(ROOT))
   src=sorted((ROOT/'third_party/evoapproxlib').glob('add12se_*.c'))
   subprocess.run(['gcc','-shared','-fPIC','-o',str(d/'oracle.so'),*map(str,src)],check=True)
   lib=ctypes.CDLL(str(d/'oracle.so')); rng=random.Random(81239)
   edges=[0,1,2,2047,2048,2049,4094,4095]
   vectors=[(a,b) for a in edges for b in edges]+[(rng.randrange(4096),rng.randrange(4096)) for _ in range(1000)]
   for c in src:
    unit=c.stem.split('_')[1]; fn=getattr(lib,c.stem); fn.argtypes=[ctypes.c_uint64]*2; fn.restype=ctypes.c_uint64
    tb=['module tb; reg [11:0] A,B; wire [12:0] Y; approx_'+unit+' dut(A,B,Y); initial begin']
    for a,b in vectors:
     expected=fn(b,a)&8191
     tb.append(f"A=12'd{a}; B=12'd{b}; #1; if(Y !== 13'd{expected}) $fatal(1,\"{unit} mismatch\");")
    tb+=['$finish; end endmodule']; (d/'tb.sv').write_text('\n'.join(tb))
    subprocess.run(['iverilog','-g2012','-s','tb','-o',str(d/'sim'),str(sv),str(d/'tb.sv')],check=True)
    subprocess.run(['vvp',str(d/'sim')],check=True,capture_output=True)
if __name__=='__main__': unittest.main()
