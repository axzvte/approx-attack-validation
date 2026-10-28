import importlib.util
import json
from pathlib import Path
import sys
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))

class DesignTests(unittest.TestCase):
    def module(self):
        path=ROOT/'scripts/bitflip_area_design.py'
        self.assertTrue(path.is_file(),'Missing area design generator')
        spec=importlib.util.spec_from_file_location('area_design',path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        return module

    def test_current_fixed_hardware_generates_all_five_scoped_tops(self):
        m=self.module()
        with tempfile.TemporaryDirectory() as tmp:
            target=Path(tmp)/'design';meta=m.generate_design(target)
            self.assertEqual(meta['scope'],'selected_unit_bank_not_full_application')
            self.assertEqual((target/meta['config_snapshot']).read_bytes(),m.DEFAULT_CONFIG.read_bytes())
            self.assertEqual([(a['application'],a['nodes'],a['config_bits']) for a in meta['applications']],
                [('sharpen',4,96),('conv',6,148),('sobel',6,148),('dct',6,148)])
            for app in meta['applications']:
                self.assertEqual(set(app['tops']),{'attack_comb','config_storage','attack_complete','host_baseline','host_attacked'})
                text=(target/'rtl'/f"{app['application']}_tops.sv").read_text()
                for top in app['tops'].values():
                    self.assertIn('module '+top+'(',text)
                self.assertIn('cfg_load',text)
            self.assertEqual([(a['exact_nodes'],a['approximate_nodes']) for a in meta['applications']],[(0,4),(4,2),(3,3),(3,3)])
            self.assertTrue(all((target/p).is_file() for p in meta['rtl_files']))
            with self.assertRaises(ValueError):m.generate_design(target)

    def test_invalid_flip_bit_duplicate_node_and_source_drift_are_rejected(self):
        m=self.module()
        for case in ('bit','duplicate','fingerprint'):
            with tempfile.TemporaryDirectory() as tmp:
                config=json.loads(m.DEFAULT_CONFIG.read_text())
                if case=='bit':config['applications'][0]['nodes'][0]['bit']=12
                elif case=='duplicate':config['applications'][0]['nodes'].append(config['applications'][0]['nodes'][0])
                else:config['source_fingerprints'][next(iter(config['source_fingerprints']))]='0'*64
                path=Path(tmp)/'config.json';path.write_text(json.dumps(config))
                target=Path(tmp)/'design'
                with self.assertRaises(ValueError):m.generate_design(target,path)
                self.assertFalse(target.exists())

    @unittest.skipUnless(shutil.which('iverilog') and shutil.which('vvp'),'Icarus needed for temporary RTL integration')
    def test_twenty_generated_tops_elaborate_and_each_bank_executes_fixed_payload(self):
        m=self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);meta=m.generate_design(root/'design')
            sources=[str(root/'design'/p) for p in meta['rtl_files']]
            for app in meta['applications']:
                n=app['nodes'];mon=app['monitor_bits'];total=13*n
                for top in app['tops'].values():
                    subprocess.run(['iverilog','-g2012','-s',top,'-o',str(root/'elaborated'),*sources],check=True,capture_output=True)
                # All-zero input is deterministic for both exact and 5RP. Use
                # the actual generated baseline value as the pre-flip oracle;
                # 5RP itself is separately C-oracle verified on16,640 vectors.
                lines=[f'module tb; reg clk=0, cfg_load=0; reg [{12*n-1}:0] a=0,b=0;',
                    f'reg [{mon-1}:0] low,high; wire [{total-1}:0] before_value,after_value;',
                    f'{app["tops"]["host_baseline"]} base(.a_bus(a),.b_bus(b),.y(before_value));',
                    f'{app["tops"]["host_attacked"]} dut(.clk(clk),.cfg_load(cfg_load),.a_bus(a),.b_bus(b),.cfg_lower_bus(low),.cfg_upper_bus(high),.y(after_value));',
                    'initial begin low=0; high=0; #1;']
                offset=0
                for i,p in enumerate(app['node_profiles']):
                    w=p['monitor_width'];value=f'before_value[{13*i+12}:{13*i}]' if w==13 else "12'b0"
                    lines.append(f'low[{offset+w-1}:{offset}]={value}; high[{offset+w-1}:{offset}]={value};')
                    offset+=w
                lines+=['cfg_load=1; #1; clk=1; #1; clk=0; cfg_load=0; #1;']
                mask=sum(1<<(13*i+p['bit']) for i,p in enumerate(app['node_profiles']))
                lines.append(f'if (after_value !== (before_value ^ {total}\'h{mask:x})) $fatal(1,"generated bank fixed payload mismatch");')
                # New data proves input1/input2 routing is fixed and payload
                # decisions use pre-flip values rather than the attacked bus.
                lines += [f"a='1; b={{{n}{{12'h001}}}}; #1;"]
                offset=0
                for i,p in enumerate(app['node_profiles']):
                    w=p['monitor_width']
                    value=f'before_value[{13*i+12}:{13*i}]' if w==13 else ("12'hfff" if p['monitor']=='input1' else "12'h001")
                    if i%2:value=f'($signed({value}) + 1)'
                    lines.append(f'low[{offset+w-1}:{offset}]={value}; high[{offset+w-1}:{offset}]={value};')
                    offset+=w
                selective_mask=sum(1<<(13*i+p['bit']) for i,p in enumerate(app['node_profiles']) if i%2==0)
                lines+=['cfg_load=1; #1; clk=1; #1; clk=0; cfg_load=0; #1;',
                    f'if (after_value !== (before_value ^ {total}\'h{selective_mask:x})) $fatal(1,"unequal-input bank routing mismatch");',
                    '$display("GENERATED_BANK_OK"); $finish; end endmodule']
                bench=root/'tb.sv';bench.write_text('\n'.join(lines))
                exe=root/'bank'
                subprocess.run(['iverilog','-g2012','-s','tb','-o',str(exe),str(bench),*sources],check=True,capture_output=True)
                result=subprocess.run(['vvp',str(exe)],check=True,capture_output=True,text=True,timeout=30)
                self.assertIn('GENERATED_BANK_OK',result.stdout)

if __name__=='__main__':unittest.main()
