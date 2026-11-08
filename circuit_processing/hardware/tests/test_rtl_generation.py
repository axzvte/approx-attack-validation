import sys, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'hardware/scripts')]

class GenerationTests(unittest.TestCase):
    def test_integer_emitter_is_available(self):
        try:
            import rtl_emit
        except ImportError:
            self.fail('source IR to RTL emitter is missing')
        from circuit_sources import load_profile
        profile=load_profile(ROOT/'configs/circuits/sharpen_mixed.json')
        source=rtl_emit.emit_graph(profile,'test_sharpen',exact_nodes=[1,2,3],flip_site='n2',flip_bit=5)
        self.assertIn('module test_sharpen',source)
        self.assertIn('approx_5QC',source)
        self.assertIn('flip_enable',source)
        self.assertNotIn(' real ',source)

    def test_fractional_arithmetic_fails_explicitly(self):
        try:
            import rtl_emit
        except ImportError:
            self.fail('source IR to RTL emitter is missing')
        from circuit_sources import load_profile
        profile=load_profile(ROOT/'configs/circuits/sharpen_mixed.json')
        profile['ir']['operations'][0]['factor']=0.5
        profile['ir']['operations'][0]['integer']=False
        with self.assertRaisesRegex(ValueError,'[Ff]ractional'):
            rtl_emit.emit_graph(profile,'bad',[])

if __name__=='__main__': unittest.main()
