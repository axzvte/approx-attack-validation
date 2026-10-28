"""Full-kernel functional checks only. Never performs synthesis."""
import importlib.util
import ctypes
import json
from pathlib import Path
import shutil
import random
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))


class LegacyDesignTests(unittest.TestCase):
    def module(self):
        self.assertIsNotNone(importlib.util.find_spec('bitflip_legacy_design'), 'Missing full legacy-kernel generator')
        import bitflip_legacy_design
        return bitflip_legacy_design

    def test_current_configuration_uses_complete_historical_kernels(self):
        m=self.module()
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp)/'design';meta=m.generate_design(out)
            self.assertEqual(meta['scope'],'single_arithmetic_kernel_not_full_image_system')
            self.assertEqual([(a['application'],a['nodes'],a['config_bits']) for a in meta['applications']],
                [('sharpen',4,96),('conv',6,148),('sobel',6,148),('dct',6,148)])
            self.assertEqual([a['total_add_nodes'] for a in meta['applications']],[4,8,11,32])
            self.assertEqual([a['historical_baseline_um2'] for a in meta['applications']],[293.132,482.790,513.380,16686.446])
            self.assertEqual(meta['liberty_sha256'],'8d540a4d4cf6d09d27c87ad067857a9c0c2eeb023ab7a56e058cd3113db4e9b1')
            self.assertTrue(all((out/p).is_file() for p in meta['rtl_files']))
            self.assertTrue(all(p['native_monitor_width']==32 for p in meta['applications'][-1]['node_profiles']))
            with self.assertRaises(ValueError):m.generate_design(out)

    def test_changed_reference_kernel_is_rejected_before_output(self):
        m=self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);shutil.copytree(m.LEGACY_SOURCES,root/'reference')
            with (root/'reference/dct_baseline.v').open('a') as f:f.write('// changed\n')
            with patch.object(m,'LEGACY_SOURCES',root/'reference'):
                with self.assertRaisesRegex(ValueError,'fingerprint'):
                    m.generate_design(root/'design')
            self.assertFalse((root/'design').exists())

    def test_configuration_edit_during_loading_cannot_mismatch_snapshot_and_rtl(self):
        m=self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);config=root/'config.json';shutil.copy2(m.DEFAULT_CONFIG,config)
            original=m.load_config
            def edit_during_load(path):
                parsed=original(path)
                changed=json.loads(path.read_text());changed['applications'][0]['nodes'][0]['bit']=1
                path.write_text(json.dumps(changed))
                return parsed
            with patch.object(m,'load_config',side_effect=edit_during_load):
                with self.assertRaisesRegex(ValueError,'Configuration changed'):
                    m.generate_design(root/'design',config)
            self.assertFalse((root/'design').exists())

    @unittest.skipUnless(shutil.which('iverilog') and shutil.which('vvp'),'Icarus needed')
    def test_all_twenty_tops_elaborate_and_native_monitor_does_not_wrap(self):
        m=self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);meta=m.generate_design(root/'design')
            sources=[str(root/'design'/p) for p in meta['rtl_files']]
            for app in meta['applications']:
                for top in app['tops'].values():
                    p=subprocess.run(['iverilog','-g2012','-s',top,'-o',str(root/'sim'),*sources],capture_output=True,text=True)
                    self.assertEqual(p.returncode,0,p.stderr)
            # Native32 DCT monitor 4096 must not alias the12-bit value0.
            app=meta['applications'][-1];n=app['nodes'];b=app['monitor_bits']
            tb=['module tb;',f'reg [{32*n-1}:0] mon=0,base=0;',
                f'reg [{b-1}:0] low=0,high=0; wire [{32*n-1}:0] y;',
                f"{app['tops']['attack_comb']} dut(.monitor_bus(mon),.baseline_bus(base),.lower_bus(low),.upper_bus(high),.y(y));",
                'initial begin mon[31:0]=4096; #1;',
                'if (y[31:0] !== 0) $fatal(1,"truncated native monitor");',
                'mon[31:0]=0; base[31:0]=32\'h12345000; #1;',
                f"if (y[31:0] !== (32'h12345000 ^ 32'd{1<<app['node_profiles'][0]['bit']})) $fatal(1,\"native payload width\");",
                'mon[31:0]=-1; #1; if (y[31:0] !== base[31:0]) $fatal(1,"signed comparison");',
                '$finish; end endmodule']
            (root/'tb.sv').write_text('\n'.join(tb))
            subprocess.run(['iverilog','-g2012','-s','tb','-o',str(root/'sim'),str(root/'tb.sv'),*sources],check=True,capture_output=True)
            p=subprocess.run(['vvp',str(root/'sim')],capture_output=True,text=True)
            self.assertEqual(p.returncode,0,p.stdout+p.stderr)

    @unittest.skipUnless(all(shutil.which(t) for t in ('gcc','iverilog','vvp')),'C/Icarus needed')
    def test_full_kernels_match_c_oracle_and_hold_loaded_configuration(self):
        m=self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);meta=m.generate_design(root/'design')
            sources=[str(root/'design'/p) for p in meta['rtl_files']]
            so=root/'oracle.so'
            subprocess.run(['gcc','-shared','-fPIC','-O2','-o',str(so),str(ROOT/'third_party/evoapproxlib/add12se_5RP.c')],check=True,capture_output=True)
            lib=ctypes.CDLL(str(so));fn=lib.add12se_5RP
            fn.argtypes=(ctypes.c_uint64,ctypes.c_uint64);fn.restype=ctypes.c_uint64
            defaults={'sharpen':set(range(4)),'conv':{1,2,4,7},'sobel':{0,2,4,7,9},'dct':{8,10,11,13,16,19,22,25,28,31}}
            coeff=[[11585]*4,[16069,13623,9102,3196],[15137,6270,-6270,-15137],
                [13623,-3196,-16069,-9102],[11585,-11585,-11585,11585],
                [9102,-16069,3196,13623],[6270,-15137,15137,-6270],[3196,-9102,13623,-16069]]
            def reference(app,values,profiles,mode):
                chosen={p['node_id']:p for p in profiles}
                def add(i,a,b):
                    if i in defaults[app]:
                        if not all(-2048<=x<=2047 for x in (a,b)):raise ValueError('outside software signed12 domain')
                        raw=int(fn(b&4095,a&4095))&8191
                        base=raw-8192 if raw&4096 else raw
                    else:base=a+b
                    if app!='dct' and not -4096<=base<=4095:raise ValueError('outside legacy pixel width')
                    if mode and i in chosen:
                        p=chosen[i];mon={'input1':a,'input2':b,'baseline_output':base}[p['monitor']]
                        w=p['monitor_width'];lo,hi=(-(1<<(w-1)),(1<<(w-1))-1) if mode==1 else (-1,1)
                        if lo<=mon<=hi:base ^= 1<<p['bit']
                    return base
                clamp=lambda x:max(0,min(255,x))
                if app=='sharpen':
                    c,t,b,l,r=values
                    return [clamp(add(3,add(2,add(1,add(0,5*c,-t),-b),-l),-r))]
                if app=='conv':
                    z=[x-128 for x in values];w=[8*x if i==4 else -x for i,x in enumerate(z)]
                    a=[add(i,w[2*i],w[2*i+1]) for i in range(4)]
                    return [clamp(abs(add(7,add(6,add(4,a[0],a[1]),add(5,a[2],a[3])),w[8])))]
                if app=='sobel':
                    p=dict(zip([1,2,3,4,6,7,8,9],values))
                    gx=add(4,add(1,add(0,p[3],2*p[6]),p[9]),-add(3,add(2,p[1],2*p[4]),p[7]))
                    gy=add(9,add(6,add(5,p[7],2*p[8]),p[9]),-add(8,add(7,p[1],2*p[2]),p[3]))
                    return [clamp(add(10,abs(gx),abs(gy)))]
                pairs=[add(i,values[i],values[7-i]) for i in range(4)]
                pairs += [add(i+4,values[i],-values[7-i]) for i in range(4)]
                result=[]
                for k in range(8):
                    products=[]
                    for j in range(4):
                        v=pairs[j+(4 if k%2 else 0)]*coeff[k][j]
                        q=(abs(v)+16384)>>15;products.append(q if v>=0 else -q)
                    i=8+3*k;result.append(add(i+2,add(i+1,add(i,products[0],products[1]),products[2]),products[3]))
                return result
            rng=random.Random(20261028)
            for a in meta['applications']:
                app=a['application'];bits=a['monitor_bits'];dct=app=='dct'
                names=[f'x{i}' for i in range(8)] if dct else (['center','top','bottom','left','right'] if app=='sharpen' else
                    [f'p{i}' for i in ([1,2,3,4,6,7,8,9] if app=='sobel' else range(1,10))])
                outputs=[f'y{i}' for i in range(8)] if dct else ['pixel']
                lines=['module tb; reg clk=0,load=0;',f'reg [{bits-1}:0] lo,hi;',
                    *[f"reg {'signed [11:0]' if dct else '[7:0]'} {n};" for n in names],
                    *[f"wire {'signed [31:0]' if dct else '[7:0]'} base_{n},out_{n};" for n in outputs]]
                ins=','.join(f'.{n}({n})' for n in names)
                lines += [f"{a['tops']['host_baseline']} original({ins},"+','.join(f'.{n}(base_{n})' for n in outputs)+');',
                    f"{a['tops']['host_attacked']} attacked({ins},.clk(clk),.cfg_load(load),.cfg_lower_bus(lo),.cfg_upper_bus(hi),"+','.join(f'.{n}(out_{n})' for n in outputs)+');','initial begin']
                checked=0;changed=0
                for mode in (0,1,2):
                    low=high=0
                    for p in a['node_profiles']:
                        w=p['monitor_width'];off=p['offset'];l,h=((1,0) if mode==0 else ((-(1<<(w-1)),(1<<(w-1))-1) if mode==1 else (-1,1)))
                        low|=(l&((1<<w)-1))<<off;high|=(h&((1<<w)-1))<<off
                    lines += [f"lo={bits}'h{low:x}; hi={bits}'h{high:x}; load=1; #1; clk=1; #1; clk=0; load=0; lo=0; hi=0; #1;"]
                    for _ in range(28):
                        values=[rng.randrange(-64,65) if dct else rng.randrange(256) for n in names]
                        try:base=reference(app,values,a['node_profiles'],0);want=reference(app,values,a['node_profiles'],mode)
                        except ValueError:continue
                        changed+=want!=base
                        lines += [f"{n}={12 if dct else 8}'h{v&((1<<(12 if dct else 8))-1):x};" for n,v in zip(names,values)]
                        # cfg_load=0 must hold even while the clock toggles.
                        lines += ['#1; clk=1; #1; clk=0; #1;']
                        for n,b,v in zip(outputs,base,want):
                            w=32 if dct else 8
                            lines += [f"if (base_{n} !== {w}'h{b&((1<<w)-1):x}) $fatal(1,\"{app} baseline\");",
                                f"if (out_{n} !== {w}'h{v&((1<<w)-1):x}) $fatal(1,\"{app} bitflip profile{mode}\");"]
                        checked+=1
                self.assertGreaterEqual(checked,40)
                self.assertGreater(changed,0,app+' must exercise a visible payload effect')
                lines+=['$finish; end endmodule']
                (root/'tb.sv').write_text('\n'.join(lines))
                subprocess.run(['iverilog','-g2012','-s','tb','-o',str(root/'sim'),str(root/'tb.sv'),*sources],check=True,capture_output=True)
                p=subprocess.run(['vvp',str(root/'sim')],capture_output=True,text=True)
                self.assertEqual(p.returncode,0,p.stdout+p.stderr)


if __name__=='__main__':unittest.main()
