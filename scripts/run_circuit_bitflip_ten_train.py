#!/usr/bin/env python3
"""User-run: select one H jointly on images1–10, then calibrate frozen H on new images.

No independent11–20 screening and no author-side execution. Original native
oracles remain authoritative; feature priorities are proposals, not scores.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import shutil
import sys
import time

import run_sharpen_bitflip_hardware as protocol
from run_sharpen_bitflip_batch import atomic_csv,atomic_json,process,read_csv
from bitflip_calibration import run_interval_task
from bitflip_workflow import TaskCache,source_fingerprint
from ten_train_report import validate_training_report

TRAIN_FILES=('training_baselines.csv','candidate_hardware.csv','training_ranking.csv',
             'training_evaluations.csv','training_configurations.csv','search_stats.csv')

def parser(root):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('data',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--application',choices=('sobel','conv','dct'),default='sobel')
    p.add_argument('--train',default='1:10');p.add_argument('--test',default='31:40')
    p.add_argument('--max-train-evaluations',type=int,default=80)
    p.add_argument('--max-candidates',type=int,default=24);p.add_argument('--initial-candidates',type=int,default=3)
    p.add_argument('--proposals-per-round',type=int,default=3);p.add_argument('--max-rounds',type=int,default=6)
    p.add_argument('--patience',type=int,default=2);p.add_argument('--workers',type=int,default=4)
    p.add_argument('--scheduling',choices=('interleaved','sequential'),default='interleaved')
    p.add_argument('--initial-batch',type=int,default=4);p.add_argument('--completion-reserve',type=int,default=20)
    p.add_argument('--train-context-budget',type=int,default=64);p.add_argument('--context-budget',type=int,default=128)
    p.add_argument('--beam',type=int,default=4);p.add_argument('--refinement',type=int,default=1)
    p.add_argument('--restarts',type=int,default=2);p.add_argument('--boundary-budget',type=int,default=24)
    p.add_argument('--max-full-evals',type=int,default=256);p.add_argument('--full-candidates',type=int,default=8)
    p.add_argument('--global-psnr',type=float,default=30);p.add_argument('--convergence-tolerance',type=float,default=.001)
    p.add_argument('--seed-limit',type=int,default=2);p.add_argument('--image-mode',choices=('compact','none','full'),default='compact')
    p.add_argument('--build-dir',type=Path,default=root/'build-bitflip');p.add_argument('--jobs',type=int,default=4)
    p.add_argument('--skip-build',action='store_true');p.add_argument('--train-exe',type=Path);p.add_argument('--fixed-exe',type=Path)
    p.add_argument('--resume-from',type=Path,help='Read-only complete train-task and matching per-test artifact reuse; fresh output required.')
    return p

def overlap(a,b):return a==b or a in b.parents or b in a.parents

def main(argv=None):
    root=Path(__file__).resolve().parents[1];args=parser(root).parse_args(argv)
    owned=False;meta=None;begin=time.monotonic()
    try:
        train=protocol.split_range(args.train);test=protocol.split_range(args.test)
        if train!=list(range(1,11)):raise ValueError('This pilot selects H using exactly images1–10; use --train 1:10')
        if set(train)&set(test):raise ValueError('Test images overlap the ten hardware-selection images')
        if test!=list(range(31,41)):raise ValueError('This pilot tests exactly images31–40; images11–20 are not evaluated')
        if not (len(train)<=args.max_train_evaluations<=100000 and 1<=args.max_candidates<=10000 and
                1<=args.initial_candidates<=args.max_candidates and 1<=args.proposals_per_round<=1000 and
                1<=args.max_rounds<=1000 and 1<=args.patience<=1000 and 1<=args.workers<=64 and
                1<=args.initial_batch<=10 and 0<=args.completion_reserve<=10000000 and
                1<=args.train_context_budget<=1000000 and 1<=args.context_budget<=1000000 and
                1<=args.beam<=64 and 0<=args.refinement<=10 and 1<=args.restarts<=8 and
                (args.boundary_budget==0 or 2<=args.boundary_budget<=8192) and 1<=args.max_full_evals<=10000000 and
                1<=args.full_candidates<=1024 and 1<=args.seed_limit<=10 and 1<=args.jobs<=64 and
                math.isfinite(args.global_psnr) and 0<=args.global_psnr<=1000 and
                math.isfinite(args.convergence_tolerance) and 0<=args.convergence_tolerance<=1):
            raise ValueError('Invalid ten-image search options')
        if (args.train_exe or args.fixed_exe) and not args.skip_build:raise ValueError('External executables require --skip-build')
        data=args.data.resolve();out=args.output.resolve();build=args.build_dir.resolve()
        resume=args.resume_from.resolve() if args.resume_from else None
        for writable in [out]+([] if args.skip_build else [build]):
            if any(part.casefold()=='sources' for part in writable.parts):
                raise ValueError('Synced sources are read-only; choose another output/build directory')
        for reference in [data]+([resume] if resume else []):
            if overlap(out,reference):raise ValueError('Output must not overlap input data or previous reports')
            if not args.skip_build and overlap(build,reference):raise ValueError('Build must not overlap reference data/reports')
        if resume and not resume.is_dir():raise ValueError('Missing read-only resume directory')
        mask=data/'mask/roi_mask.jpg'
        if not mask.is_file():raise ValueError('Missing original ROI mask')
        # Existence only for test images: never hash/read their pixels before H freezes.
        paths={i:protocol.resolve_staged_image_path(data,i) for i in train+test}
        train_exe=args.train_exe.resolve() if args.train_exe else build/'circuit_bitflip_ten_train'
        fixed=args.fixed_exe.resolve() if args.fixed_exe else build/'circuit_bitflip_fixed'
        out.mkdir(parents=True,exist_ok=False);owned=True;(out/'logs').mkdir()
        cache=TaskCache(out,resume);source_hash=source_fingerprint(root);mask_hash=protocol.digest(mask)
        input_hashes={str(i):protocol.digest(paths[i]) for i in train}
        meta=dict(application=args.application,applications=[args.application],completed=False,all_succeeded=False,
                  workflow='ten_train',training_scheduling=args.scheduling,initial_batch=args.initial_batch,
                  completion_reserve=args.completion_reserve,selection_status='not_selected',test_executed=False,
                  splits=dict(train=train,test=test),screening_disabled=True,screen_wall_seconds=0,screen_task_count=0,
                  data=str(data),source_fingerprint=source_hash,training_input_hashes=input_hashes,roi_sha256=mask_hash,
                  options={k:v for k,v in vars(args).items() if isinstance(v,(str,int,float,bool))},
                  hardware_fixed_fields=['node_id','monitor','bit'],adaptive_fields=['lower','upper'],
                  seed_policy='train-only frozen bank; reset independently for every test image',
                  ranking='complete candidates: numeric successes, worst/mean nonnegative ROI drop, fewer nodes, key',
                  visual_status='pending_user_review',ablations='disabled',enable_parameter='absent',failed_tasks=[],
                  training_wall_seconds=0,hardware_selection_wall_seconds=0,test_wall_seconds=0,test_task_seconds=0,
                  cache_hits=0,training_cache_hit=False,seed_bank_unchanged_after_test=False,total_wall_seconds=0)
        statuses=[];best=[];attempts=[];tables={name:[] for name in protocol.TABLES}
        def flush():
            atomic_csv(out/'run_status.csv',statuses,['application','phase','hardware_id','image','status','returncode','elapsed_seconds','log','detail'])
            atomic_csv(out/'best_per_image.csv',best,['application',*protocol.SUMMARY_FIELDS,'roi_drop_score'])
            atomic_csv(out/'interval_attempts.csv',attempts,['application','phase','hardware_id','image','attempt','status','selected'])
            for name,(_,fields) in protocol.TABLES.items():atomic_csv(out/name,tables[name],['application',*fields])
            app=dict(application=args.application,status='completed' if meta['completed'] and meta['all_succeeded'] else 'incomplete',
                     selection_status=meta['selection_status'],selected_hardware_id=meta.get('selected_hardware_id',''),
                     training_wall_seconds=meta['training_wall_seconds'],hardware_selection_wall_seconds=meta['hardware_selection_wall_seconds'],
                     screen_wall_seconds=0,test_wall_seconds=meta['test_wall_seconds'],numeric_test_successes=sum(r['status']=='numeric_eligible' for r in best),
                     test_images=len(test),actual_train_evaluations=meta.get('actual_train_evaluations',''),cache_hits=meta['cache_hits'])
            for field in ('training_scheduling','initial_batch','completion_reserve','effective_completion_reserve','reference_evaluations',
                          'exploration_evaluations','completion_evaluations','completion_candidates'):
                app[field]=meta.get(field,'')
            app['evaluation_image_order']=','.join(map(str,meta.get('evaluation_image_order',[])))
            atomic_csv(out/'applications.csv',[app],list(app))
            meta['total_wall_seconds']=time.monotonic()-begin;atomic_json(out/'RUN.json',meta)
        flush()
        if not args.skip_build:
            for step,command in enumerate((['cmake','-S',root,'-B',build,'-DCMAKE_BUILD_TYPE=Release'],
                    ['cmake','--build',build,'--target','circuit_bitflip_ten_train','circuit_bitflip_fixed',f'-j{args.jobs}'])):
                if process(command,out/'build.log',root,append=bool(step)):raise RuntimeError('Build failed; inspect build.log')
        if not train_exe.is_file() or not fixed.is_file():raise ValueError('Missing ten-image or fixed-H executable')
        controls=dict(train=args.train,max_evaluations=args.max_train_evaluations,max_candidates=args.max_candidates,
                      initial_candidates=args.initial_candidates,proposals_per_round=args.proposals_per_round,max_rounds=args.max_rounds,
                      scheduling=args.scheduling,initial_batch=args.initial_batch,completion_reserve=args.completion_reserve,
                      patience=args.patience,workers=args.workers,context_budget=args.train_context_budget,beam=args.beam,
                      refinement=args.refinement,restarts=args.restarts,boundary_budget=args.boundary_budget,
                      max_full_evals=args.max_full_evals,full_candidates=args.full_candidates,global_psnr=args.global_psnr,
                      convergence_tolerance=args.convergence_tolerance,seed_limit=args.seed_limit)
        signature=dict(schema=2,kind='ten_image_training',application=args.application,inputs=input_hashes,
                       roi=mask_hash,source=source_hash,executable=protocol.digest(train_exe),controls=controls)
        status=dict(application=args.application,phase='train_joint',hardware_id='',image='',status='running',returncode='',
                    elapsed_seconds='',log='logs/train.log',detail='',cache_hit=0)
        statuses.append(status);started=time.monotonic()
        try:
            reused=cache.restore('train',signature);status['cache_hit']=int(reused)
            command=[train_exe,args.application,data,out/'train']
            for name,value in controls.items():command.extend(['--'+name.replace('_','-'),value])
            status['returncode']=0 if reused else process(command,out/'logs/train.log',root)
            if status['returncode']:raise RuntimeError('Joint ten-image training executable failed')
            report=validate_training_report(out/'train',args.application,train,args.global_psnr,args.max_train_evaluations,args.max_candidates)
            if int(report['notes'].get('reference_builds','-1'))!=len(train):raise ValueError('Native prepared references do not match the ten train images')
            notes=report['notes']
            if (notes.get('scheduling')!=args.scheduling or notes.get('initial_batch')!=str(args.initial_batch) or
                    notes.get('completion_reserve')!=str(args.completion_reserve)):
                raise ValueError('Training executable/report does not match the requested scheduling; rebuild the executable')
            if not reused:cache.record('train',signature)
            status.update(status='completed',log='' if reused else status['log'])
            meta.update(training_cache_hit=reused,cache_hits=int(reused),actual_train_evaluations=int(report['notes']['actual_evaluations']),
                        native_preparation_seconds=report['notes'].get('preparation_seconds'),native_search_seconds=report['notes'].get('search_seconds'),
                        search_stop_reason=report['notes'].get('stop_reason'),reference_builds=len(train))
            for field in ('effective_completion_reserve','reference_evaluations','exploration_evaluations',
                          'completion_evaluations','completion_candidates'):
                meta[field]=int(notes[field])
            meta['evaluation_image_order']=[int(value) for value in notes['evaluation_image_order'].split(',')]
        except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
            status.update(status='failed',detail=str(exc));meta['failed_tasks'].append(dict(phase='train_joint'))
            raise
        finally:
            status['elapsed_seconds']=time.monotonic()-started;meta['training_wall_seconds']=status['elapsed_seconds']
            meta['hardware_selection_wall_seconds']=status['elapsed_seconds'];flush()
        for name in TRAIN_FILES:shutil.copyfile(out/'train'/name,out/name)
        if any(protocol.digest(paths[i])!=input_hashes[str(i)] for i in train) or protocol.digest(mask)!=mask_hash:
            raise RuntimeError('Training input or original ROI changed during hardware selection')
        selected=report['selected']
        if selected is None:
            meta.update(completed=True,all_succeeded=True,selection_status='no_numeric_eligible_hardware');flush()
            print('No complete numeric-eligible training hardware; final test not started');return 0
        hardware=selected['key'];hid=selected['hardware_id']
        for name in ('frozen_hardware.csv','frozen_seeds.csv'):shutil.copyfile(out/'train'/name,out/name)
        frozen={name:protocol.digest(out/name) for name in ('frozen_hardware.csv','frozen_seeds.csv')}
        meta.update(selection_status='selected',selected_hardware_id=hid,frozen_hashes=frozen,test_executed=True)
        atomic_json(out/'frozen_protocol.json',dict(schema='ten_train_v1',application=args.application,hardware_id=hid,
                    hardware_key=hardware,splits=meta['splits'],options=meta['options'],frozen_hashes=frozen,
                    training_input_hashes=input_hashes,roi_sha256=mask_hash,screening_disabled=True))
        flush()  # H and bank freeze before any test pixels are read/hashed.
        fixed_args=argparse.Namespace(**{**vars(args),'workflow':'continuation','adaptive_intervals':'auto',
                                        'adaptive_min_gain':args.convergence_tolerance})
        test_started=time.monotonic()
        try:
            for image in test:
                if any(protocol.digest(out/name)!=value for name,value in frozen.items()):raise RuntimeError('Frozen hardware/seed bank changed before test')
                result=run_interval_task(root=root,out=out,data=data,fixed=fixed,application=args.application,
                    h=dict(hardware_id=hid,key=hardware),image=image,mode=args.image_mode,
                    hardware_path=out/'frozen_hardware.csv',seeds_path=out/'frozen_seeds.csv',args=fixed_args,phase='test',
                    cache=cache,source_hash=source_hash,image_path=paths[image],mask_hash=mask_hash,report_reader=protocol.fixed_report)
                state,row,staged,efforts=result;statuses.append(dict(application=args.application,**state))
                row.update(application=args.application);best.append(row)
                attempts.extend(dict(application=args.application,**e) for e in efforts)
                meta['cache_hits']+=int(state.get('cache_hit',0));meta['test_task_seconds']+=state['elapsed_seconds']
                if state['status']=='failed':meta['failed_tasks'].append(dict(phase='test',image=image))
                else:
                    for name,rs in staged.items():tables[name].extend({**r,'application':args.application,'phase':'test','hardware_id':hid,'image':image} for r in rs)
                meta['test_wall_seconds']=time.monotonic()-test_started;flush()
        finally:meta['test_wall_seconds']=time.monotonic()-test_started
        if any(protocol.digest(out/name)!=value for name,value in frozen.items()):raise RuntimeError('Frozen bank changed during test')
        meta.update(completed=True,all_succeeded=not meta['failed_tasks'],seed_bank_unchanged_after_test=True);flush()
        print(f"Selected {hid} from images1–10; {sum(r['status']=='numeric_eligible' for r in best)}/{len(test)} test images numeric eligible. Visual review pending.")
        return 0 if meta['all_succeeded'] else 1
    except KeyboardInterrupt:
        if owned and meta is not None:meta['error']='Interrupted by user';atomic_json(out/'RUN.json',meta)
        print('Interrupted; complete artifacts preserved',file=sys.stderr);return 130
    except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
        if owned and meta is not None:meta['error']=str(exc);atomic_json(out/'RUN.json',meta)
        print(f'Ten-image hardware selection failed: {exc}',file=sys.stderr);return 1

if __name__=='__main__':sys.exit(main())
