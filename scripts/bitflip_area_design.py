"""Generate scoped area RTL only; never invoke a simulator or synthesis tool."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[1]
DEFAULT_CONFIG=ROOT/'hardware/bitflip_area/current_hardware.json'
APPLICATIONS={'sharpen':4,'conv':8,'sobel':11,'dct':32}
APPROXIMATE_NODES={'sharpen':{0,1,2,3},'conv':{1,2,4,7},'sobel':{0,2,4,7,9},
                   'dct':{8,10,11,13,16,19,22,25,28,31}}
KINDS=('attack_comb','config_storage','attack_complete','host_baseline','host_attacked')
MODEL_FILES=('src/applications/sharpen.cpp','src/applications/conv.cpp','src/applications/sobel.cpp',
             'src/applications/dct8_fixed_graph.cpp','src/approximate/evoapprox_adapter.cpp',
             'third_party/evoapproxlib/add12se_5RP.c')

def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def normalized_hash(path):
    return hashlib.sha256(path.read_text(encoding='utf-8').replace('\r\n','\n').encode()).hexdigest()

def overlap(a,b):
    return a==b or a in b.parents or b in a.parents

def load_config(path):
    config=json.loads(path.read_text(encoding='utf-8-sig'))
    if config.get('schema')!='fixed_bitflip_area_config_v1':
        raise ValueError('Unsupported fixed hardware configuration')
    fingerprints=config.get('source_fingerprints',{})
    if set(fingerprints)!=set(MODEL_FILES):
        raise ValueError('Missing baseline/model source fingerprints')
    for name in MODEL_FILES:
        if normalized_hash(ROOT/name)!=fingerprints[name]:
            raise ValueError('Baseline source changed; revalidate unit profiles: '+name)
    apps=config.get('applications',[])
    if [a.get('application') for a in apps]!=list(APPLICATIONS):
        raise ValueError('Expected all four applications in canonical order')
    for app in apps:
        name=app['application'];nodes=app.get('nodes',[]);seen=set()
        if not nodes:raise ValueError('No installed nodes: '+name)
        for node in nodes:
            index=node.get('node_id');bit=node.get('bit');monitor=node.get('monitor')
            if type(index) is not int or not 0<=index<APPLICATIONS[name] or index in seen:
                raise ValueError('Invalid/duplicate node: '+name)
            if type(bit) is not int or not 0<=bit<12:
                raise ValueError('Flip bit must be fixed non-sign bit0..11')
            if monitor not in ('input1','input2','baseline_output'):
                raise ValueError('Invalid fixed monitor')
            if any(k in node for k in ('lower','upper')):
                raise ValueError('Bounds must remain writable, not hardware constants')
            seen.add(index)
        app['nodes']=sorted(nodes,key=lambda n:n['node_id'])
    return config

def bus(name,width,direction='input'):
    return f'{direction} wire [{width-1}:0] {name}'

def part(name,offset,width):
    return f'{name}[{offset+width-1}:{offset}]'

def instance(module,name,ports,parameters=''):
    return f'  {module} {parameters} {name}('+', '.join(f'.{p}({v})' for p,v in ports.items())+');'

def module(name,ports,body):
    return 'module '+name+'(\n  '+',\n  '.join(ports)+'\n);\n'+'\n'.join(body)+'\nendmodule\n'

def application_rtl(app):
    name=app['application'];nodes=app['nodes'];n=len(nodes)
    widths=[13 if r['monitor']=='baseline_output' else 12 for r in nodes]
    m=sum(widths);y=13*n;x=12*n
    tops={kind:name+'_'+kind for kind in KINDS}
    clk=['input wire clk','input wire cfg_load']
    cfg=[bus('cfg_lower_bus',m),bus('cfg_upper_bus',m)]
    body=[];offset=0
    for i,(r,w) in enumerate(zip(nodes,widths)):
        body.append(instance('bitflip_trigger',f'trigger_{i}',dict(monitor_value=part('monitor_bus',offset,w),
            lower=part('lower_bus',offset,w),upper=part('upper_bus',offset,w),
            baseline_value=part('baseline_bus',13*i,13),attacked_value=part('y',13*i,13)),
            f'#(.MONITOR_WIDTH({w}), .FLIP_BIT({r["bit"]}))'))
        offset+=w
    text=module(tops['attack_comb'],[bus('monitor_bus',m),bus('baseline_bus',y),bus('lower_bus',m),bus('upper_bus',m),bus('y',y,'output')],body)
    body=[];offset=0
    for i,w in enumerate(widths):
        body.append(instance('bitflip_bounds',f'bounds_{i}',dict(clk='clk',cfg_load='cfg_load',
            cfg_lower=part('cfg_lower_bus',offset,w),cfg_upper=part('cfg_upper_bus',offset,w),
            lower=part('lower_bus',offset,w),upper=part('upper_bus',offset,w)),f'#(.WIDTH({w}))'))
        offset+=w
    text+=module(tops['config_storage'],clk+cfg+[bus('lower_bus',m,'output'),bus('upper_bus',m,'output')],body)
    body=[f'  wire [{m-1}:0] lower_bus, upper_bus;',
          instance(tops['config_storage'],'configuration',dict(clk='clk',cfg_load='cfg_load',cfg_lower_bus='cfg_lower_bus',cfg_upper_bus='cfg_upper_bus',lower_bus='lower_bus',upper_bus='upper_bus')),
          instance(tops['attack_comb'],'payload',dict(monitor_bus='monitor_bus',baseline_bus='baseline_bus',lower_bus='lower_bus',upper_bus='upper_bus',y='y'))]
    text+=module(tops['attack_complete'],clk+cfg+[bus('monitor_bus',m),bus('baseline_bus',y),bus('y',y,'output')],body)
    body=[]
    for i,r in enumerate(nodes):
        a,b,dest=part('a_bus',12*i,12),part('b_bus',12*i,12),part('y',13*i,13)
        if r['node_id'] in APPROXIMATE_NODES[name]:
            body.append(instance('add12se_5RP',f'unit_{i}',dict(a=a,b=b,y=dest)))
        else:
            body.append(f'  assign {dest} = $signed({{a_bus[{12*i+11}], {a}}}) + $signed({{b_bus[{12*i+11}], {b}}});')
    text+=module(tops['host_baseline'],[bus('a_bus',x),bus('b_bus',x),bus('y',y,'output')],body)
    body=[f'  wire [{y-1}:0] baseline_bus;',f'  wire [{m-1}:0] monitor_bus;',
          instance(tops['host_baseline'],'original_units',dict(a_bus='a_bus',b_bus='b_bus',y='baseline_bus'))]
    offset=0
    for i,(r,w) in enumerate(zip(nodes,widths)):
        value=part('baseline_bus',13*i,13) if w==13 else part('a_bus' if r['monitor']=='input1' else 'b_bus',12*i,12)
        body.append(f'  assign {part("monitor_bus",offset,w)} = {value};')
        offset+=w
    body.append(instance(tops['attack_complete'],'programmable_attack',dict(clk='clk',cfg_load='cfg_load',cfg_lower_bus='cfg_lower_bus',cfg_upper_bus='cfg_upper_bus',monitor_bus='monitor_bus',baseline_bus='baseline_bus',y='y')))
    text+=module(tops['host_attacked'],clk+cfg+[bus('a_bus',x),bus('b_bus',x),bus('y',y,'output')],body)
    profiles=[dict(**r,monitor_width=w,baseline_unit='add12se_5RP' if r['node_id'] in APPROXIMATE_NODES[name] else 'exact_signed12_add') for r,w in zip(nodes,widths)]
    return text,dict(application=name,nodes=n,config_bits=2*m,monitor_bits=m,tops=tops,
        exact_nodes=sum(r['node_id'] not in APPROXIMATE_NODES[name] for r in nodes),
        approximate_nodes=sum(r['node_id'] in APPROXIMATE_NODES[name] for r in nodes),node_profiles=profiles)

def generate_design(output,config_path=None):
    output=Path(output).resolve();path=Path(config_path or DEFAULT_CONFIG).resolve()
    if output.exists():raise ValueError('Design output exists; choose a fresh directory')
    if any(p.casefold()=='sources' for p in output.parts) or any(overlap(output,r.resolve()) for r in (path,ROOT/'hardware/rtl',ROOT/'third_party/evoapproxlib')):
        raise ValueError('Design output overlaps read-only model/configuration sources')
    config_hash=sha256(path)
    config=load_config(path)
    rtl=[ROOT/'hardware/rtl'/n for n in ('bitflip_trigger.sv','add12se_5RP.sv')]
    if not all(p.is_file() for p in rtl):raise ValueError('Missing area RTL source')
    generated=[application_rtl(app) for app in config['applications']]
    if sha256(path)!=config_hash:raise ValueError('Configuration changed while generating RTL')
    output.mkdir(parents=True,exist_ok=False);(output/'rtl').mkdir()
    shutil.copy2(path,output/'hardware_config.json')
    if sha256(output/'hardware_config.json')!=config_hash:
        raise ValueError('Configuration changed while taking its snapshot')
    files=[]
    for source in rtl:
        target=output/'rtl'/source.name;shutil.copy2(source,target);files.append('rtl/'+source.name)
    apps=[]
    for text,meta in generated:
        target=output/'rtl'/f"{meta['application']}_tops.sv";target.write_text('// Selected-unit bank only; not a complete application.\n'+text,encoding='utf-8')
        files.append(target.relative_to(output).as_posix());apps.append(meta)
    manifest=dict(schema='bitflip_area_design_v1',scope='selected_unit_bank_not_full_application',
        configuration=str(path),config_sha256=config_hash,config_snapshot='hardware_config.json',source_fingerprints=config['source_fingerprints'],
        rtl_files=files,rtl_sha256={f:sha256(output/f) for f in files},applications=apps,
        runtime_roi_hardware=False,configuration_loading='one parallel cfg_load; no reset; load bounds before processing',
        comparison='independent selected arithmetic units; no image pipeline, memory or host bus')
    (output/'design_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    return manifest
