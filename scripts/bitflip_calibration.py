"""Bounded per-image effort and validated, read-only task reuse.

This module orchestrates existing native executables; it never estimates final
image quality from independent node effects. The author must not execute it.
"""
import csv
import hashlib
import math
from pathlib import Path
import shutil
import time

from run_sharpen_bitflip_batch import process,atomic_csv

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def effort_levels(args,node_count,application):
    """All circuits use the same fractions; counts only set coverage minima."""
    adaptive=args.adaptive_intervals=='on' or (args.adaptive_intervals=='auto' and args.workflow=='balanced')
    levels=[]
    for fraction in ((.25,.5,1.) if adaptive else (1.,)):
        level=dict(context_budget=min(args.context_budget,max(node_count,math.ceil(args.context_budget*fraction))),
                   beam=args.beam,refinement=args.refinement if fraction==1 else 0,
                   restarts=args.restarts if fraction==1 else 1,boundary_budget=args.boundary_budget)
        if application!='sharpen':
            level.update(max_full_evals=min(args.max_full_evals,max(node_count,math.ceil(args.max_full_evals*fraction))),
                         full_candidates=args.full_candidates)
        if level not in levels:levels.append(level)
    return levels

def result_key(row):
    status=row['status']
    if status=='numeric_eligible':return (2,float(row['roi_drop_score']),-float(row['global_mse']))
    if status=='quality_not_met':return (1,-float(row['global_mse']),float(row['roi_mse']))
    return (0,0.,0.)

def run_interval_task(*,root,out,data,fixed,application,h,image,mode,hardware_path,seeds_path,
                      args,phase,cache,source_hash,image_path,mask_hash,report_reader):
    """Return (status,row,staged,attempt_diagnostics); caller alone writes globals."""
    relative=f"{phase}/{h['hardware_id']}/image_{image:02d}"
    report=out/relative;report.parent.mkdir(parents=True,exist_ok=True)
    log=f"logs/{phase}_{h['hardware_id']}_image_{image:02d}.log"
    status=dict(phase=phase,hardware_id=h['hardware_id'],image=image,status='running',returncode='',
                elapsed_seconds='',log=log,detail='',cache_hit=0)
    begin=time.monotonic();staged={};attempt_rows=[]
    native_nodes={'sharpen':4,'conv':8,'sobel':11,'dct':32}[application]
    levels=effort_levels(args,len(h['key']),application)
    signature=dict(schema=1,kind='fixed_intervals',application=application,phase=phase,image=image,
                   input_sha256=digest(image_path),mask_sha256=mask_hash,source_sha256=source_hash,
                   executable_sha256=digest(fixed),hardware_sha256=digest(hardware_path),seeds_sha256=digest(seeds_path),
                   global_psnr=args.global_psnr,image_mode=mode,effort_levels=levels,
                   adaptive_min_gain=args.adaptive_min_gain)
    def read(path):
        return report_reader(path,image,h['key'],mode,relative,args.global_psnr,native_nodes,
                             application if application!='sharpen' else None)
    try:
        if cache.restore(relative,signature):
            staged,row=read(report);status.update(status='completed',returncode=0,cache_hit=1,
                                                 search_status=row['search_status'],log='',detail='Reused hash-matched task artifacts')
            attempts_file=report/'effort.csv'
            if attempts_file.is_file():
                with attempts_file.open(newline='',encoding='utf-8-sig') as stream:
                    attempt_rows=list(csv.DictReader(stream))
            for item in attempt_rows:item.update(phase=phase,hardware_id=h['hardware_id'],image=image,cache_hit=1)
        else:
            attempt_root=report.parent/(report.name+'_work')
            attempt_root.mkdir(exist_ok=False)
            best=None;best_path=None;previous_best=None
            for index,level in enumerate(levels):
                target=attempt_root/f'attempt_{index+1}'
                command=[fixed,*([application] if application!='sharpen' else []),data,target,
                         '--hardware',hardware_path,'--seeds',seeds_path,'--image',image,
                         '--beam',level['beam'],'--refinement',level['refinement'],'--restarts',level['restarts'],
                         '--boundary-budget',level['boundary_budget'],'--context-budget',level['context_budget'],
                         '--global-psnr',args.global_psnr,'--image-mode',mode]
                if application!='sharpen':
                    command.extend(['--max-full-evals',level['max_full_evals'],'--full-candidates',level['full_candidates']])
                started=time.monotonic()
                effort=dict(phase=phase,hardware_id=h['hardware_id'],image=image,attempt=index+1,**level,
                            status='running',returncode='',roi_drop_score='',global_mse='',elapsed_seconds='',
                            contexts_built='',full_evaluations='',selected=0,cache_hit=0,detail='')
                attempt_rows.append(effort)
                try:
                    code=process(command,out/log,root,append=index>0);status['returncode']=code;effort['returncode']=code
                    if code:raise RuntimeError(f'Interval executable returned {code} at effort {index+1}')
                    attempt_tables,candidate=read(target)
                    native_stats=attempt_tables['search_stats_all.csv'][0]
                    effort.update(status=candidate['status'],roi_drop_score=candidate['roi_drop_score'],
                                  global_mse=candidate.get('global_mse',''),contexts_built=native_stats.get('contexts_built',''),
                                  full_evaluations=native_stats.get('full_evaluations',''))
                except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
                    effort.update(status='run_failed',detail=str(exc));raise
                finally:
                    effort['elapsed_seconds']=time.monotonic()-started
                    atomic_csv(attempt_root/'effort.csv',attempt_rows,['attempt','status','returncode','elapsed_seconds','selected'])
                if best is None or result_key(candidate)>result_key(best):
                    best=candidate;best_path=target;best_index=index
                # A successful probe alone never stops the search. Require two
                # budgets and measured stagnation; failures get the full ceiling.
                current=float(best['roi_drop_score']) if best['status']=='numeric_eligible' else None
                if index>=1 and current is not None and previous_best is not None:
                    gain=current-previous_best
                    if gain<=args.adaptive_min_gain*max(1.,abs(previous_best)):break
                previous_best=current
            if best_path is None:raise RuntimeError('No interval effort executed')
            shutil.copytree(best_path,report)
            attempt_rows[best_index]['selected']=1
            atomic_csv(report/'effort.csv',attempt_rows,['attempt','status','roi_drop_score','selected'])
            staged,row=read(report)
            # This folder was created by this call, below the fresh output root.
            # Only the selected report/images survive; logs retain every attempt.
            shutil.rmtree(attempt_root)
            cache.record(relative,signature)
            status.update(status='completed',search_status=row['search_status'])
        row.update(phase=phase,hardware_id=h['hardware_id'])
    except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
        status.update(status='failed',detail=str(exc))
        row=dict(phase=phase,hardware_id=h['hardware_id'],image=image,status='run_failed',
                 visual_status='not_available',roi_drop_score=0)
    status['attempts']=len(attempt_rows)
    status['selected_attempt']=next((r['attempt'] for r in attempt_rows if str(r['selected'])=='1'),'')
    status['elapsed_seconds']=time.monotonic()-begin
    return status,row,staged,attempt_rows
