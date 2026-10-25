#!/usr/bin/env python3
"""User-run only: reuse frozen Sharpen hardware and repair/repeat held-out tests."""
import argparse
import csv
import json
import math
import re
from pathlib import Path
import shutil
import subprocess
import sys
import time

from run_sharpen_bitflip_hardware import (TABLES,SUMMARY_FIELDS,digest,hardware_key,interval_rows,
    fixed_report,resolve_staged_image_path,split_range)
from run_sharpen_bitflip_batch import atomic_csv,atomic_json,process,read_csv

def frozen_source(source):
    run=json.loads((source/"RUN.json").read_text(encoding="utf-8-sig"))
    protocol=json.loads((source/"frozen_protocol.json").read_text(encoding="utf-8-sig"))
    if not isinstance(run,dict) or not isinstance(protocol,dict):raise ValueError("Source metadata must be JSON objects")
    if run.get("completed") is not True or run.get("selection_status")!="selected":
        raise ValueError("Source must have a completed hardware-selection protocol")
    identity=run["selected_hardware_id"]
    if not isinstance(identity,str) or not identity.startswith("h") or not identity[1:].isdigit() or identity!=protocol["hardware_id"]:
        raise ValueError("Invalid or inconsistent selected hardware identity")
    hashes=run["frozen_hashes"]
    if not isinstance(hashes,dict) or set(hashes)!={"frozen_hardware.csv","frozen_seeds.csv"} or hashes!=protocol["frozen_hashes"]:
        raise ValueError("Missing or inconsistent frozen hashes")
    for name,value in hashes.items():
        if digest(source/name)!=value:raise ValueError(f"Source frozen file changed: {name}")
    bank=protocol["training_bank_hashes"]
    if not isinstance(bank,dict) or bank.get(f"hardware/{identity}/hardware.csv")!=hashes["frozen_hardware.csv"] or bank.get(f"hardware/{identity}/seeds.csv")!=hashes["frozen_seeds.csv"]:
        raise ValueError("Frozen files are not the selected original training bank")
    hardware=hardware_key(read_csv(source/"frozen_hardware.csv",["node_id","monitor","bit"]))
    if hardware!=tuple(tuple(x) for x in protocol["hardware_key"]):raise ValueError("Frozen hardware disagrees with protocol")
    groups={}
    for row in read_csv(source/"frozen_seeds.csv",["seed_id","node_id","monitor","bit","lower","upper"]):
        if not row["seed_id"]:raise ValueError("Empty frozen seed identity")
        groups.setdefault(row["seed_id"],[]).append(row)
    if not groups:raise ValueError("Missing frozen training seed bank")
    for rows in groups.values():
        if interval_rows(rows)[0]!=hardware:raise ValueError("Frozen seed changes installed hardware")
    options=protocol["options"]
    if not isinstance(options,dict) or not isinstance(protocol.get("splits"),dict) or not isinstance(run.get("splits"),dict):
        raise ValueError("Missing source search options or split metadata")
    for name,lo,hi in (("beam",1,64),("refinement",0,10),("restarts",1,8),("boundary_budget",0,8192),("context_budget",1,1000000)):
        value=options[name]
        if type(value) is not int or not lo<=value<=hi or (name=="boundary_budget" and value==1):
            raise ValueError(f"Invalid frozen search option: {name}")
    if type(options["global_psnr"]) not in (int,float):raise ValueError("Invalid frozen global PSNR floor")
    floor=float(options["global_psnr"])
    if not math.isfinite(floor) or not 0<=floor<=1000:raise ValueError("Invalid frozen global PSNR floor")
    for name in ("train","screen"):
        values=protocol["splits"][name]
        if not values or any(type(x) is not int or not 1<=x<=40 for x in values) or len(set(values))!=len(values) or values!=run["splits"][name]:
            raise ValueError("Invalid source training/screening split")
    if set(protocol["splits"]["train"])&set(protocol["splits"]["screen"]):raise ValueError("Source splits overlap")
    for seed in groups:
        match=re.fullmatch(r"train_([0-9]{2})_(.+)",seed)
        if not match or int(match.group(1)) not in protocol["splits"]["train"]:
            raise ValueError("Frozen seed identity does not belong to source training images")
    return run,protocol,hardware

def main(argv=None):
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data",type=Path);parser.add_argument("source_report",type=Path);parser.add_argument("output",type=Path)
    parser.add_argument("--test",default="31:40");parser.add_argument("--image-mode",choices=("compact","none","full"),default="compact")
    parser.add_argument("--build-dir",type=Path,default=root/"build-bitflip");parser.add_argument("--jobs",type=int,default=4)
    parser.add_argument("--skip-build",action="store_true");parser.add_argument("--fixed-exe",type=Path)
    args=parser.parse_args(argv);owned=False;meta=None
    try:
        data=args.data.resolve();source=args.source_report.resolve();out=args.output.resolve();build=args.build_dir.resolve()
        if args.fixed_exe and not args.skip_build:raise ValueError("External executable requires --skip-build")
        if not 1<=args.jobs<=64:raise ValueError("Invalid compilation jobs")
        if out==source or source in out.parents or out in source.parents:raise ValueError("New output must not overlap the source report")
        if out==data or data in out.parents or out in data.parents:raise ValueError("Output must not overlap reference data")
        if not args.skip_build:
            for reference in (source,data):
                if build==reference or reference in build.parents or build in reference.parents:
                    raise ValueError("Build directory must not overlap the source report or reference data")
        origin,protocol,hardware=frozen_source(source);images=split_range(args.test)
        if set(images)&(set(protocol["splits"]["train"])|set(protocol["splits"]["screen"])):
            raise ValueError("Retest images overlap hardware training/screening images")
        if Path(origin["data"]).resolve()!=data:raise ValueError("Use the same data root and original ROI as the frozen source")
        if not (data/"mask/roi_mask.jpg").is_file():raise ValueError("Missing original ROI mask")
        paths={str(i):str(resolve_staged_image_path(data,i)) for i in images}  # Existence only, no pixels read.
        fixed=args.fixed_exe.resolve() if args.fixed_exe else build/"sharpen_bitflip_fixed"
        out.mkdir(parents=True,exist_ok=False);owned=True;(out/"logs").mkdir()
        identity=origin["selected_hardware_id"];options=protocol["options"];frozen=origin["frozen_hashes"]
        meta=dict(completed=False,all_succeeded=False,selection_status="selected",selected_hardware_id=identity,
                  screening_reused=True,source_frozen_report=str(source),source_code_revision=origin.get("code_revision"),
                  source_screen_wall_seconds=origin.get("screen_wall_seconds"),screen_wall_seconds=0,screen_task_seconds=0,screen_task_count=0,
                  test_executed=False,test_wall_seconds=0,test_task_seconds=0,failed_tasks=[],data=str(data),resolved_test_inputs=paths,
                  splits={**protocol["splits"],"test":images},options={**options,"test":args.test,"image_mode":args.image_mode},
                  frozen_hashes=frozen,hardware_fixed_fields=["node_id","monitor","bit"],adaptive_fields=["lower","upper"],
                  seed_bank_unchanged_after_test=False,visual_status="pending_user_review",ablations="disabled",enable_parameter="absent")
        statuses=[];best=[];tables={name:[] for name in TABLES}
        def flush():
            atomic_json(out/"RUN.json",meta);atomic_csv(out/"run_status.csv",statuses,["phase","hardware_id","image","status","returncode","elapsed_seconds","log","detail"])
            atomic_csv(out/"best_per_image.csv",best,SUMMARY_FIELDS+["roi_drop_score"])
            for name,(_,fields) in TABLES.items():atomic_csv(out/name,tables[name],fields)
        flush()
        for name in frozen:shutil.copyfile(source/name,out/name)
        # Hash copied files before any held-out image is executed.
        if any(digest(out/name)!=value for name,value in frozen.items()):raise ValueError("Frozen copy hash mismatch")
        if (source/"screening_ranking.csv").is_file():shutil.copyfile(source/"screening_ranking.csv",out/"screening_ranking.csv")
        atomic_json(out/"frozen_protocol.json",{**protocol,"splits":meta["splits"],"source_frozen_report":str(source),"screening_reused":True})
        try:
            rev=subprocess.run(["git","rev-parse","HEAD"],cwd=root,capture_output=True,text=True)
            meta["code_revision"]=rev.stdout.strip() if rev.returncode==0 else "unavailable"
        except OSError:meta["code_revision"]="unavailable"
        if not args.skip_build:
            for i,command in enumerate((["cmake","-S",root,"-B",build,"-DCMAKE_BUILD_TYPE=Release"],
                                         ["cmake","--build",build,"--target","sharpen_bitflip_fixed",f"-j{args.jobs}"])):
                if process(command,out/"build.log",root,append=bool(i)):raise RuntimeError("Build failed; see build.log")
        if not fixed.is_file():raise ValueError("Missing fixed-interval executable")
        meta["test_executed"]=True;begin=time.monotonic();flush()
        for image in images:
            if any(digest(out/name)!=value or digest(source/name)!=value for name,value in frozen.items()):raise ValueError("Frozen source or copied bank changed")
            relative=f"test/{identity}/image_{image:02d}";report=out/relative;report.parent.mkdir(parents=True,exist_ok=True)
            log=f"logs/test_{identity}_image_{image:02d}.log";start=time.monotonic()
            status=dict(phase="test",hardware_id=identity,image=image,status="running",returncode="",elapsed_seconds="",log=log,detail="")
            try:
                command=[fixed,data,report,"--hardware",out/"frozen_hardware.csv","--seeds",out/"frozen_seeds.csv","--image",image,
                         "--beam",options["beam"],"--refinement",options["refinement"],"--restarts",options["restarts"],
                         "--boundary-budget",options["boundary_budget"],"--context-budget",options["context_budget"],"--global-psnr",options["global_psnr"],"--image-mode",args.image_mode]
                status["returncode"]=process(command,out/log,root)
                if status["returncode"]:raise RuntimeError(f"Interval executable returned {status['returncode']}")
                staged,row=fixed_report(report,image,hardware,args.image_mode,relative,float(options["global_psnr"]))
                row.update(phase="test",hardware_id=identity)
                for name,rs in staged.items():tables[name].extend({**r,"phase":"test","hardware_id":identity,"image":image} for r in rs)
                status.update(status="completed",search_status=row["search_status"])
            except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
                status.update(status="failed",detail=str(exc));meta["failed_tasks"].append(dict(phase="test",hardware_id=identity,image=image))
                row=dict(phase="test",hardware_id=identity,image=image,status="run_failed",visual_status="not_available",roi_drop_score=0)
                tables["summary_all.csv"].append(row)
            status["elapsed_seconds"]=time.monotonic()-start;statuses.append(status);best.append(row)
            meta["test_task_seconds"]+=status["elapsed_seconds"];meta["test_wall_seconds"]=time.monotonic()-begin;flush()
        if any(digest(out/name)!=value or digest(source/name)!=value for name,value in frozen.items()):raise ValueError("Frozen bank changed during retest")
        meta.update(completed=True,all_succeeded=not meta["failed_tasks"],seed_bank_unchanged_after_test=True);flush()
        print(f"Reused {identity}; no hardware screening performed. {sum(r['status']=='numeric_eligible' for r in best)}/{len(images)} images numeric eligible; visual review pending.")
        print(f"Image manifest: {out/'images_all.csv'}")
        return 0 if meta["all_succeeded"] else 1
    except KeyboardInterrupt:
        if owned and meta is not None:meta["error"]="Interrupted by user";atomic_json(out/"RUN.json",meta)
        return 130
    except (OSError,ValueError,RuntimeError,KeyError,TypeError,csv.Error) as exc:
        if owned and meta is not None:meta["error"]=str(exc);atomic_json(out/"RUN.json",meta)
        print(f"Frozen retest failed: {exc}",file=sys.stderr);return 1

if __name__=="__main__":sys.exit(main())
