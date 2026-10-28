#!/usr/bin/env python3
"""Run Conv/Sobel/DCT using an existing completed per-image ROI experiment.

Usage: python run_u2net_other_apps.py SOURCE_SHARPEN_RUN NEW_OUTPUT BUILD_DIR
Default: detached background worker. --foreground waits; --check writes nothing.
SOURCE_SHARPEN_RUN contains RUN.json, not the enclosing one-shot run directory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import traceback
from run_circuit_bitflip_ten_train import overlap
from run_sharpen_bitflip_hardware import resolve_staged_image_path
from image_roi import resolve_roi_path

APPLICATIONS=['conv','sobel','dct']

def preflight(source,output,build):
    source,output,build=(Path(p).resolve() for p in (source,output,build))
    meta=json.loads((source/'RUN.json').read_text())
    if not meta.get('completed') or not meta.get('all_succeeded') or meta.get('roi_mode')!='per-image':
        raise ValueError('Source must be a completed, successful per-image ROI run')
    data=Path(meta['data']).resolve();roi=Path(meta.get('roi_dir') or data/'roi').resolve()
    if output.exists():raise FileExistsError('Choose a NEW output directory: '+str(output))
    if any(part.casefold()=='sources' for part in output.parts):raise ValueError('Synced sources are read-only')
    if any(overlap(output,p) for p in (source,data,roi,build)):
        raise ValueError('Output must not overlap data, ROI, previous reports or build')
    for name in ('circuit_bitflip_ten_train','circuit_bitflip_fixed'):
        if not (build/name).is_file():raise FileNotFoundError(build/name)
    # Paths only: each child verifies held-out mask pixels after freezing its H.
    for i in [*range(1,11),*range(31,41)]:
        resolve_staged_image_path(data,i);resolve_roi_path(roi,i)
    return dict(source=str(source),source_report_sha256=hashlib.sha256((source/'RUN.json').read_bytes()).hexdigest(),
                data=str(data),roi_dir=str(roi),output=str(output),build=str(build),applications=list(APPLICATIONS),
                roi_source='offline_u2net',u2net_runtime_hardware=False)

def worker(plan):
    out=Path(plan['output']);root=Path(__file__).resolve().parents[1]
    status=out/'STATUS.txt'
    try:
        status.write_text('RUNNING_CONV_SOBEL_DCT\n')
        command=[sys.executable,str(root/'scripts/run_circuit_bitflip_ten_train_batch.py'),
                 plan['data'],str(out/'results'),'--applications',','.join(APPLICATIONS),
                 '--roi-dir',plan['roi_dir'],'--skip-build','--build-dir',plan['build'],
                 '--train-exe',str(Path(plan['build'])/'circuit_bitflip_ten_train'),
                 '--fixed-exe',str(Path(plan['build'])/'circuit_bitflip_fixed'),'--image-mode','full']
        code=subprocess.run(command,check=False).returncode
        if code:raise RuntimeError(f'Batch returned {code}; inspect results/applications.csv and child RUN.json')
        meta=json.loads((out/'results/RUN.json').read_text())
        if not meta.get('completed') or not meta.get('all_succeeded') or meta.get('applications')!=APPLICATIONS:
            raise RuntimeError('Batch report does not confirm completion of all requested applications')
        summaries=[]
        for app in APPLICATIONS:
            child=json.loads((out/'results'/app/'RUN.json').read_text())
            if (child.get('roi_mode')!='per-image' or not child.get('all_succeeded') or
                    child.get('selection_status') not in ('selected','no_numeric_eligible_hardware')):
                raise RuntimeError('Invalid final protocol state: '+app)
            if child['selection_status']=='selected' and not (child.get('test_executed') and child.get('seed_bank_unchanged_after_test')):
                raise RuntimeError('Frozen hardware/seed protocol not confirmed: '+app)
            summaries.append(dict(application=app,selection_status=child['selection_status'],
                                  hardware=child.get('selected_hardware_id'),
                                  training_seconds=child.get('training_wall_seconds'),test_seconds=child.get('test_wall_seconds')))
        (out/'SUMMARY.json').write_text(json.dumps(summaries,indent=2)+'\n')
        status.write_text('FINISHED_VISUAL_REVIEW_PENDING\n')
        return 0
    except Exception:
        status.write_text('FAILED_INSPECT_RUN_LOG_AND_REPORTS\n');traceback.print_exc();return 1

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path);parser.add_argument('build',type=Path)
    mode=parser.add_mutually_exclusive_group();mode.add_argument('--check',action='store_true');mode.add_argument('--foreground',action='store_true')
    parser.add_argument('--worker',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args(argv)
    if args.worker:
        plan=json.loads((args.output/'LAUNCH.json').read_text());return worker(plan)
    try:
        plan=preflight(args.source,args.output,args.build)
        if args.check:print(json.dumps(plan,indent=2));return 0
        out=Path(plan['output']);out.mkdir(parents=True,exist_ok=False)
        (out/'LAUNCH.json').write_text(json.dumps(plan,indent=2)+'\n');(out/'STATUS.txt').write_text('STARTING\n')
        with (out/'run.log').open('x') as log:
            command=[sys.executable,str(Path(__file__).resolve()),plan['source'],str(out),plan['build'],'--worker']
            if args.foreground:
                return subprocess.run(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT).returncode
            child=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            (out/'PID.txt').write_text(str(child.pid)+'\n')
        print(f'Background PID={child.pid}\nOutput: {out}\nLog: {out}/run.log\nStatus: {out}/STATUS.txt')
        return 0
    except (OSError,ValueError,KeyError) as exc:
        print(f'Cannot launch: {exc}',file=sys.stderr);return 1

if __name__=='__main__':raise SystemExit(main())
