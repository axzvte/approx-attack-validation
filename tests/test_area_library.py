import importlib.util
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
LIB='''library (NangateOpenCellLibrary) {
nom_voltage : 1.10;
nom_temperature : 25.0;
time_unit : "1ns";
cell (NAND2_X1) { area : 0.798; pin (Y) { function : "!(A & B)"; } }
cell (DFF_X1) { area : 1.064; ff (IQ, IQN) { next_state : "D"; } }
cell (BUF_X1) { area : 0.532; }
}'''
LEF='''VERSION 5.8 ;
MACRO NAND2_X1
 SIZE 0.57 BY 1.4 ;
END NAND2_X1
MACRO DFF_X1
 SIZE 0.76 BY 1.4 ;
END DFF_X1
MACRO BUF_X1
 SIZE 0.38 BY 1.4 ;
END BUF_X1
'''

class LibraryTests(unittest.TestCase):
    def module(self):
        path=ROOT/'scripts/area_library.py'
        self.assertTrue(path.is_file(),'Missing Liberty/LEF area verification')
        spec=importlib.util.spec_from_file_location('area_library',path)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        return module

    def test_area_units_require_matching_lef_and_operating_conditions(self):
        meta=self.module().inspect_library(LIB,LEF)
        self.assertEqual(meta['area_unit'],'um^2')
        self.assertAlmostEqual(meta['cell_areas']['NAND2_X1'],.798)
        self.assertIn('DFF_X1',meta['sequential_cells'])
        self.assertEqual(meta['operating_conditions']['temperature_c'],25)

    def test_mismatched_area_or_missing_operating_conditions_rejected(self):
        m=self.module()
        for lib,lef in [(LIB,LEF.replace('.57 BY','1.57 BY')),
                        (LIB.replace('nom_voltage : 1.10;',''),LEF),
                        (LIB.replace('area : 0.798','area : nan'),LEF)]:
            with self.assertRaises(ValueError):m.inspect_library(lib,lef)

    def test_manifest_recomputes_hashes_geometry_and_derived_area(self):
        m=self.module()
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);payloads={'liberty':LIB.encode(),'lef':LEF.encode(),'license':b'fixture license'}
            pin={role:dict(name=role+'.txt',path='fixtures/'+role,blob=m.git_blob_sha(data)) for role,data in payloads.items()}
            for role,data in payloads.items():(root/pin[role]['name']).write_bytes(data)
            with patch.object(m,'PINNED_FILES',pin):
                manifest=m.make_library_manifest(root)
                path=root/'library_manifest.json';path.write_text(json.dumps(manifest))
                actual=m.load_library_manifest(path)
                self.assertEqual(actual['liberty_path'],str(root/'liberty.txt'))
                manifest['cell_areas']['NAND2_X1']=999
                path.write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):m.load_library_manifest(path)
                path.write_text(json.dumps(m.make_library_manifest(root)))
                (root/'liberty.txt').write_bytes(payloads['liberty']+b'changed')
                with self.assertRaises(ValueError):m.load_library_manifest(path)

if __name__=='__main__':unittest.main()
