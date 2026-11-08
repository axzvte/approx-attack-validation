"""Generate static paired RTL from source IR and the frozen processing choices."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from rtl_stencil import emit_stencil, position_rtl
from rtl_units import generate_units

APPS=('sharpen','conv','sobel','dct')
VERSIONS=('baseline','compensated','processed')
BBOX=[120,135,193,201]


def default_configuration():
    from circuit_sources import load_profile
    from fixed_roi_pipeline import plan_profiles,profile_digest
    profiles={f'{app}_mixed':load_profile(ROOT/f'configs/circuits/{app}_mixed.json') for app in APPS}
    plans=plan_profiles(profiles,4818,320*320,4,512,128)
    return dict(schema=3,grid=320,bbox_half_open=BBOX,input_circuits=profiles,
                input_circuits_sha256=profile_digest(profiles),circuits=plans)


def checked_configuration(config):
    import circuit_ir
    from circuit_sources import load_profile
    from fixed_roi_pipeline import plan_profiles,profile_digest
    if config.get('schema')!=3 or config.get('grid')!=320:
        raise ValueError('RTL requires schema3 and normalized320 geometry')
    bbox=config['bbox_half_open']
    if len(bbox)!=4 or any(type(x)!=int for x in bbox) or not 0<=bbox[0]<bbox[2]<=320 or not 0<=bbox[1]<bbox[3]<=320:
        raise ValueError('Invalid ROI rectangle')
    profiles=config['input_circuits']
    if config.get('input_circuits_sha256')!=profile_digest(profiles): raise ValueError('Frozen input profiles changed')
    if set(profiles)!={f'{app}_mixed' for app in APPS}:
        raise ValueError('Initial RTL backend requires all four existing mixed source profiles')
    for app in APPS:
        name=f'{app}_mixed'
        circuit_ir.validate_profile(profiles[name])
        current=load_profile(ROOT/f'configs/circuits/{app}_mixed.json')
        def semantic(p):
            # Ignore source paths/call stacks, retain arithmetic/type/topology/baseline.
            q=json.loads(json.dumps(p))
            q.pop('source',None)
            for operation in q['ir']['operations']:
                operation.pop('source_coord',None); operation.pop('call_stack',None)
            return q
        if semantic(profiles[name])!=semantic(current):
            raise ValueError(f'{app}: frozen circuit does not match current source kernel and baseline')
    area=(bbox[2]-bbox[0])*(bbox[3]-bbox[1])
    plans=plan_profiles(profiles,area,320*320,config.get('min_bit',4),config.get('max_evaluations',512),config.get('max_candidates',128))
    for name,choice in plans.items():
        stored=config['circuits'][name]
        # Coordinates depend on host location; all decisions and bound checks must match.
        def decisions(p): return {k:v for k,v in p.items() if k not in ('source_coord','call_stack')}
        if decisions(choice)!=decisions(stored): raise ValueError(f'{name}: frozen choices failed recomputation')
        if stored['mode']!='ir_internal': raise ValueError(f'{name}: output fallback is outside this RTL backend')
    return config


def generate_bundle(output,config=None):
    from rtl_dct import emit_dct
    config=checked_configuration(config or default_configuration())
    output=Path(output)
    sources={}
    def write(relative,text):
        relative=Path(relative)
        path=output/relative
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(text,encoding='utf-8',newline='\n')
        sources[relative.as_posix()]=hashlib.sha256(text.encode()).hexdigest()
    write('rtl/common/evoapprox_units.sv',generate_units(ROOT))
    write('rtl/common/roi_position.sv',position_rtl())
    selections={}
    targets=[]
    for app in APPS:
        profile=config['input_circuits'][f'{app}_mixed']
        plan=config['circuits'][f'{app}_mixed']
        selections[app]={k:plan[k] for k in ('node','bit','unit_node','exact_nodes','inside_error_bound','outside_error_bound','analysis_evaluations')}
        selections[app]['baseline']=profile['baseline']
        selections[app]['source_sha256']=hashlib.sha256((ROOT/f'kernels/{app}.c').read_text(encoding='utf-8').encode()).hexdigest()
        for version in VERSIONS:
            text=emit_dct(ROOT,profile,plan,version,config['bbox_half_open']) if app=='dct' else emit_stencil(profile,plan,app,version,config['bbox_half_open'])
            relative=f'rtl/{app}/{version}.sv'
            write(relative,text)
            targets.append(dict(application=app,version=version,top=f'{app}_{version}',sources=['rtl/common/evoapprox_units.sv','rtl/common/roi_position.sv',relative]))
    selection=dict(schema=1,grid=320,bbox_half_open=config['bbox_half_open'],circuits=selections)
    write('config/selection.json',json.dumps(selection,indent=2,sort_keys=True)+'\n')
    manifest=dict(schema=1,language='SystemVerilog-2012',targets=targets,sha256=sources,
        scope=dict(stencils='registered arithmetic core plus raster/ROI control; external input windows',
                   dct='reused32-group forward2D DCT core plus row storage and raster/ROI/schedule control; external padded blocks',
                   excluded=['inverse DCT reconstruction','external window/padding generators','full-frame memory','host interfaces']),
        interface=dict(dimension_bits=16,dimension_range=[1,65535],frame_start='latches dimensions and aborts prior work; accepts no sample',
                       input_handshake='in_valid && in_ready',output_backpressure=False,stencil_latency='accepted rising edge registers pixel/out_valid',
                       dct_latency='16 compute cycles after block acceptance; pulse out_valid after column7',
                       dct_input='64 unsigned8 pixels in row-major8x8 lanes',dct_output='64 signed32 forward coefficients in row-major lanes'))
    # manifest hashes every generated payload; it is not self-hashed.
    path=output/'manifest.json'; path.write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frozen',type=Path,help='Existing schema3 frozen configuration; no images are read')
    parser.add_argument('--output',type=Path,default=ROOT/'hardware')
    args=parser.parse_args()
    try:
        config=json.loads(args.frozen.read_text(encoding='utf-8')) if args.frozen else None
        manifest=generate_bundle(args.output,config)
        print(f"Generated {len(manifest['targets'])} RTL tops in {args.output}")
    except (ValueError,KeyError,OSError) as exc: parser.exit(1,f'{exc}\n')

if __name__=='__main__': main()
