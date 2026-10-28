"""Generate current bitflip hardware in byte-preserved legacy arithmetic cores.

No synthesis, external builder execution, image/model or interval-search work.
"""
import json
from pathlib import Path
import re
import shutil

from bitflip_area_design import (ROOT, DEFAULT_CONFIG, APPLICATIONS, KINDS,
    load_config, sha256, overlap, bus, part, instance, module)

LEGACY_SOURCES=ROOT/'hardware/legacy_area'
SCOPE='single_arithmetic_kernel_not_full_image_system'


def components(app):
    name=app['application'];nodes=app['nodes'];native=32 if name=='dct' else 13
    widths=[13 if n['monitor']=='baseline_output' else 12 for n in nodes]
    bits=sum(widths);count=len(nodes);total=native*count
    tops={k:name+'_'+k for k in KINDS};tops['host_baseline']=name+'_baseline'
    clk=['input wire clk','input wire cfg_load']
    cfg=[bus('cfg_lower_bus',bits),bus('cfg_upper_bus',bits)]
    bound_ports=[bus('lower_bus',bits),bus('upper_bus',bits)]
    data=[bus('monitor_bus',total),bus('baseline_bus',total),bus('y',total,'output')]
    logic=[];storage=[];offset=0;profiles=[]
    for i,(node,w) in enumerate(zip(nodes,widths)):
        lo=part('lower_bus',offset,w);hi=part('upper_bus',offset,w)
        mon=part('monitor_bus',i*native,native);base=part('baseline_bus',i*native,native)
        logic += [f'  wire active_{i} = ($signed({mon}) >= $signed({lo})) && ($signed({mon}) <= $signed({hi}));',
            f"  assign {part('y',i*native,native)} = {base} ^ (active_{i} ? {native}'d{1<<node['bit']} : {native}'d0);"]
        storage.append(instance('bitflip_bounds',f'bounds_{i}',dict(clk='clk',cfg_load='cfg_load',
            cfg_lower=part('cfg_lower_bus',offset,w),cfg_upper=part('cfg_upper_bus',offset,w),
            lower=lo,upper=hi),f'#(.WIDTH({w}))'))
        profiles.append(dict(**node,monitor_width=w,native_monitor_width=native,offset=offset))
        offset+=w
    text=module(tops['attack_comb'],data+bound_ports,logic)
    text+=module(tops['config_storage'],clk+cfg+[bus('lower_bus',bits,'output'),bus('upper_bus',bits,'output')],storage)
    storage_instance=instance(tops['config_storage'],'configuration',dict(clk='clk',cfg_load='cfg_load',
        cfg_lower_bus='cfg_lower_bus',cfg_upper_bus='cfg_upper_bus',lower_bus='lower_bus',upper_bus='upper_bus'))
    wires=[f'  wire [{bits-1}:0] lower_bus,upper_bus;',storage_instance]
    text+=module(tops['attack_complete'],clk+cfg+data,wires+[
        instance(tops['attack_comb'],'payload',dict(monitor_bus='monitor_bus',baseline_bus='baseline_bus',
            lower_bus='lower_bus',upper_bus='upper_bus',y='y'))])
    return text,dict(application=name,nodes=count,monitor_bits=bits,config_bits=2*bits,
        native_value_width=native,total_add_nodes=APPLICATIONS[name],node_profiles=profiles,tops=tops),wires,clk+cfg


def attacked_kernel(baseline,meta,wires,ports):
    name=meta['application'];native=meta['native_value_width']
    header='module '+name+'_baseline('
    if baseline.count(header)!=1:raise ValueError('Unexpected legacy baseline module')
    text=baseline.replace(header,'module '+meta['tops']['host_attacked']+'(\n  '+',\n  '.join(ports)+',',1)
    close=text.find(');')
    if close<0:raise ValueError('Missing legacy baseline port boundary')
    text=text[:close+2]+'\n'+'\n'.join(wires)+'\n'+text[close+2:]
    for p in meta['node_profiles']:
        node=p['node_id'];w=p['monitor_width'];offset=p['offset']
        pattern=rf'(?m)^  wire signed \[{native-1}:0\] n{node} = ([^;\n]+);$'
        matches=re.findall(pattern,text)
        if len(matches)!=1:raise ValueError(f'Unexpected legacy node assignment: {name}:{node}')
        monitor={'input1':f'n{node}_a','input2':f'n{node}_b','baseline_output':f'n{node}_before_flip'}[p['monitor']]
        lo=part('lower_bus',offset,w);hi=part('upper_bus',offset,w)
        replacement=(f'  wire signed [{native-1}:0] n{node}_before_flip = {matches[0]};\n'
            f'  wire n{node}_active = ($signed({monitor}) >= $signed({lo})) && ($signed({monitor}) <= $signed({hi}));\n'
            f"  wire signed [{native-1}:0] n{node} = n{node}_before_flip ^ (n{node}_active ? {native}'d{1<<p['bit']} : {native}'d0);")
        text=re.sub(pattern,lambda _:replacement,text)
    return text


def generate_design(output,config_path=None):
    output=Path(output).resolve();config_path=Path(config_path or DEFAULT_CONFIG).resolve()
    if output.exists():raise ValueError('Design output exists; choose a fresh directory')
    if any(p.casefold()=='sources' for p in output.parts) or any(overlap(output,p.resolve()) for p in
            [config_path,ROOT/'hardware',ROOT/'third_party/evoapproxlib']):
        raise ValueError('Design output overlaps source/configuration')
    digest=sha256(config_path);config=load_config(config_path)
    if sha256(config_path)!=digest:raise ValueError('Configuration changed while loading')
    provenance=LEGACY_SOURCES/'provenance.json'
    reference=json.loads(provenance.read_text(encoding='utf-8'))
    expected={name+'_baseline.v' for name in APPLICATIONS}|{'evo_5RP.v'}
    if set(reference['files'])!=expected:raise ValueError('Unexpected legacy reference files')
    sources=[provenance,ROOT/'hardware/rtl/bitflip_trigger.sv']
    for name,want in reference['files'].items():
        path=LEGACY_SOURCES/name
        if not path.is_file() or sha256(path)!=want:raise ValueError('Legacy source fingerprint mismatch: '+name)
        sources.append(path)
    before={str(p):sha256(p) for p in sources}
    generated=[]
    for app in config['applications']:
        text,meta,wires,ports=components(app)
        baseline=(LEGACY_SOURCES/(app['application']+'_baseline.v')).read_text(encoding='utf-8')
        text+=attacked_kernel(baseline,meta,wires,ports)
        meta['historical_baseline_um2']=reference['historical_baseline_um2'][app['application']]
        meta['historical_proposed_reg_um2']=reference['historical_proposed_reg_um2'][app['application']]
        generated.append((text,meta))
    output.mkdir(parents=True);(output/'rtl').mkdir()
    shutil.copy2(config_path,output/'hardware_config.json')
    if sha256(output/'hardware_config.json')!=digest:raise ValueError('Configuration changed during snapshot')
    files=[]
    for source in sources[1:]:
        shutil.copy2(source,output/'rtl'/source.name);files.append('rtl/'+source.name)
        if sha256(output/'rtl'/source.name)!=before[str(source)]:raise ValueError('Legacy snapshot fingerprint mismatch')
    apps=[]
    for text,meta in generated:
        path=output/'rtl'/(meta['application']+'_legacy_bitflip.sv')
        path.write_text(text,encoding='utf-8');files.append(path.relative_to(output).as_posix());apps.append(meta)
    if any(sha256(Path(p))!=h for p,h in before.items()):raise ValueError('Legacy sources changed during snapshot')
    manifest=dict(schema='bitflip_area_design_v1',scope=SCOPE,configuration=str(config_path),
        config_sha256=digest,config_snapshot='hardware_config.json',source_fingerprints=config['source_fingerprints'],
        source_dependencies=list(before),rtl_files=files,applications=apps,legacy_reference=reference,
        liberty_sha256=reference['liberty_sha256'],runtime_roi_hardware=False,
        comparison='legacy full arithmetic kernel, not a frame-level image system; current bounds preserved')
    (output/'design_manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    return manifest
