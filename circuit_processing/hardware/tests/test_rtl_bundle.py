"""Artifact completeness, portability and fail-closed frozen configuration checks."""
from pathlib import Path
import copy,hashlib,json,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'hardware/scripts')]

class BundleTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  from generate_rtl import default_configuration
  cls.config=default_configuration()

 def test_twelve_complete_tops_reproducible_and_compilable(self):
  from generate_rtl import generate_bundle
  with tempfile.TemporaryDirectory() as td:
   root=Path(td)
   first=generate_bundle(root/'first',self.config)
   second=generate_bundle(root/'second',self.config)
   self.assertEqual(first,second)
   self.assertEqual(len(first['targets']),12)
   for relative,digest in first['sha256'].items():
    data=(root/'first'/relative).read_bytes()
    self.assertEqual(hashlib.sha256(data).hexdigest(),digest)
    self.assertEqual(data,(root/'second'/relative).read_bytes())
   for target in first['targets']:
    paths=[root/'first'/x for x in target['sources']]
    self.assertTrue(all(x.is_file() for x in paths))
    if shutil.which('iverilog'):
     p=subprocess.run(['iverilog','-g2012','-s',target['top'],'-o',str(root/'top'),*map(str,paths)],capture_output=True,text=True)
     self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   selections=json.loads((root/'first/config/selection.json').read_text())
   self.assertEqual(selections['circuits']['sharpen']['exact_nodes'],[1,2,3])
   self.assertEqual(selections['circuits']['dct']['unit_node'],8)
   self.assertIn('inverse DCT reconstruction',first['scope']['excluded'])

 def test_changed_frozen_choice_rejected_before_writing(self):
  from generate_rtl import generate_bundle
  config=copy.deepcopy(self.config)
  config['circuits']['sharpen_mixed']['bit']=6
  with tempfile.TemporaryDirectory() as td:
   out=Path(td)/'rejected'
   with self.assertRaisesRegex(ValueError,'recomputation'):
    generate_bundle(out,config)
   self.assertFalse(out.exists())

if __name__=='__main__': unittest.main()
