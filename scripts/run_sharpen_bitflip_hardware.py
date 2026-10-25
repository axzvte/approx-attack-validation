#!/usr/bin/env python3
"""Sharpening: discover hardware, screen fixed structures, calibrate held-out intervals.

User-run only. The authoring agent must not execute experiments or tests.
"""
import argparse
import csv
import hashlib
import json
import math
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import subprocess
import sys
import time

from run_sharpen_bitflip_batch import atomic_csv, atomic_json, process, read_csv

MONITORS={"input1","input2","baseline_output"}
APPLICATION_NODES={"sharpen":4,"conv":8,"sobel":11,"dct":32}
SUMMARY_FIELDS=["phase","hardware_id","image","candidate_id","status","installed_nodes","triggered_nodes","visual_status","search_status"]
TABLES={"summary_all.csv":("summary.csv",SUMMARY_FIELDS),
        "configurations_all.csv":("configurations.csv",["hardware_id","image","candidate_id","node_id","monitor","bit"]),
        "search_stats_all.csv":("search_stats.csv",["hardware_id","image","contexts_built","context_budget_exhausted"]),
        "images_all.csv":("images.csv",["hardware_id","image","candidate_id","image_kind","path"])}

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def resolve_staged_image_path(data,image):
    if not 1<=image<=40:raise ValueError("Image index must stay in1..40")
    stage=data/f"stage{(image-1)//10+1}";name=f"image_{image:02d}.jpg"
    nested,flat=stage/"input"/name,stage/name
    if nested.is_file():return nested
    if flat.is_file():return flat
    raise ValueError(f"Missing input image; checked: {nested} and {flat}")

def split_range(text):
    values=text.split(":")
    if len(values)!=2:raise ValueError("Split must be an inclusive FIRST:LAST range")
    first,last=map(int,values)
    if not 1<=first<=last<=40:raise ValueError("Image split must stay in1..40")
    return list(range(first,last+1))

def hardware_key(rows,native_nodes=4):
    result=[];seen=set()
    for row in rows:
        node=int(row["node_id"]);bit=int(row["bit"]);monitor=row["monitor"]
        if node not in range(native_nodes) or bit not in range(12) or monitor not in MONITORS or node in seen:
            raise ValueError("Invalid/duplicate hardware node, monitor or bit")
        seen.add(node);result.append((node,monitor,bit))
    if not 2<=len(result)<=native_nodes:raise ValueError(f"A structure needs2..{native_nodes} installed nodes")
    return tuple(sorted(result))

def interval_rows(rows,native_nodes=4):
    key=hardware_key(rows,native_nodes);out=[]
    for r in rows:
        lo,hi=int(r["lower"]),int(r["upper"])
        mn,mx=(-4096,4095) if r["monitor"]=="baseline_output" else (-2048,2047)
        if not mn<=lo<=hi<=mx:raise ValueError("Interval endpoints do not fit the original monitor domain")
        out.append(dict(node_id=int(r["node_id"]),monitor=r["monitor"],bit=int(r["bit"]),lower=lo,upper=hi))
    return key,sorted(out,key=lambda r:r["node_id"])

def finite_nonnegative(row,key):
    value=float(row[key])
    if not math.isfinite(value) or value<0:raise ValueError(f"Invalid {key}")
    return value

def drop_score(baseline_mse,selected_mse):
    # 100dB is a ranking-only numerical cap for exact zero MSE, not a quality gate.
    def psnr(mse):return min(100.0,10*math.log10(65025/mse)) if mse else 100.0
    return max(0.0,psnr(baseline_mse)-psnr(selected_mse))

def training_pool(report,indices,floor,application="sharpen",native_nodes=4):
    if (report/"RUN.json").is_file():
        source_run=json.loads((report/"RUN.json").read_text(encoding="utf-8-sig"))
        if source_run.get("completed") is not True or source_run.get("all_succeeded") is not True:
            raise ValueError("Training report metadata is incomplete or contains failed runs")
    summaries=read_csv(report/"summary_all.csv",["image","candidate_id","status","global_mse","roi_mse"])
    configs=read_csv(report/"configurations_all.csv",["image","candidate_id","node_id","monitor","bit","lower","upper"])
    allowed=set(indices);baselines={};groups={};source_ids=set();source_status={}
    for row in summaries+configs:
        if int(row["image"]) not in allowed:raise ValueError("Training report contains screening/test/out-of-split images")
        app=row.get("application","sharpen").lower()
        if app=="sharpening":app="sharpen"
        if app not in APPLICATION_NODES or (application=="sharpen" and app!="sharpen"):
            raise ValueError("Training report has an unsupported application")
        if application!="sharpen" and "application" not in row:raise ValueError("Circuit training sources must identify the application")
    if application!="sharpen":
        summaries=[r for r in summaries if r["application"].lower()==application]
        configs=[r for r in configs if r["application"].lower()==application]
    for row in summaries:
        image=int(row["image"]);identity=(image,row["candidate_id"])
        if identity in source_ids:raise ValueError("Duplicate training summary candidate")
        source_ids.add(identity)
        source_status[identity]=row["status"]
        if row["status"] not in ("reference","numeric_eligible","no_numeric_candidate"):
            raise ValueError("Training source has a failed or unknown candidate status")
        if row["status"]=="reference":
            if image in baselines:raise ValueError("Duplicate baseline for a training image")
            baselines[image]=finite_nonnegative(row,"roi_mse")
    if set(baselines)!=allowed:raise ValueError("Every training image requires a completed baseline report")
    for r in configs:
        if application=="sharpen" and "node_name" in r and r["node_name"]!=f"A{int(r['node_id'])+1}":raise ValueError("Non-Sharpening training node name")
        identity=(int(r["image"]),r["candidate_id"])
        if identity not in source_ids or source_status[identity]!="numeric_eligible":
            raise ValueError("Training configuration is not attached to a numeric candidate")
        groups.setdefault(identity,[]).append(r)
    pool={};image_queues={i:[] for i in indices}
    for row in summaries:
        if row["status"]!="numeric_eligible":continue
        image=int(row["image"]);g=finite_nonnegative(row,"global_mse");roi=finite_nonnegative(row,"roi_mse")
        if g>65025*10**(-floor/10)*(1+1e-10) or roi<=baselines[image]+1e-10:continue
        key,intervals=interval_rows(groups.get((image,row["candidate_id"]),[]),native_nodes)
        item=pool.setdefault(key,dict(key=key,sources=[]))
        source=dict(image=image,candidate_id=row["candidate_id"],roi_drop_score=drop_score(baselines[image],roi),global_mse=g,intervals=intervals)
        item["sources"].append(source);image_queues[image].append((source["roi_drop_score"],-g,key))
    ordered=sorted(pool)
    for index,key in enumerate(ordered,1):pool[key]["hardware_id"]=f"h{index:04d}"
    # Deterministic round robin preserves evidence from different training images.
    order=[];seen=set()
    queues={i:sorted(image_queues[i],key=lambda x:(-x[0],-x[1],x[2])) for i in indices}
    for depth in range(max((len(q) for q in queues.values()),default=0)):
        for image in indices:
            if depth<len(queues[image]):
                key=queues[image][depth][2]
                if key not in seen:seen.add(key);order.append(key)
    return [pool[k] for k in ordered],order

def flags_file(path):
    flags={}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if "=" in line:
            k,v=line.split("=",1);flags[k]=v
    return flags

def fixed_report(report,image,hardware,mode,prefix,floor,native_nodes=4,application=None):
    flags=flags_file(report/"RUN.txt")
    if flags.get("completed")!="1" or flags.get("image")!=str(image):raise ValueError("Incomplete or mismatched fixed report")
    if application is not None and flags.get("application")!=application:raise ValueError("Fixed report belongs to a different application")
    staged={name:read_csv(report/source,[f for f in fields if f not in ("phase","hardware_id","image")]) for name,(source,fields) in TABLES.items()}
    if application is not None:
        for rows in staged.values():
            for row in rows:
                if ("application" in row and row["application"]!=application) or ("image" in row and int(row["image"])!=image):
                    raise ValueError("CSV application/image does not match fixed report")
    summaries=staged["summary_all.csv"];base=[r for r in summaries if r["status"]=="reference"]
    selected=[r for r in summaries if r["candidate_id"]!="baseline"]
    if len(base)!=1 or len(selected)!=1:raise ValueError("Fixed report needs exactly one baseline and selected row")
    b=base[0];row=selected[0]
    baseline_roi=finite_nonnegative(b,"roi_mse");finite_nonnegative(b,"global_mse")
    if row["status"] not in ("numeric_eligible","quality_not_met","no_legal_interval_state"):
        raise ValueError("Unknown fixed interval outcome")
    if int(row["installed_nodes"])!=len(hardware):raise ValueError("Installed-node count changed")
    configs=staged["configurations_all.csv"]
    if row["status"]=="no_legal_interval_state":
        if configs:raise ValueError("No-state report unexpectedly contains an executed configuration")
        score=0.0
    else:
        got,_=interval_rows(configs,native_nodes)
        if got!=hardware:raise ValueError("Fixed node, monitor or bit changed in report")
        if any(r["candidate_id"]!=row["candidate_id"] for r in configs):raise ValueError("Wrong selected configuration identity")
        triggered=sum(int(r["dynamic_triggers"])>0 for r in configs)
        if int(row["triggered_nodes"])!=triggered:raise ValueError("Trigger count does not match installed rows")
        g=finite_nonnegative(row,"global_mse");roi=finite_nonnegative(row,"roi_mse");finite_nonnegative(row,"non_roi_mse")
        eligible=g<=65025*10**(-floor/10)*(1+1e-10) and roi>baseline_roi+1e-10
        if (row["status"]=="numeric_eligible")!=eligible:raise ValueError("Numeric eligibility disagrees with exported metrics")
        score=drop_score(baseline_roi,roi) if eligible else 0.0
    stats=staged["search_stats_all.csv"]
    if len(stats)!=1:raise ValueError("Missing search statistics")
    if row["search_status"] not in ("completed","budget_exhausted"):raise ValueError("Invalid search budget status")
    image_rows=staged["images_all.csv"]
    if mode=="none" and image_rows:raise ValueError("Unexpected images in data-only phase")
    if mode=="compact":
        expected=3 if row["status"]=="numeric_eligible" else 0
        if len(image_rows)!=expected or (expected and ({r["image_kind"] for r in image_rows}!={"output","roi_comparison","background_comparison"} or any(r["candidate_id"]!=row["candidate_id"] for r in image_rows))):
            raise ValueError("Compact images do not match selected candidate")
    paths={}
    for r in image_rows:
        relative=Path(r["path"])
        if relative.is_absolute():raise ValueError("Image paths must be relative")
        resolved=(report/relative).resolve()
        try:resolved.relative_to(report.resolve())
        except ValueError:raise ValueError("Image manifest escapes report") from None
        if not resolved.is_file() or not resolved.stat().st_size:raise ValueError("Missing exported image")
        r["path"]=(Path(prefix)/relative).as_posix()
        if r["candidate_id"]==row["candidate_id"]:paths[r["image_kind"]]=r["path"]
    result={**row,"image":image,"baseline_roi_mse":baseline_roi,"roi_drop_score":score}
    for kind in ("output","roi_comparison","background_comparison"):result[kind+"_path"]=paths.get(kind,"")
    return staged,result

def rank_hardware(candidates,results,screen):
    ranked=[]
    for h in candidates:
        rows=[r for r in results if r["hardware_id"]==h["hardware_id"]]
        complete=len(rows)==len(screen) and not any(r["status"]=="run_failed" for r in rows)
        scores=[float(r.get("roi_drop_score",0)) if r["status"]=="numeric_eligible" else 0.0 for r in rows]
        successes=sum(r["status"]=="numeric_eligible" for r in rows)
        ranked.append(dict(hardware_id=h["hardware_id"],screen_images=len(screen),evaluated_images=len(rows),complete=int(complete),
                           numeric_successes=successes,numeric_success_fraction=successes/len(screen),
                           worst_roi_drop_score=min(scores,default=0),mean_roi_drop_score=sum(scores)/len(screen),
                           success_mean_roi_drop_score=sum(scores)/successes if successes else "",
                           success_min_roi_drop_score=min((float(r["roi_drop_score"]) for r in rows if r["status"]=="numeric_eligible"),default=""),
                           installed_nodes=len(h["key"]),hardware_key=str(h["key"])))
    ranked.sort(key=lambda r:(-r["complete"],-r["numeric_successes"],-r["worst_roi_drop_score"],-r["mean_roi_drop_score"],r["installed_nodes"],r["hardware_key"]))
    for i,r in enumerate(ranked,1):r["rank"]=i
    return ranked

def ordered_screen_results(jobs,work,workers):
    """Only workers execute subprocesses; caller owns all shared reports.

    Completion timing cannot change aggregation order or ranking. On interruption
    queued jobs are cancelled; already active children are allowed to finish.
    """
    if workers==1:
        for index,job in enumerate(jobs):yield index,work(job)
        return
    executor=ThreadPoolExecutor(max_workers=workers);futures=[]
    try:
        for job in jobs:futures.append(executor.submit(work,job))
        for index,future in enumerate(futures):yield index,future.result()
    finally:
        for future in futures:future.cancel()
        executor.shutdown(wait=True)

def protocol_parser(root,application="sharpen"):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("data",type=Path);p.add_argument("output",type=Path);p.add_argument("--training-report",type=Path)
    p.add_argument("--train",default="1:10");p.add_argument("--screen",default="11:20");p.add_argument("--test",default="31:40")
    p.add_argument("--max-hardware",type=int,default=12);p.add_argument("--seed-limit",type=int,default=8)
    p.add_argument("--beam",type=int,default=4);p.add_argument("--refinement",type=int,default=1);p.add_argument("--restarts",type=int,default=2)
    p.add_argument("--boundary-budget",type=int,default=24);p.add_argument("--training-context-budget",type=int,default=256)
    p.add_argument("--context-budget",type=int,default=128);p.add_argument("--training-top",type=int,default=2)
    p.add_argument("--global-psnr",type=float,default=30);p.add_argument("--image-mode",choices=("compact","full","none"),default="compact")
    p.add_argument("--build-dir",type=Path,default=root/"build-bitflip");p.add_argument("--jobs",type=int,default=4)
    p.add_argument("--screen-workers",type=int,default=min(4,os.cpu_count() or 1),help="Concurrent screening subprocesses; 1 is serial. --jobs controls compilation only.")
    p.add_argument("--skip-build",action="store_true");p.add_argument("--fixed-exe",type=Path);p.add_argument("--discovery-exe",type=Path)
    if application!="sharpen":
        p.add_argument("--max-full-evals",type=int,default=256);p.add_argument("--full-candidates",type=int,default=8)
        p.add_argument("--training-max-full-evals",type=int,default=4096);p.add_argument("--training-full-candidates",type=int,default=16)
    return p

def protocol_splits(args):
    train,screen,test=split_range(args.train),split_range(args.screen),split_range(args.test)
    if set(train)&set(screen) or set(train)&set(test) or set(screen)&set(test):raise ValueError("Training, screening and test splits must be disjoint")
    if not (0<=args.max_hardware<=10000 and 1<=args.seed_limit<=1000 and 1<=args.beam<=64 and 0<=args.refinement<=10 and 1<=args.restarts<=8 and
            (args.boundary_budget==0 or 2<=args.boundary_budget<=8192) and 4<=args.training_context_budget<=1000000 and
            1<=args.context_budget<=1000000 and 1<=args.training_top<=10 and 1<=args.jobs<=64 and 1<=args.screen_workers<=64 and
            math.isfinite(args.global_psnr) and 0<=args.global_psnr<=1000):raise ValueError("Invalid protocol/search options")
    if hasattr(args,"max_full_evals") and not (1<=args.max_full_evals<=10000000 and 1<=args.full_candidates<=1024 and
            1<=args.training_max_full_evals<=10000000 and 1<=args.training_full_candidates<=1024):
        raise ValueError("Invalid full-evaluation budgets")
    if (args.fixed_exe or args.discovery_exe) and not args.skip_build:raise ValueError("External executables require --skip-build")
    return train,screen,test

def main(argv=None,application="sharpen"):
    root=Path(__file__).resolve().parents[1];native_nodes=APPLICATION_NODES[application]
    args=protocol_parser(root,application).parse_args(argv);owned=False;meta=None
    try:
        train,screen,test=protocol_splits(args)
        data=args.data.resolve();out=args.output.resolve();build=args.build_dir.resolve()
        fixed_target="sharpen_bitflip_fixed" if application=="sharpen" else "circuit_bitflip_fixed"
        discovery_target="sharpen_bitflip_joint" if application=="sharpen" else "circuit_bitflip_joint"
        fixed=args.fixed_exe.resolve() if args.fixed_exe else build/fixed_target
        discovery=args.discovery_exe.resolve() if args.discovery_exe else build/discovery_target
        if not (data/"mask/roi_mask.jpg").is_file():raise ValueError("Missing original ROI mask")
        # Check names/existence only, before expensive screening; do not inspect
        # held-out pixels or use them in hardware selection.
        for image in screen+test+([] if args.training_report else train):resolve_staged_image_path(data,image)
        source=args.training_report.resolve() if args.training_report else out/"training"
        if args.training_report:
            pool,order=training_pool(source,train,args.global_psnr,application,native_nodes)  # Validate source leakage before owning output.
        out.mkdir(parents=True,exist_ok=False);owned=True
        for folder in ("logs","hardware","screen","test"):(out/folder).mkdir()
        meta=dict(application=application,native_node_count=native_nodes,completed=False,all_succeeded=False,selection_status="not_selected",test_executed=False,
                  splits=dict(train=train,screen=screen,test=test),source_training_report=str(source),
                  data=str(data),fixed_executable=str(fixed),discovery_executable=str(discovery),
                  options={k:v for k,v in vars(args).items() if isinstance(v,(int,float,str,bool))},
                  hardware_fixed_fields=["node_id","monitor","bit"],adaptive_fields=["lower","upper"],
                  enable_parameter="absent",ablations="disabled",seed_policy="train-only immutable bank; reset for each screen/test image",
                  ranking="success count, worst then mean nonnegative ROI PSNR drop over all screen images, fewer installed nodes, key",
                  ranking_psnr_cap_db=100,failed_tasks=[],visual_status="pending_user_review",seed_bank_unchanged_after_test=False)
        meta.update(screen_workers=args.screen_workers,screen_wall_seconds=0,screen_task_seconds=0,screen_task_count=0,
                    test_wall_seconds=0,test_task_seconds=0,profiling="fixed monitor only; per-image reference and single-trace reuse" if application=="sharpen" else "native exact conditional pixel scores or full reconstructed-image scores; fixed monitor and bit")
        statuses=[];screen_results=[];test_results=[];tables={name:[] for name in TABLES}
        def flush():
            atomic_json(out/"RUN.json",meta)
            atomic_csv(out/"run_status.csv",statuses,["phase","hardware_id","image","status","returncode","elapsed_seconds","log","detail"])
            atomic_csv(out/"screening_results.csv",screen_results,SUMMARY_FIELDS+["roi_drop_score"])
            atomic_csv(out/"best_per_image.csv",test_results,SUMMARY_FIELDS+["roi_drop_score"])
            for name,(_,fields) in TABLES.items():atomic_csv(out/name,tables[name],fields)
        flush()
        try:
            rev=subprocess.run(["git","rev-parse","HEAD"],cwd=root,capture_output=True,text=True);meta["code_revision"]=rev.stdout.strip() if rev.returncode==0 else "unavailable"
        except OSError:meta["code_revision"]="unavailable"
        if not args.skip_build:
            for i,command in enumerate((["cmake","-S",root,"-B",build,"-DCMAKE_BUILD_TYPE=Release"],
                                         ["cmake","--build",build,"--target",fixed_target,discovery_target,f"-j{args.jobs}"])):
                if process(command,out/"build.log",root,append=bool(i)):raise RuntimeError("Build failed; see build.log")
        if not fixed.is_file():raise ValueError(f"Missing fixed-interval executable: {fixed}")
        if args.training_report:
            archive=out/"training_source";archive.mkdir()
            for filename in ("summary_all.csv","configurations_all.csv","RUN.json"):
                if (source/filename).is_file():shutil.copyfile(source/filename,archive/filename)
            source=archive
            pool,order=training_pool(source,train,args.global_psnr,application,native_nodes)
            meta["training_source_archive"]="training_source"
        if not args.training_report:
            if not discovery.is_file():raise ValueError("Missing discovery executable")
            source.mkdir();ss=[];cs=[]
            for image in train:
                report=source/f"image_{image:02d}";log=f"logs/train_image_{image:02d}.log"
                status=dict(phase="train",hardware_id="",image=image,status="running",returncode="",elapsed_seconds="",log=log,detail="");statuses.append(status);begin=time.monotonic()
                try:
                    command=[discovery,data,report,image,args.beam,args.refinement,args.boundary_budget,args.training_top,args.restarts,args.global_psnr,"--context-budget",args.training_context_budget,"--no-ablations","--image-mode","none"]
                    if application!="sharpen":
                        command=[discovery,application,data,report,"--image",image,"--beam",args.beam,"--refinement",args.refinement,
                                 "--boundary-budget",args.boundary_budget,"--top-per-count",args.training_top,"--restarts",args.restarts,
                                 "--global-psnr",args.global_psnr,"--max-contexts",args.training_context_budget,
                                 "--max-full-evals",args.training_max_full_evals,"--dct-full-candidates",args.training_full_candidates,"--image-mode","none"]
                    code=process(command,out/log,root);status["returncode"]=code
                    if code:raise RuntimeError("Training discovery failed")
                    notes=flags_file(report/"RUN.txt")
                    if notes.get("completed")!="1" or notes.get("image")!=str(image):raise RuntimeError("Training discovery report incomplete or image mismatched")
                    if application!="sharpen" and notes.get("application")!=application:raise ValueError("Discovery application mismatch")
                    summaries=read_csv(report/"summary.csv",["candidate_id","status","global_mse","roi_mse"])
                    configs=read_csv(report/"configurations.csv",["candidate_id","node_id","monitor","bit","lower","upper"])
                    for row in summaries+configs:
                        if ("application" in row and row["application"].lower() not in (("sharpen","sharpening") if application=="sharpen" else (application,))) or ("image" in row and int(row["image"])!=image):
                            raise ValueError("Discovery CSV application/image mismatch")
                    ss.extend({**r,"image":image,"application":application} for r in summaries)
                    cs.extend({**r,"image":image,"application":application} for r in configs)
                    status["status"]="completed"
                except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
                    status.update(status="failed",detail=str(exc));meta["failed_tasks"].append(dict(phase="train",image=image))
                status["elapsed_seconds"]=time.monotonic()-begin;flush()
            atomic_csv(source/"summary_all.csv",ss,["image","candidate_id","status","global_mse","roi_mse"])
            atomic_csv(source/"configurations_all.csv",cs,["image","candidate_id","node_id","monitor","bit","lower","upper"])
            if meta["failed_tasks"]:raise RuntimeError("Training failures recorded; screening not started")
            pool,order=training_pool(source,train,args.global_psnr,application,native_nodes)
        meta["training_source_hashes"]={name:digest(source/name) for name in ("summary_all.csv","configurations_all.csv")}
        chosen_keys=order[:args.max_hardware] if args.max_hardware else order
        candidates=[h for h in pool if h["key"] in set(chosen_keys)]
        by_id={h["hardware_id"]:h for h in candidates};selected_keys=set(chosen_keys)
        atomic_csv(out/"candidate_pool.csv",[dict(hardware_id=h["hardware_id"],installed_nodes=len(h["key"]),hardware_key=str(h["key"]),
                    source_count=len(h["sources"]),source_images=";".join(map(str,sorted({r["image"] for r in h["sources"]}))),screen_selected=int(h["key"] in selected_keys)) for h in pool],
                   ["hardware_id","installed_nodes","hardware_key","source_count","source_images","screen_selected"])
        evidence=[];bank_hashes={}
        for h in pool:
            folder=out/"hardware"/h["hardware_id"];folder.mkdir()
            atomic_csv(folder/"hardware.csv",[dict(node_id=n,monitor=m,bit=b) for n,m,b in h["key"]],["node_id","monitor","bit"])
            seed_rows=[];seen_seeds=set();sources=sorted(h["sources"],key=lambda r:(-r["roi_drop_score"],r["image"],r["candidate_id"]))
            for r in sources:
                evidence.append(dict(hardware_id=h["hardware_id"],image=r["image"],candidate_id=r["candidate_id"],roi_drop_score=r["roi_drop_score"]))
                identity=tuple((x["node_id"],x["lower"],x["upper"]) for x in r["intervals"])
                if identity in seen_seeds or len(seen_seeds)>=args.seed_limit:continue
                seen_seeds.add(identity);sid=f"train_{r['image']:02d}_{r['candidate_id']}"
                seed_rows.extend(dict(seed_id=sid,**x) for x in r["intervals"])
            atomic_csv(folder/"seeds.csv",seed_rows,["seed_id","node_id","monitor","bit","lower","upper"])
            for path in (folder/"hardware.csv",folder/"seeds.csv"):bank_hashes[str(path.relative_to(out))]=digest(path)
        atomic_csv(out/"candidate_evidence.csv",evidence,["hardware_id","image","candidate_id","roi_drop_score"])
        meta["candidate_count"]=len(pool);meta["screen_candidate_count"]=len(candidates);meta["training_bank_hashes"]=bank_hashes;flush()
        if not candidates:
            meta.update(completed=True,all_succeeded=True,selection_status="no_candidate_hardware");flush();print("No training candidate hardware; final test not started");return 0
        def evaluate(phase,h,image,mode,hardware_path,seeds_path):
            relative=f"{phase}/{h['hardware_id']}/image_{image:02d}";report=out/relative;report.parent.mkdir(parents=True,exist_ok=True)
            log=f"logs/{phase}_{h['hardware_id']}_image_{image:02d}.log";status=dict(phase=phase,hardware_id=h["hardware_id"],image=image,status="running",returncode="",elapsed_seconds="",log=log,detail="");begin=time.monotonic();staged={}
            try:
                command=[fixed,*([application] if application!="sharpen" else []),data,report,"--hardware",hardware_path,"--seeds",seeds_path,"--image",image,"--beam",args.beam,"--refinement",args.refinement,"--restarts",args.restarts,"--boundary-budget",args.boundary_budget,"--context-budget",args.context_budget,"--global-psnr",args.global_psnr,"--image-mode",mode]
                if application!="sharpen":command.extend(["--max-full-evals",args.max_full_evals,"--full-candidates",args.full_candidates])
                code=process(command,out/log,root);status["returncode"]=code
                if code:raise RuntimeError(f"Interval executable returned {code}")
                staged,row=fixed_report(report,image,h["key"],mode,relative,args.global_psnr,native_nodes,application if application!="sharpen" else None);row.update(phase=phase,hardware_id=h["hardware_id"])
                status["status"]="completed";status["search_status"]=row["search_status"]
            except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
                status.update(status="failed",detail=str(exc))
                row=dict(phase=phase,hardware_id=h["hardware_id"],image=image,status="run_failed",visual_status="not_available",roi_drop_score=0)
            status["elapsed_seconds"]=time.monotonic()-begin
            return status,row,staged
        def collect(result):
            # Main thread alone owns metadata, merged tables and atomic writers.
            status,row,staged=result;statuses.append(status)
            if status["status"]=="failed":
                meta["failed_tasks"].append({k:status[k] for k in ("phase","hardware_id","image")})
                if row["phase"]=="test":tables["summary_all.csv"].append(row)
            elif row["phase"]=="test":
                for name,rs in staged.items():tables[name].extend({**r,"phase":"test","hardware_id":row["hardware_id"],"image":row["image"]} for r in rs)
            return row
        jobs=[(h,image) for h in candidates for image in screen];meta["screen_task_count"]=len(jobs)
        def screen_one(job):
            h,image=job;folder=out/"hardware"/h["hardware_id"]
            return evaluate("screen",h,image,"none",folder/"hardware.csv",folder/"seeds.csv")
        screen_begin=time.monotonic();flush()
        try:
            for _,result in ordered_screen_results(jobs,screen_one,args.screen_workers):
                screen_results.append(collect(result));meta["screen_task_seconds"]+=result[0]["elapsed_seconds"]
                meta["screen_wall_seconds"]=time.monotonic()-screen_begin;flush()
        finally:meta["screen_wall_seconds"]=time.monotonic()-screen_begin
        flush()
        ranking=rank_hardware(candidates,screen_results,screen);atomic_csv(out/"screening_ranking.csv",ranking,["rank","hardware_id","complete","numeric_successes","screen_images","numeric_success_fraction","worst_roi_drop_score","mean_roi_drop_score","installed_nodes"])
        if meta["failed_tasks"]:raise RuntimeError("Screening program/report failures: preserved all results; hardware freeze and test blocked")
        if any(digest(out/path)!=value for path,value in bank_hashes.items()):raise RuntimeError("Training hardware/seed bank changed during screening")
        if not ranking or not ranking[0]["numeric_successes"]:
            meta.update(completed=True,all_succeeded=True,selection_status="no_numeric_eligible_hardware");flush();print("No structure found numeric-eligible intervals on screening images; final test not started");return 0
        h=by_id[ranking[0]["hardware_id"]]
        shutil.copyfile(out/"hardware"/h["hardware_id"]/"hardware.csv",out/"frozen_hardware.csv")
        shutil.copyfile(out/"hardware"/h["hardware_id"]/"seeds.csv",out/"frozen_seeds.csv")
        frozen={name:digest(out/name) for name in ("frozen_hardware.csv","frozen_seeds.csv")}
        meta.update(selection_status="selected",selected_hardware_id=h["hardware_id"],frozen_hashes=frozen,test_executed=True)
        atomic_json(out/"frozen_protocol.json",dict(application=application,native_node_count=native_nodes,hardware_id=h["hardware_id"],hardware_key=h["key"],splits=meta["splits"],options=meta["options"],frozen_hashes=frozen,training_bank_hashes=bank_hashes,ranking=meta["ranking"]))
        flush()  # Freeze before observing any test image.
        test_begin=time.monotonic()
        try:
            for image in test:
                if any(digest(out/name)!=value for name,value in frozen.items()):raise RuntimeError("Frozen hardware/seed file changed before test image")
                result=evaluate("test",h,image,args.image_mode,out/"frozen_hardware.csv",out/"frozen_seeds.csv")
                test_results.append(collect(result));meta["test_task_seconds"]+=result[0]["elapsed_seconds"]
                meta["test_wall_seconds"]=time.monotonic()-test_begin;flush()
        finally:meta["test_wall_seconds"]=time.monotonic()-test_begin
        unchanged=all(digest(out/name)==value for name,value in frozen.items()) and all(digest(out/path)==value for path,value in bank_hashes.items())
        if not unchanged:raise RuntimeError("Frozen or training bank changed during held-out calibration")
        meta.update(completed=True,all_succeeded=not meta["failed_tasks"],seed_bank_unchanged_after_test=True);flush()
        print(f"Frozen {h['hardware_id']}; {sum(r['status']=='numeric_eligible' for r in test_results)}/{len(test)} test images numeric eligible. Visual review pending.")
        print(f"Share {out/'screening_ranking.csv'}, {out/'frozen_hardware.csv'}, {out/'best_per_image.csv'} and {out/'search_stats_all.csv'}")
        return 0 if meta["all_succeeded"] else 1
    except KeyboardInterrupt:
        if owned and meta is not None:meta["error"]="Interrupted by user";atomic_json(out/"RUN.json",meta)
        print("Interrupted; written results preserved",file=sys.stderr);return 130
    except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
        if owned and meta is not None:meta["error"]=str(exc);atomic_json(out/"RUN.json",meta)
        print(f"Hardware pipeline failed: {exc}",file=sys.stderr);return 1

if __name__=="__main__":sys.exit(main())
