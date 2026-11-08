"""AST extraction, fail-closed language boundary, and compiled-C equivalence."""
import ctypes
import json
import math
from pathlib import Path
import random
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from c_kernel_encoder import encode_profile, EncodingError


def exact(ir,inputs):
    env={x['id']:v for x,v in zip(ir['inputs'],inputs)}
    def get(v): return env[v] if isinstance(v,str) else v
    for op in ir['operations']:
        a=[get(x) for x in op['args']]
        name=op['op']
        if name=='add': v=a[0]+a[1]
        elif name=='sub': v=a[0]-a[1]
        elif name=='scale': v=a[0]*op['factor']
        elif name=='abs': v=abs(a[0])
        elif name=='clamp': v=max(op['min'],min(op['max'],a[0]))
        elif name=='trunc': v=math.trunc(a[0])
        elif name=='round_even': v=round(a[0])
        elif name=='shift': v=int(a[0])>>op['bits'] if op['bits']>=0 else int(a[0])<<-op['bits']
        elif name=='round_shift':
            bits=op['bits']; v=a[0] if bits==0 else (1 if a[0]>=0 else -1)*((abs(int(a[0]))+(1<<(bits-1)))>>bits)
        else: raise AssertionError(name)
        env[op['id']]=v
    return [get(o['value']) for o in ir['outputs']]


class FrontendTests(unittest.TestCase):
    def custom(self,body,policy='marked',omit_baseline=False):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp); (p/'kernel.c').write_text(body)
            d=dict(schema=2,name='test',application='c',source='kernel.c',entry='kernel',
                   tile=[1,1],border='replicate',inputs=[dict(id='p0',dx=0,dy=0)],
                   outputs=[dict(dx=0,dy=0)],node_policy=policy,baseline=[])
            if omit_baseline: d.pop('baseline')
            return encode_profile(d,p)

    def test_edges_are_extracted_from_source(self):
        a=self.custom('void kernel(int x[1],double y[1]) { y[0]=x[0]+3; }','all_integer_adds')
        b=self.custom('void kernel(int x[1],double y[1]) { y[0]=x[0]-7; }','all_integer_adds')
        self.assertEqual(exact(a['ir'],[9]),[12])
        self.assertEqual(exact(b['ir'],[9]),[2])
        self.assertNotEqual(a['source']['sha256'],b['source']['sha256'])
        self.assertEqual(a['ir']['operations'][0]['unit_node'],0)

    def test_loops_inline_arrays_and_casts(self):
        p=self.custom('''void helper(int a[1],int b[1]) { b[0]=a[0]+2; }
        void kernel(int x[1],double y[1]) {
          int a[1]; helper(x,a); double s=0.0;
          for(int i=0;i<3;++i) { s=s+0.5*a[0]; }
          int t; t=s; y[0]=t;
        }''')
        self.assertEqual(exact(p['ir'],[3]),[7])
        self.assertTrue(any(x['op']=='trunc' for x in p['ir']['operations']))

    def test_source_location_group_reused_across_unrolling(self):
        p=self.custom('''void kernel(int x[1],double y[1]) {
          int a=x[0]; for(int i=0;i<3;++i) a=a+2; y[0]=a;
        }''','all_integer_adds')
        groups=[x['unit_node'] for x in p['ir']['operations'] if 'unit_node' in x]
        self.assertEqual(groups,[0,0,0])

    def test_signed_rounding_and_const_array(self):
        p=self.custom('''const int q[3]={-3,-1,1};
        void kernel(int x[1],double y[1]) {
          long long p=(long long)x[0]*q[0];
          y[0]=circuit_round_shift(p,1);
        }''')
        self.assertEqual(exact(p['ir'],[1]),[-2])
        self.assertEqual(exact(p['ir'],[-1]),[2])
        self.assertTrue(any(o['width']==64 for o in p['ir']['operations']))

    def test_automatic_groups_and_source_provenance_without_baseline(self):
        p=self.custom('''int helper(int a) { return a+2; }
        void kernel(int x[1],double y[1]) { y[0]=helper(x[0])-1; }''',
                      'all_integer_adds',omit_baseline=True)
        self.assertEqual(p['baseline'],[dict(node=0,unit='exact'),dict(node=1,unit='exact')])
        self.assertEqual(exact(p['ir'],[9]),[10])
        for op in p['ir']['operations']:
            self.assertIn('kernel.c:',op['source_coord'])
            self.assertEqual(op['call_stack'][0]['function'],'kernel')
        self.assertEqual(p['ir']['operations'][0]['call_stack'][-1]['function'],'helper')
        self.assertEqual(self.custom('void kernel(int x[1],double y[1]) { y[0]=x[0]; }',
                                    omit_baseline=True)['baseline'],[])

    def test_left_shift_lowers_to_scale_and_right_shift_roundtrip(self):
        p=self.custom('void kernel(int x[1],double y[1]) { y[0]=(x[0]<<3)>>2; }')
        self.assertEqual(exact(p['ir'],[123]),[246])
        self.assertEqual(p['ir']['operations'][0]['op'],'scale')
        self.assertEqual(p['ir']['operations'][0]['factor'],8)
        self.assertEqual(p['ir']['operations'][1]['bits'],2)
        for count in ('-1','32','1.5','x[0]'):
            with self.subTest(count=count),self.assertRaises(EncodingError):
                self.custom('void kernel(int x[1],double y[1]) { y[0]=x[0]<<'+count+'; }')

    def test_rejects_reserved_intrinsic_overrides_and_persistent_state(self):
        sources=[
            'int circuit_add(int id,int a,int b) { return a-b; } void kernel(int x[1],double y[1]) { y[0]=circuit_add(0,x[0],2); }',
            'int counter=0; void kernel(int x[1],double y[1]) { counter=counter+1; y[0]=counter; }',
            'int table[2]={1,2}; void kernel(int x[1],double y[1]) { y[0]=table[0]; }',
            'void kernel(int x[1],double y[1]) { static int counter=0; counter++; y[0]=counter; }',
            'const int table[2]; void kernel(int x[1],double y[1]) { y[0]=table[0]; }',
        ]
        for body in sources:
            with self.subTest(body=body),self.assertRaises(EncodingError) as ctx:
                self.custom(body)
            self.assertIn('kernel.c:',str(ctx.exception))
        p=self.custom('int circuit_add(int id,int a,int b); void kernel(int x[1],double y[1]) { y[0]=circuit_add(0,x[0],2); }')
        self.assertEqual(exact(p['ir'],[4]),[6])

    def test_compile_time_logical_conditions_short_circuit(self):
        p=self.custom('''void kernel(int x[1],double y[1]) {
          int a=1; if(0&&a++) {} if(1||a++) {} y[0]=a;
        }''')
        self.assertEqual(exact(p['ir'],[0]),[1])
        with self.assertRaises(EncodingError):
            self.custom('void kernel(int x[1],double y[1]) { if(x[0]&&1) y[0]=0; }')

    @unittest.skipUnless(shutil.which('gcc'),'gcc required for conditional C type check')
    def test_ternary_usual_type_and_inactive_branch_not_evaluated(self):
        body='''void kernel(int input[1],double output[1]) {
          int x=input[0];
          output[0]=(1?x:0.5)+input[0];
          int a=1; int b=0?a++:a;
          output[0]=output[0]+a+b-2;
        }'''
        p=self.custom(body,'all_integer_adds',omit_baseline=True)
        self.assertEqual(p['baseline'],[])
        self.assertFalse(any('unit_node' in op for op in p['ir']['operations']))
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'ternary.c'; source.write_text(body)
            lib=Path(temp)/'ternary.so'
            subprocess.run(['gcc','-std=c99','-shared','-fPIC','-O0',str(source),'-o',str(lib)],
                           check=True,capture_output=True)
            fn=ctypes.CDLL(str(lib)).kernel
            fn.argtypes=[ctypes.POINTER(ctypes.c_int),ctypes.POINTER(ctypes.c_double)]
            for value in (0,1,127,255):
                out=(ctypes.c_double*1)(); fn((ctypes.c_int*1)(value),out)
                self.assertEqual(list(out),exact(p['ir'],[value]))

    @unittest.skipUnless(shutil.which('gcc'),'gcc required for intrinsic C conversion check')
    def test_intrinsic_formal_conversions_match_native_c(self):
        body='''#include "circuit_intrinsics.h"
        void kernel(int x[1],double y[1]) {
          y[0]=circuit_abs(-2.7)+circuit_clip(x[0],1.25,3.75)
               +circuit_round_even(x[0]*0.5)+circuit_trunc(x[0]*0.5);
        }'''
        with tempfile.TemporaryDirectory() as temp:
            directory=Path(temp); source=directory/'kernel.c'; source.write_text(body)
            d=dict(schema=2,name='intrinsic',application='c',source='kernel.c',entry='kernel',
                   tile=[1,1],border='replicate',inputs=[dict(id='p0',dx=0,dy=0)],
                   outputs=[dict(dx=0,dy=0)],node_policy='all_integer_adds')
            p=encode_profile(d,directory)
            self.assertEqual(p['baseline'],[])
            self.assertFalse(any('unit_node' in op for op in p['ir']['operations']))
            lib=directory/'intrinsic.so'
            subprocess.run(['gcc','-std=c99','-shared','-fPIC','-O0','-ffp-contract=off',
                            '-I',str(ROOT/'kernels'),str(source),'-lm','-o',str(lib)],
                           check=True,capture_output=True)
            fn=ctypes.CDLL(str(lib)).kernel
            fn.argtypes=[ctypes.POINTER(ctypes.c_int),ctypes.POINTER(ctypes.c_double)]
            for value in range(9):
                out=(ctypes.c_double*1)(); fn((ctypes.c_int*1)(value),out)
                self.assertEqual(list(out),exact(p['ir'],[value]))

    def test_rejects_unsupported_with_location(self):
        for fragment in ('if(x[0]) y[0]=1;', 'y[0]=x[0]*x[0];',
                         'y[0]=unknown(x[0]);', 'y[0]=x[x[0]];',
                         'while(1) y[0]=0;', 'y[0]=x[1];',
                         'int z; y[0]=z;', 'int *p; y[0]=0;',
                         'float p; y[0]=0;', 'y[0]=1e90;',
                         'const int q=1; q=2; y[0]=q;'):
            with self.subTest(fragment=fragment),self.assertRaises(EncodingError) as ctx:
                self.custom('void kernel(int x[1],double y[1]) {'+fragment+'}')
            self.assertIn('kernel.c',str(ctx.exception))

    def test_graph_determinism_and_all_dct_sites(self):
        for name,count in [('sharpen',4),('conv',8),('sobel',11),('dct',512)]:
            f=ROOT/'kernels'/f'{name}_interface.json'
            d=json.loads(f.read_text(encoding='utf-8-sig'))
            p=encode_profile(d,f.parent)
            self.assertEqual(p,encode_profile(d,f.parent))
            ops=p['ir']['operations']
            self.assertEqual(sum('unit_node' in x for x in ops),count)
            seen={x['id'] for x in p['ir']['inputs']}
            for op in ops:
                self.assertTrue(all(not isinstance(a,str) or a in seen for a in op['args']))
                self.assertNotIn(op['id'],seen)
                seen.add(op['id'])
            if name=='dct': self.assertEqual(len({x['unit_node'] for x in ops if 'unit_node' in x}),32)

    @unittest.skipUnless(shutil.which('gcc'),'gcc required for compiled native comparison')
    def test_compiled_c_matches_exact_ir(self):
        rng=random.Random(8241)
        with tempfile.TemporaryDirectory() as temp:
            for name in ('sharpen','conv','sobel','dct'):
                f=ROOT/'kernels'/f'{name}_interface.json'
                d=json.loads(f.read_text(encoding='utf-8-sig')); p=encode_profile(d,f.parent)
                lib=Path(temp)/f'{name}.so'
                subprocess.run(['gcc','-std=c99','-shared','-fPIC','-O0','-ffp-contract=off',
                                str(f.parent/d['source']),'-lm','-o',str(lib)],check=True,capture_output=True)
                fn=getattr(ctypes.CDLL(str(lib)),d['entry'])
                ni,no=len(d['inputs']),len(d['outputs'])
                fn.argtypes=[ctypes.POINTER(ctypes.c_int),ctypes.POINTER(ctypes.c_double)]
                samples=[[value]*ni for value in (0,1,63,127,128,129,254,255)]
                samples += [[rng.randrange(256) for _ in range(ni)] for _ in range(32)]
                for values in samples:
                    outputs=(ctypes.c_double*no)(); fn((ctypes.c_int*ni)(*values),outputs)
                    self.assertEqual(list(outputs),exact(p['ir'],values),(name,values))


if __name__=='__main__': unittest.main()
