"""Pinned Nangate45 provenance and Liberty/LEF area verification; no tools run."""
import hashlib
import json
import math
from pathlib import Path
import re

UPSTREAM_REPOSITORY='The-OpenROAD-Project/OpenROAD-flow-scripts'
UPSTREAM_COMMIT='64939f3717ad7c694199ba04cb98924b98022868'
PINNED_FILES={
    'liberty':dict(path='flow/platforms/nangate45/lib/NangateOpenCellLibrary_typical.lib',
        name='NangateOpenCellLibrary_typical.lib',blob='90e4afb274cfc6514c587c434c77903d554117d1'),
    'lef':dict(path='flow/platforms/nangate45/lef/NangateOpenCellLibrary.macro.mod.lef',
        name='NangateOpenCellLibrary.macro.mod.lef',blob='b9ff5073223361e1ba883286d260c598c8217b23'),
    'license':dict(path='flow/platforms/nangate45/LICENSE',name='LICENSE',
        blob='f433b1a53f5b830a205fd2df78e2b34974656c7b'),
}

def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def git_blob_sha(data):
    return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()

def strip_comments(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)

def groups(text,kind):
    pattern=re.compile(r'\b'+kind+r'\s*\(\s*"?([A-Za-z_][A-Za-z0-9_]*)"?\s*\)\s*\{')
    for match in pattern.finditer(text):
        start=match.end();depth=1;quoted=False;escaped=False
        for i in range(start,len(text)):
            char=text[i]
            if escaped:escaped=False;continue
            if quoted and char=='\\':escaped=True;continue
            if char=='"':quoted=not quoted
            if not quoted:
                if char=='{':depth+=1
                elif char=='}':depth-=1
            if depth==0:
                yield match.group(1),text[start:i]
                break
        else:raise ValueError('Unterminated Liberty '+kind+' block')

def scalar(text,name,positive=False):
    matches=re.findall(r'\b'+re.escape(name)+r'\s*:\s*([^;]+);',text)
    if len(matches)!=1:raise ValueError('Missing or ambiguous Liberty '+name)
    try:value=float(matches[0].strip().strip('"'))
    except ValueError:raise ValueError('Invalid Liberty '+name) from None
    if not math.isfinite(value) or (positive and value<=0):raise ValueError('Invalid Liberty '+name)
    return value

def inspect_library(liberty,lef):
    text=strip_comments(liberty);areas={};sequential=[]
    for name,body in groups(text,'cell'):
        if name in areas:raise ValueError('Duplicate Liberty cell '+name)
        areas[name]=scalar(body,'area',True)
        if re.search(r'\b(ff|latch)\s*\(',body):sequential.append(name)
    if not areas:raise ValueError('No standard-cell areas in Liberty')
    geometries={}
    for match in re.finditer(r'(?m)^\s*MACRO\s+([A-Za-z_][A-Za-z0-9_]*)\s*$',lef):
        name=match.group(1)
        end=re.search(r'(?m)^\s*END\s+'+re.escape(name)+r'\s*$',lef[match.end():])
        if not end:raise ValueError('Unterminated LEF macro '+name)
        body=lef[match.end():match.end()+end.start()]
        size=re.search(r'\bSIZE\s+([0-9.eE+-]+)\s+BY\s+([0-9.eE+-]+)\s*;',body)
        if not size:raise ValueError('Missing LEF geometry '+name)
        width,height=map(float,size.groups())
        if not all(math.isfinite(v) and v>0 for v in (width,height)) or name in geometries:
            raise ValueError('Invalid/duplicate LEF macro '+name)
        geometries[name]=width*height
    common=sorted(set(areas)&set(geometries))
    if len(common)<3 or not {'NAND2_X1','DFF_X1'}.issubset(common):
        raise ValueError('Need matching NAND2/DFF and at least three LEF macro areas')
    for name in common:
        if not math.isclose(areas[name],geometries[name],rel_tol=1e-6,abs_tol=1e-7):
            raise ValueError('Liberty/LEF area mismatch: '+name)
    voltage=scalar(text,'nom_voltage',True);temperature=scalar(text,'nom_temperature')
    time_unit=re.search(r'\btime_unit\s*:\s*"([^"\n]+)"\s*;',text)
    if not time_unit:raise ValueError('Missing Liberty time_unit')
    return dict(area_unit='um^2',area_unit_validation='Liberty area agrees with LEF SIZE in microns',
        operating_conditions=dict(voltage_v=voltage,temperature_c=temperature,time_unit=time_unit.group(1)),
        cell_areas=areas,sequential_cells=sorted(sequential),lef_verified_cells=common,
        sequential_cell_bits={name:1 for name in sequential})

def make_library_manifest(directory):
    directory=Path(directory).resolve();paths={}
    for role,info in PINNED_FILES.items():
        path=directory/info['name']
        if not path.is_file() or git_blob_sha(path.read_bytes())!=info['blob']:
            raise ValueError('Pinned Nangate45 Git blob mismatch: '+role)
        paths[role]=path
    inspected=inspect_library(paths['liberty'].read_text(),paths['lef'].read_text())
    return dict(schema='nangate45_library_v1',source_repository=UPSTREAM_REPOSITORY,source_commit=UPSTREAM_COMMIT,
        **{role:info['name'] for role,info in PINNED_FILES.items()},
        **{role+'_sha256':sha256(path) for role,path in paths.items()},
        source_git_blobs={role:info['blob'] for role,info in PINNED_FILES.items()},
        **inspected,paper_alignment='FreePDK45-compatible open library; exact paper corner/version unspecified')

def load_library_manifest(path):
    path=Path(path).resolve();provided=json.loads(path.read_text(encoding='utf-8-sig'))
    if provided.get('schema')!='nangate45_library_v1' or provided.get('source_commit')!=UPSTREAM_COMMIT or provided.get('source_repository')!=UPSTREAM_REPOSITORY:
        raise ValueError('Unsupported or unpinned Nangate45 manifest')
    for role,info in PINNED_FILES.items():
        if provided.get(role)!=info['name'] or provided.get(role+'_sha256')!=sha256(path.parent/info['name']):
            raise ValueError('Library hash or path disagrees: '+role)
    actual=make_library_manifest(path.parent)
    # Recompute all derived claims; never trust an edited list of cell areas.
    if actual!=provided:raise ValueError('Library manifest metadata disagrees with source files')
    actual.update({role+'_path':str(path.parent/info['name']) for role,info in PINNED_FILES.items()})
    actual['manifest_path']=str(path)
    return actual
