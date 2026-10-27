#!/usr/bin/env python3
"""User-run supplemental completion of Conv's initial four-node ten_h2.

Reuse four verified training outcomes, evaluate only the six missing images,
and compare complete training evidence. No discovery or new-image tests run.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor,as_completed
import csv
import hashlib
import json
import math
from pathlib import Path
import sys
import threading
import time

import run_sharpen_bitflip_hardware as protocol
from run_sharpen_bitflip_batch import atomic_csv,atomic_json,process,read_csv
from bitflip_workflow import _artifact_hashes
from ten_train_report import METRIC_FIELDS,_quality,validate_training_report

TARGET='ten_h2'
CONTROL_NAMES=('train','max_evaluations','max_candidates','initial_candidates','proposals_per_round',
               'max_rounds','patience','workers','scheduling','initial_batch','completion_reserve',
               'context_budget','beam','refinement','restarts','boundary_budget','max_full_evals',
               'full_candidates','global_psnr','convergence_tolerance','seed_limit')
NATIVE_FLAGS={'beam':'beam','refinement':'refinement','restarts':'restarts',
              'boundary_budget':'boundary_budget','context_budget':'context_budget',
              'max_full_evaluations':'max_full_evals','full_candidates':'full_candidates',
              'global_psnr_threshold':'global_psnr','convergence_tolerance':'convergence_tolerance'}


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def overlap(a,b):return a==b or a in b.parents or b in a.parents


def load_json(path):
    value=json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(value,dict):raise ValueError('Expected a JSON object: '+str(path))
    return value


def preflight(args):
    data=args.data.resolve();source=args.source.resolve();out=args.output.resolve()
    if any(part.casefold()=='sources' for part in out.parts):raise ValueError('Synced sources are read-only')
    build=args.build_dir.resolve()
    if any(overlap(out,path) for path in (data,source,build)):
        raise ValueError('Fresh output must not overlap data, source reports or build directory')
    if out.exists():raise ValueError('Output exists; choose a new directory')
    run=load_json(source/'RUN.json');options=run['options'];train=source/'train'
    if not isinstance(options,dict) or not isinstance(run.get('splits'),dict):raise ValueError('Malformed source run options/splits')
    if (run.get('application')!='conv' or run.get('workflow')!='ten_train' or
            run.get('completed') is not True or run.get('all_succeeded') is not True or
            run.get('splits',{}).get('train')!=list(range(1,11))):
        raise ValueError('Source must be a completed Conv ten-image application report')
    manifest=load_json(train/'TASK.json');signature=manifest.get('signature',{})
    if not isinstance(signature,dict):raise ValueError('Malformed original task signature')
    if (manifest.get('version')!=1 or manifest.get('completed') is not True or
            manifest.get('artifacts')!=_artifact_hashes(train) or signature.get('schema')!=2 or
            signature.get('kind')!='ten_image_training' or signature.get('application')!='conv'):
        raise ValueError('Original training task artifacts/signature are missing or changed')
    controls=signature['controls']
    if not isinstance(controls,dict):raise ValueError('Malformed original search controls')
    for name in CONTROL_NAMES:
        original_name={'max_evaluations':'max_train_evaluations','context_budget':'train_context_budget'}.get(name,name)
        if name not in controls or options.get(original_name)!=controls[name]:
            raise ValueError('Original search options disagree with signed task identity: '+name)
    if controls['scheduling']!='interleaved' or controls['initial_candidates']<2 or controls['initial_batch']!=4:
        raise ValueError('Only the original initial-global four-image ten_h2 is supported')
    for name,lo,hi in (('context_budget',1,1000000),('beam',1,64),('refinement',0,10),
                       ('restarts',1,8),('boundary_budget',0,8192),('max_full_evals',1,10000000),('full_candidates',1,1024)):
        value=controls[name]
        if type(value) is not int or not lo<=value<=hi:raise ValueError('Invalid original calibration option: '+name)
    if controls['boundary_budget']==1:raise ValueError('Invalid original boundary budget')
    for name,limit in (('global_psnr',1000),('convergence_tolerance',1)):
        if not math.isfinite(float(controls[name])) or not 0<=float(controls[name])<=limit:
            raise ValueError('Invalid original calibration option: '+name)
    report=validate_training_report(train,'conv',list(range(1,11)),float(controls['global_psnr']),
                                    int(controls['max_evaluations']),int(controls['max_candidates']))
    if report['selected'] is None or run.get('selected_hardware_id')!=report['selected']['hardware_id']:
        raise ValueError('Original selected hardware evidence disagrees')
    if int(report['notes']['actual_evaluations'])!=run.get('actual_train_evaluations'):
        raise ValueError('Original evaluation counts disagree')
    ranking=next((row for row in report['ranking'] if row['hardware_id']==TARGET),None)
    hardware=report['hardware'].get(TARGET,())
    old=[row for row in report['evaluations'] if row['hardware_id']==TARGET]
    if (ranking is None or ranking['status']!='budget_incomplete' or len(old)!=4 or len(hardware)!=4 or
            any(bit!=4 for _,_,bit in hardware)):
        raise ValueError('Source must contain four completed outcomes for the four-node bit4 ten_h2')
    order=[int(value) for value in report['notes']['evaluation_image_order'].split(',')]
    missing=[image for image in order if image not in {int(row['image']) for row in old}]
    if len(missing)!=6:raise ValueError('Expected exactly six missing training images')
    inputs={str(i):digest(protocol.resolve_staged_image_path(data,i)) for i in range(1,11)}
    mask=digest(data/'mask/roi_mask.jpg')
    if (inputs!=run.get('training_input_hashes') or inputs!=signature.get('inputs') or
            mask!=run.get('roi_sha256') or mask!=signature.get('roi') or
            signature.get('source')!=run.get('source_fingerprint')):
        raise ValueError('Original training inputs, ROI or provenance changed')
    train_exe=args.train_exe.resolve() if args.train_exe else build/'circuit_bitflip_ten_train'
    fixed=args.fixed_exe.resolve() if args.fixed_exe else build/'circuit_bitflip_fixed'
    if digest(train_exe)!=signature.get('executable'):
        raise ValueError('Training executable differs from the original; preserve its original binary')
    # Read executable-identity metadata only, never test pixels or test scores.
    identity_path=source/'test'/report['selected']['hardware_id']/'image_31/TASK.json'
    identity=load_json(identity_path);fixed_signature=identity.get('signature',{})
    if not isinstance(fixed_signature,dict):raise ValueError('Malformed evaluator identity metadata')
    if (identity.get('version')!=1 or identity.get('completed') is not True or
            fixed_signature.get('kind')!='fixed_intervals' or fixed_signature.get('application')!='conv' or
            fixed_signature.get('entry_style')!='circuit' or fixed_signature.get('source_sha256')!=signature['source'] or
            fixed_signature.get('mask_sha256')!=mask or digest(fixed)!=fixed_signature.get('executable_sha256')):
        raise ValueError('Fixed evaluator identity differs from the original report')
    workers=args.workers if args.workers is not None else controls['workers']
    if type(workers) is not int or not 1<=workers<=64:raise ValueError('Workers must be inside1..64')
    paths=[source/'RUN.json',train/'TASK.json',identity_path,train_exe,fixed,data/'mask/roi_mask.jpg']
    paths.extend(train/name for name in manifest['artifacts'])
    paths.extend(protocol.resolve_staged_image_path(data,i) for i in range(1,11))
    return dict(data=data,source=source,out=out,run=run,controls=controls,report=report,hardware=hardware,
                missing=missing,workers=workers,fixed=fixed,protected={path:digest(path) for path in paths})


def stable(protected):
    if any(not path.is_file() or digest(path)!=expected for path,expected in protected.items()):
        raise ValueError('Source evidence, executable, training input or fixed hardware changed during completion')


def evaluate(context,image,hardware_path,started,lock):
    out=context['out'];controls=context['controls'];folder=out/'evaluations'/f'image_{image:02d}'
    status=dict(application='conv',hardware_id=TARGET,image=image,status='running',returncode='',detail='',
                elapsed_seconds=0,log=f'logs/image_{image:02d}.log')
    begin=time.monotonic();evaluation=None;configs=[];stats=[]
    try:
        command=[context['fixed'],'conv',context['data'],folder,'--hardware',hardware_path,'--image',image,
                 '--image-mode','none','--continuous']
        for flag,key in (('beam','beam'),('refinement','refinement'),('restarts','restarts'),
                         ('boundary-budget','boundary_budget'),('context-budget','context_budget'),
                         ('max-full-evals','max_full_evals'),('full-candidates','full_candidates'),
                         ('global-psnr','global_psnr'),('convergence-tolerance','convergence_tolerance')):
            command.extend(['--'+flag,controls[key]])
        with lock:started.add(image)
        status['returncode']=process(command,out/status['log'],Path(__file__).resolve().parents[1])
        if status['returncode']:raise RuntimeError('Fixed evaluator returned '+str(status['returncode']))
        tables,row=protocol.fixed_report(folder,image,context['hardware'],'none',f'evaluations/image_{image:02d}',
                                          float(controls['global_psnr']),8,'conv')
        flags=protocol.flags_file(folder/'RUN.txt')
        if any(flags.get(k)!=v for k,v in {'continuous':'1','warm_seeds':'0','input_bits':'signed12',
                'output_bits':'signed13_non_sign_0..11','baseline':'original_application_default'}.items()):
            raise ValueError('Evaluator did not confirm the original baseline/width/no-seed protocol')
        for flag,key in NATIVE_FLAGS.items():
            if float(flags[flag])!=float(controls[key]):raise ValueError('Evaluator option mismatch: '+flag)
        baseline=next(r for r in tables['summary_all.csv'] if r['status']=='reference')
        _quality(baseline)
        if row['status']!='no_legal_interval_state':_quality(row)
        original=next(r for r in read_csv(context['source']/'train/training_baselines.csv',['image']) if int(r['image'])==image)
        for region in ('global','roi','non_roi'):
            if abs(float(baseline[region+'_mse'])-float(original[region+'_mse']))>1e-8:
                raise ValueError('Original baseline MSE changed for '+region)
        stat=tables['search_stats_all.csv'][0]
        if int(stat['contexts_built'])>controls['context_budget'] or int(stat.get('full_evaluations',0))>controls['max_full_evals']:
            raise ValueError('Evaluator exceeded the original per-image budget')
        evaluation=dict(application='conv',hardware_id=TARGET,image=image,status=row['status'],
                        **{key:row.get(key,'') for key in METRIC_FIELDS},elapsed_seconds=0,
                        contexts_built=stat['contexts_built'],full_evaluations=stat.get('full_evaluations',0),
                        completed_sweeps=stat.get('completed_sweeps',0),stopped_for_convergence=stat.get('stopped_for_convergence',0),
                        reference_cache_hit=0,baseline_cache_hit=0,phase='supplementary',evidence_origin='supplementary',
                        timing_basis='standalone_cli_including_reference_and_process',native_calibration_seconds=row.get('calibration_seconds',''))
        if row['status']=='no_legal_interval_state':
            for key in METRIC_FIELDS:evaluation[key]=''
        configs=[dict(r,hardware_id=TARGET,image=image,evidence_origin='supplementary') for r in tables['configurations_all.csv']]
        stats=[dict(stat,application='conv',hardware_id=TARGET,image=image)]
        status['status']=row['status']
    except (OSError,ValueError,RuntimeError,KeyError,TypeError,csv.Error) as error:
        status.update(status='run_failed',detail=str(error));evaluation=None;configs=[];stats=[]
    status['elapsed_seconds']=time.monotonic()-begin
    if evaluation is not None:evaluation['elapsed_seconds']=status['elapsed_seconds']
    return status,evaluation,configs,stats


def ranked(report,new):
    evaluations=[dict(row) for row in report['evaluations']]+new
    result=[]
    for old in report['ranking']:
        hid=old['hardware_id'];rows=sorted((r for r in evaluations if r['hardware_id']==hid),key=lambda r:int(r['image']))
        scores=[float(r['roi_drop_score']) if r['status']=='numeric_eligible' else 0 for r in rows]
        row=dict(old,original_rank=old['rank'],original_status=old['status'],complete=int(len(rows)==10),
                 evaluated_images=len(rows),numeric_successes=sum(r['status']=='numeric_eligible' for r in rows),
                 worst_roi_drop_score=min(scores,default=0),mean_roi_drop_score=math.fsum(scores)/10)
        if hid==TARGET:
            row.update(status='complete' if len(rows)==10 else 'supplement_failed',
                       reason='all_training_images_evaluated' if len(rows)==10 else 'supplementary_process_failure')
        legal=[r for r in rows if r['status'] in ('numeric_eligible','quality_not_met')]
        row['metric_images']=len(legal)
        for field in ('global_psnr','roi_psnr','non_roi_psnr'):
            values=[float(r[field]) for r in legal]
            row['mean_'+field]='' if not values else 'inf' if any(math.isinf(v) for v in values) else math.fsum(values)/len(values)
        result.append(row)
    result.sort(key=lambda r:(-int(r['complete']),-int(r['numeric_successes']),-float(r['worst_roi_drop_score']),
                              -float(r['mean_roi_drop_score']),int(r['installed_nodes']),report['hardware'][r['hardware_id']]))
    for index,row in enumerate(result,1):row['rank']=index
    return result


def main(argv=None):
    root=Path(__file__).resolve().parents[1];parser=argparse.ArgumentParser(description=__doc__)
    for name in ('data','source','output'):parser.add_argument(name,type=Path)
    parser.add_argument('--build-dir',type=Path,default=root/'build-bitflip')
    parser.add_argument('--train-exe',type=Path);parser.add_argument('--fixed-exe',type=Path)
    parser.add_argument('--workers',type=int)
    args=parser.parse_args(argv);context=None;meta=None;owned=False;begin=time.monotonic()
    statuses=[];new=[];configs=[];stats=[];started=set();lock=threading.Lock()
    try:
        context=preflight(args);out=context['out'];out.mkdir(parents=True,exist_ok=False);owned=True
        (out/'evaluations').mkdir();(out/'logs').mkdir()
        hardware_path=out/'hardware.csv'
        atomic_csv(hardware_path,[dict(node_id=n,monitor=m,bit=b) for n,m,b in context['hardware']],['node_id','monitor','bit'])
        context['protected'][hardware_path]=digest(hardware_path)
        old=[dict(r,evidence_origin='original',timing_basis='original_native_cached_pair')
             for r in context['report']['evaluations'] if r['hardware_id']==TARGET]
        original_count=int(context['report']['notes']['actual_evaluations'])
        meta=dict(workflow='conv_ten_h2_supplement',completed=False,all_succeeded=False,candidate_complete=False,
                  application='conv',hardware_id=TARGET,source=str(context['source']),test_executed=False,
                  train_images=list(range(1,11)),reused_candidate_evaluations=len(old),missing_images=context['missing'],
                  original_train_evaluations=original_count,supplementary_evaluations=0,combined_train_evaluations=original_count,
                  original_training_budget=context['controls']['max_evaluations'],supplementary_budget=6,
                  calibration_options=context['controls'],warm_seed_policy='none: initial global ten_h2',
                  workers=context['workers'],visual_status='not_evaluated',provenance_verified_after=False,
                  protected_sha256={str(p):value for p,value in context['protected'].items()},failed_images=[])
        def flush():
            with lock:meta['supplementary_evaluations']=len(started)
            meta['combined_train_evaluations']=original_count+meta['supplementary_evaluations']
            meta['completion_wall_seconds']=time.monotonic()-begin
            atomic_json(out/'RUN.json',meta)
            atomic_csv(out/'run_status.csv',sorted(statuses,key=lambda r:int(r['image'])),
                       ['application','hardware_id','image','status','returncode','elapsed_seconds','detail','log'])
            atomic_csv(out/'search_stats_all.csv',stats,['application','hardware_id','image'])
        flush();pool=ThreadPoolExecutor(max_workers=context['workers'])
        futures=[pool.submit(evaluate,context,image,hardware_path,started,lock) for image in context['missing']]
        try:
            for future in as_completed(futures):
                state,evaluation,intervals,counters=future.result();statuses.append(state)
                if evaluation is not None:new.append(evaluation);configs.extend(intervals);stats.extend(counters)
                else:meta['failed_images'].append(state['image'])
                flush()
        except KeyboardInterrupt:
            for future in futures:future.cancel()
            raise
        finally:pool.shutdown(wait=True,cancel_futures=True)
        stable(context['protected']);meta['provenance_verified_after']=True
        merged=sorted(old+new,key=lambda r:int(r['image']))
        atomic_csv(out/'completed_candidate_evaluations.csv',merged,['application','hardware_id','image','status'])
        old_configs=[dict(r,evidence_origin='original') for r in context['report']['configurations'] if r['hardware_id']==TARGET]
        atomic_csv(out/'completed_candidate_configurations.csv',sorted(old_configs+configs,key=lambda r:(int(r['image']),int(r['node_id']))),
                   ['hardware_id','image','node_id','monitor','bit','lower','upper','evidence_origin'])
        ranking=ranked(context['report'],new)
        atomic_csv(out/'ranking_completed.csv',ranking,['hardware_id','rank','status','complete','evaluated_images'])
        original_winner=context['report']['selected']['hardware_id']
        atomic_csv(out/'comparison.csv',[r for r in ranking if r['hardware_id'] in (TARGET,original_winner)],
                   ['hardware_id','rank','original_rank','complete','evaluated_images','numeric_successes','installed_nodes',
                    'mean_global_psnr','mean_roi_psnr','mean_non_roi_psnr','worst_roi_drop_score','mean_roi_drop_score'])
        meta.update(completed=True,all_succeeded=not meta['failed_images'],candidate_complete=len(merged)==10,
                    valid_supplementary_evaluations=len(new),original_selected_hardware_id=original_winner,
                    updated_best_training_hardware_id=ranking[0]['hardware_id'],
                    supplementary_task_seconds=math.fsum(r['elapsed_seconds'] for r in statuses))
        flush()
        print(f"Conv {TARGET}: reused {len(old)}, evaluated {len(new)}/6 missing images; compare comparison.csv. No new-image tests executed.")
        return 0 if meta['all_succeeded'] else 1
    except KeyboardInterrupt:
        if owned and meta is not None:meta['error']='Interrupted by user';flush()
        return 130
    except (OSError,ValueError,RuntimeError,KeyError,TypeError,OverflowError,csv.Error) as error:
        if owned and meta is not None:meta['error']=str(error);flush()
        print('Conv candidate completion failed: '+str(error),file=sys.stderr);return 1


if __name__=='__main__':sys.exit(main())
