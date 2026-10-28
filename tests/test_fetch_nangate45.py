"""Download contract fixtures: no network connections or synthesis calls."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import fetch_nangate45 as fetch
from area_library import git_blob_sha

class FetchTests(unittest.TestCase):
    def test_all_payloads_are_verified_before_new_cache_and_manifest(self):
        data=[b'fixture liberty',b'fixture lef',b'fixture license']
        pins={r:dict(path=r,name=r+'.txt',blob=git_blob_sha(d)) for r,d in zip(('liberty','lef','license'),data)}
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp)/'cache';meta={'schema':'fixture'}
            with patch.object(fetch,'PINNED_FILES',pins),patch.object(fetch,'make_library_manifest',return_value=meta), \
                 patch.object(fetch,'load_library_manifest',return_value=meta), \
                 patch.object(fetch.urllib.request,'urlopen',side_effect=[io.BytesIO(d) for d in data]) as network:
                self.assertEqual(fetch.fetch_library(out),meta)
            self.assertEqual(network.call_count,3)
            self.assertEqual(json.loads((out/'library_manifest.json').read_text()),meta)
            for role,d in zip(pins,data):self.assertEqual((out/pins[role]['name']).read_bytes(),d)

    def test_corrupt_download_creates_no_cache_and_existing_cache_is_untouched(self):
        pins={'liberty':dict(path='lib',name='lib.txt',blob=git_blob_sha(b'expected'))}
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp)/'cache'
            with patch.object(fetch,'PINNED_FILES',pins),patch.object(fetch.urllib.request,'urlopen',return_value=io.BytesIO(b'wrong')):
                with self.assertRaises(ValueError):fetch.fetch_library(out)
            self.assertFalse(out.exists())
            out.mkdir();keep=out/'keep';keep.write_text('original')
            with patch.object(fetch,'load_library_manifest',side_effect=ValueError('Incomplete existing cache')), \
                 patch.object(fetch.urllib.request,'urlopen') as network:
                with self.assertRaises(ValueError):fetch.fetch_library(out)
                network.assert_not_called()
            self.assertEqual(keep.read_text(),'original')

if __name__=='__main__':unittest.main()
