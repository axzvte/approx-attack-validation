#!/usr/bin/env python3
"""Manual pinned library download/verification only. Never invokes synthesis."""
import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile
import urllib.request

from area_library import PINNED_FILES,UPSTREAM_COMMIT,UPSTREAM_REPOSITORY,git_blob_sha,load_library_manifest,make_library_manifest

def fetch_library(output):
    output=Path(output).resolve()
    if output.exists():
        return load_library_manifest(output/'library_manifest.json')
    if any(part.casefold()=='sources' for part in output.parts):
        raise ValueError('Synced sources are read-only')
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.nangate45-',dir=output.parent) as temporary:
        stage=Path(temporary)
        for role,info in PINNED_FILES.items():
            url=f'https://raw.githubusercontent.com/{UPSTREAM_REPOSITORY}/{UPSTREAM_COMMIT}/{info["path"]}'
            request=urllib.request.Request(url,headers={'User-Agent':'fixed-bitflip-area-library-fetch'})
            with urllib.request.urlopen(request,timeout=120) as response:
                data=response.read(20*1024*1024+1)
            if len(data)>20*1024*1024 or git_blob_sha(data)!=info['blob']:
                raise ValueError('Downloaded library does not match pinned Git blob: '+role)
            (stage/info['name']).write_bytes(data)
        manifest=make_library_manifest(stage)
        (stage/'library_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
        # Existing directories are never overwritten. Publish the completion
        # manifest last; an interrupted copy must not look like a valid cache.
        output.mkdir(exist_ok=False)
        for info in PINNED_FILES.values():
            shutil.copy2(stage/info['name'],output/info['name'])
        shutil.copy2(stage/'library_manifest.json',output/'library_manifest.json')
    return load_library_manifest(output/'library_manifest.json')

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--check',action='store_true',help='Verify an existing library, without network access')
    args=parser.parse_args(argv)
    try:
        meta=load_library_manifest(args.output/'library_manifest.json') if args.check else fetch_library(args.output)
        print(json.dumps(dict(manifest=str(args.output.resolve()/'library_manifest.json'),area_unit=meta['area_unit'],
            operating_conditions=meta['operating_conditions'],source_commit=meta['source_commit']),indent=2))
        return 0
    except (OSError,ValueError,KeyError) as exc:
        print('Library preparation failed: '+str(exc),file=sys.stderr);return 1

if __name__=='__main__':raise SystemExit(main())
