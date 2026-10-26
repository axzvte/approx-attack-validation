#!/usr/bin/env python3
"""User-run shared ten-image workflow for Sharpen, Conv, DCT (optionally Sobel).

Build once; each application selects its own fixed H on images1–10 under the
same controls, then calibrates intervals independently on images31–40.
"""
import csv
import json
import math
from pathlib import Path,PurePosixPath,PureWindowsPath
import sys
import time

import run_circuit_bitflip_ten_train as single
from run_sharpen_bitflip_batch import atomic_csv,atomic_json,process,read_csv

APPLICATIONS=('sharpen','conv','sobel','dct')
DEFAULT_APPLICATIONS=('sharpen','conv','dct')
MERGED={
    'best_per_image.csv':'best_per_image.csv',
    'training_ranking_all.csv':'training_ranking.csv',
    'training_evaluations_all.csv':'training_evaluations.csv',
    'training_configurations_all.csv':'training_configurations.csv',
    'candidate_hardware_all.csv':'candidate_hardware.csv',
    'training_baselines_all.csv':'training_baselines.csv',
    'training_search_stats_all.csv':'search_stats.csv',
    'summary_all.csv':'summary_all.csv',
    'configurations_all.csv':'configurations_all.csv',
    'search_stats_all.csv':'search_stats_all.csv',
    'images_all.csv':'images_all.csv',
    'interval_attempts_all.csv':'interval_attempts.csv',
    'run_status.csv':'run_status.csv',
}
CONTROLS=('train','test','max_train_evaluations','max_candidates','initial_candidates',
          'proposals_per_round','max_rounds','patience','workers','scheduling','initial_batch',
          'completion_reserve','train_context_budget','context_budget','beam','refinement',
          'restarts','boundary_budget','max_full_evals','full_candidates','global_psnr',
          'convergence_tolerance','seed_limit','image_mode','jobs')
APP_FIELDS=('application','status','returncode','detail','selection_status','selected_hardware_id',
            'training_wall_seconds','hardware_selection_wall_seconds','test_wall_seconds','elapsed_seconds',
            'actual_train_evaluations','training_scheduling','initial_batch','completion_reserve',
            'effective_completion_reserve','reference_evaluations','exploration_evaluations',
            'completion_evaluations','completion_candidates','evaluation_image_order',
            'complete_candidates','bound_eliminated_candidates','budget_incomplete_candidates',
            'installed_nodes','numeric_test_successes','test_images','metric_images',
            'mean_global_psnr','mean_roi_psnr','mean_non_roi_psnr','cache_hits')


def child_arguments(args,application,output,train,fixed):
    values=[str(args.data.resolve()),str(output),'--application',application,'--skip-build',
            '--train-exe',str(train),'--fixed-exe',str(fixed),'--build-dir',str(args.build_dir.resolve())]
    for name in CONTROLS:values.extend(['--'+name.replace('_','-'),str(getattr(args,name))])
    if args.resume_from:values.extend(['--resume-from',str(args.resume_from.resolve()/application)])
    return values


def prefixed_path(application,value):
    if not value:return ''
    posix=PurePosixPath(value);windows=PureWindowsPath(value)
    if posix.is_absolute() or windows.drive or windows.root or '..' in posix.parts or '..' in windows.parts:
        raise ValueError('Child report contains a path outside its application directory')
    return (PurePosixPath(application)/PurePosixPath(value.replace('\\','/'))).as_posix()


def merge_child(folder,application,tables):
    for target,source in MERGED.items():
        path=folder/source
        if not path.is_file():continue  # Failed children may have only run_status.
        for original in read_csv(path,[]):
            if original.get('application',application)!=application:
                raise ValueError('Child table application identity mismatch')
            row=dict(original,application=application)
            fields=(('output_path','roi_comparison_path','background_comparison_path')
                    if target=='best_per_image.csv' else ('path',) if target=='images_all.csv'
                    else ('log',) if target=='run_status.csv' else ())
            for field in fields:
                if field in row:row[field]=prefixed_path(application,row[field])
            tables[target].append(row)


def metrics(rows):
    legal=[row for row in rows if row['status'] in ('numeric_eligible','quality_not_met')]
    result=dict(metric_images=len(legal),numeric_test_successes=sum(row['status']=='numeric_eligible' for row in rows))
    for field in ('global_psnr','roi_psnr','non_roi_psnr'):
        values=[float(row[field]) for row in legal]
        if any(math.isnan(value) or value==-math.inf for value in values):
            raise ValueError('Invalid test PSNR in application summary')
        result['mean_'+field]=('' if not values else 'inf' if any(value==math.inf for value in values)
                              else math.fsum(values)/len(values))
    return result


def available_summary(folder,application,splits,*,require_summary=False):
    """Collect measured/observed results even from an interrupted child."""
    path=folder/'RUN.json'
    if not path.is_file():
        if require_summary:raise ValueError('Missing final child run report')
        return {}
    final=json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(final,dict) or final.get('application')!=application or final.get('splits')!=splits:
        raise ValueError('Final child report identity mismatch')
    result={};summary_path=folder/'applications.csv'
    if summary_path.is_file():
        summaries=read_csv(summary_path,['application','hardware_selection_wall_seconds','test_wall_seconds'])
        if len(summaries)!=1 or summaries[0]['application']!=application:raise ValueError('Missing application summary')
        result.update({key:value for key,value in summaries[0].items()
                       if key not in ('application','status','returncode','detail','elapsed_seconds')})
    elif require_summary:raise ValueError('Missing application summary')
    for field in ('selection_status','selected_hardware_id','actual_train_evaluations','training_scheduling',
                  'initial_batch','completion_reserve','effective_completion_reserve','reference_evaluations',
                  'exploration_evaluations','completion_evaluations','completion_candidates','cache_hits'):
        if field in final:result[field]=final[field]
    if 'evaluation_image_order' in final:
        result['evaluation_image_order']=','.join(map(str,final['evaluation_image_order']))
    for field in ('training_wall_seconds','hardware_selection_wall_seconds','test_wall_seconds'):
        if field not in final:continue
        value=float(final[field])
        if not math.isfinite(value) or value<0:raise ValueError('Invalid measured application phase time')
        result[field]=value
    ranking_path=folder/'training_ranking.csv'
    if ranking_path.is_file():
        ranking=read_csv(ranking_path,['status'])
        for state in ('complete','bound_eliminated','budget_incomplete'):
            result[state+'_candidates']=sum(row['status']==state for row in ranking)
    best_path=folder/'best_per_image.csv'
    if best_path.is_file():result.update(metrics(read_csv(best_path,['status'])))
    hardware_path=folder/'frozen_hardware.csv'
    if final.get('selection_status')=='selected' and hardware_path.is_file():
        result['installed_nodes']=len(read_csv(hardware_path,['node_id','monitor','bit']))
    return result


def main(argv=None):
    root=Path(__file__).resolve().parents[1]
    parser=single.parser(root,include_application=False);parser.description=__doc__
    parser.add_argument('--applications',default=','.join(DEFAULT_APPLICATIONS))
    args=parser.parse_args(argv);owned=False;meta=None;begin=time.monotonic()
    statuses=[];tables={name:[] for name in MERGED}
    try:
        applications=[name.strip().lower() for name in args.applications.split(',')]
        if not applications or len(set(applications))!=len(applications) or any(name not in APPLICATIONS for name in applications):
            raise ValueError('Choose distinct sharpen,conv,sobel,dct applications')
        train_images,test_images=single.validate_search_options(args)
        data=args.data.resolve();out=args.output.resolve();build=args.build_dir.resolve()
        resume=args.resume_from.resolve() if args.resume_from else None
        for writable in [out]+([] if args.skip_build else [build]):
            if any(part.casefold()=='sources' for part in writable.parts):
                raise ValueError('Synced sources are read-only; choose another output/build directory')
        for reference in [data]+([resume] if resume else []):
            if single.overlap(out,reference):raise ValueError('Output must not overlap reference data/reports')
            if not args.skip_build and single.overlap(build,reference):raise ValueError('Build must not overlap reference data/reports')
        if not args.skip_build and single.overlap(out,build):raise ValueError('Build and output directories must not overlap')
        if resume and (not resume.is_dir() or any(not (resume/app).is_dir() for app in applications)):
            raise ValueError('Resume directory must contain each selected application')
        if not (data/'mask/roi_mask.jpg').is_file():raise ValueError('Missing original ROI mask')
        # Test filenames only; each child freezes H before hashing test pixels.
        for image in train_images+test_images:single.protocol.resolve_staged_image_path(data,image)
        train=args.train_exe.resolve() if args.train_exe else build/'circuit_bitflip_ten_train'
        fixed=args.fixed_exe.resolve() if args.fixed_exe else build/'circuit_bitflip_fixed'
        if args.skip_build and (not train.is_file() or not fixed.is_file()):raise ValueError('Missing shared ten-image/fixed-H executable')
        out.mkdir(parents=True,exist_ok=False);owned=True
        meta=dict(completed=False,all_succeeded=False,workflow='ten_train_batch',applications=applications,
                  splits=dict(train=train_images,test=test_images),data=str(data),
                  options={key:value for key,value in vars(args).items() if isinstance(value,(str,int,float,bool))},
                  hardware_fixed_fields=['node_id','monitor','bit'],adaptive_fields=['lower','upper'],
                  per_application_train_evaluation_cap=args.max_train_evaluations,
                  application_execution='sequential; shared controls; independent fixed hardware per application',
                  screening_disabled=True,ablations='disabled',visual_status='pending_user_review',
                  failed_applications=[],total_wall_seconds=0)

        def flush():
            meta['total_wall_seconds']=time.monotonic()-begin
            atomic_json(out/'RUN.json',meta);atomic_csv(out/'applications.csv',statuses,list(APP_FIELDS))
            for name,rows in tables.items():atomic_csv(out/name,rows,['application'])

        flush()
        if not args.skip_build:
            for index,command in enumerate((['cmake','-S',root,'-B',build,'-DCMAKE_BUILD_TYPE=Release'],
                    ['cmake','--build',build,'--target','circuit_bitflip_ten_train','circuit_bitflip_fixed',f'-j{args.jobs}'])):
                if process(command,out/'build.log',root,append=bool(index)):raise RuntimeError('Build failed; inspect build.log')
        if not train.is_file() or not fixed.is_file():raise ValueError('Missing shared ten-image/fixed-H executable')
        for application in applications:
            folder=out/application;started=time.monotonic()
            status=dict(application=application,status='running',returncode='',detail='',test_images=len(test_images))
            statuses.append(status);flush()
            try:
                code=single.main(child_arguments(args,application,folder,train,fixed));status['returncode']=code
                if code==130:raise KeyboardInterrupt
                child=json.loads((folder/'RUN.json').read_text(encoding='utf-8-sig'))
                if child.get('application')!=application or child.get('splits')!=meta['splits']:
                    raise ValueError('Child run application/split identity mismatch')
                if code or not child.get('completed') or not child.get('all_succeeded'):
                    raise RuntimeError(child.get('error') or 'Application did not complete successfully')
                status['status']='completed'
            except KeyboardInterrupt:
                status.update(status='interrupted',detail='Interrupted by user');raise
            except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as error:
                status.update(status='failed',detail=str(error))
            finally:
                # A return130 still has observed results worth aggregating;
                # preserve interrupted status while recovering their metrics.
                try:
                    status.update(available_summary(folder,application,meta['splits'],require_summary=status['status']=='completed'))
                except (OSError,ValueError,KeyError,TypeError,OverflowError,csv.Error) as error:
                    if status['status']=='interrupted':status['detail']='Interrupted by user; partial report error: '+str(error)
                    else:status.update(status='failed',detail=str(error))
                status['elapsed_seconds']=time.monotonic()-started
                try:merge_child(folder,application,tables)
                except (OSError,ValueError,KeyError,csv.Error) as error:
                    if status['status']!='interrupted':status.update(status='failed',detail=str(error))
                if status['status']=='failed' and application not in meta['failed_applications']:
                    meta['failed_applications'].append(application)
                if status['status']=='failed':
                    print(f"{application} failed: {status['detail']}",file=sys.stderr)
                flush()
        meta.update(completed=True,all_succeeded=not meta['failed_applications']);flush()
        print(f"Completed {len(applications)} applications; compare applications.csv times and PSNR values. Visual review pending.")
        return 0 if meta['all_succeeded'] else 1
    except KeyboardInterrupt:
        if owned and meta is not None:
            meta['error']='Interrupted by user';meta['total_wall_seconds']=time.monotonic()-begin
            atomic_json(out/'RUN.json',meta)
        return 130
    except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as error:
        if owned and meta is not None:
            meta['error']=str(error);meta['total_wall_seconds']=time.monotonic()-begin
            atomic_json(out/'RUN.json',meta)
        print(f'Ten-image application batch failed: {error}',file=sys.stderr);return 1


if __name__=='__main__':sys.exit(main())
