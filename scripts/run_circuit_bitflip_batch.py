#!/usr/bin/env python3
"""User-run Conv/Sobel/DCT ten-image bit-flip trials. No ablations are executed."""
import argparse
import csv
import math
from pathlib import Path
import subprocess
import sys

# Stable report/log helpers shared with the existing user-run Sharpening batch.
from run_sharpen_bitflip_batch import atomic_csv, atomic_json, process, read_csv

APPLICATIONS={"conv":8,"sobel":11,"dct":32}
TABLES={
    "summary_all.csv":("summary.csv",["application","image","candidate_id","active_nodes","status","visual_status","search_status"]),
    "configurations_all.csv":("configurations.csv",["application","image","candidate_id","node_id"]),
    "search_stats_all.csv":("search_stats.csv",["application","image","contexts_built","search_status"]),
    "images_all.csv":("images.csv",["application","image","candidate_id","image_kind","path"]),
}

def load_report(report,application,image,mode):
    flags={}
    for line in (report/"RUN.txt").read_text(encoding="utf-8-sig").splitlines():
        if "=" in line:
            key,value=line.split("=",1);flags[key]=value
    if flags.get("completed")!="1" or flags.get("application")!=application or flags.get("image")!=str(image):
        raise ValueError("Incomplete report or application/image mismatch")
    staged={}
    for aggregate,(source,fields) in TABLES.items():
        rows=read_csv(report/source,[f for f in fields if f not in ("application","image")])
        for row in rows:
            if ("application" in row and row["application"]!=application) or ("image" in row and row["image"]!=str(image)):
                raise ValueError("CSV application/image does not match the requested task")
        staged[aggregate]=rows
    stats=staged["search_stats_all.csv"]
    if len(stats)!=1 or stats[0]["search_status"] not in ("completed","budget_exhausted"):
        raise ValueError("Missing or invalid search completion status")
    search_status=stats[0]["search_status"]
    if "search_status" in flags and flags["search_status"]!=search_status:
        raise ValueError("Inconsistent search status")
    summaries=staged["summary_all.csv"]
    if sum(r["status"]=="reference" for r in summaries)!=1 or any(r["search_status"]!=search_status for r in summaries):
        raise ValueError("Missing baseline or inconsistent summary status")
    if any(r["status"] not in ("reference","numeric_eligible","no_numeric_candidate") for r in summaries):
        raise ValueError("Unknown summary status")
    numeric=[r for r in summaries if r["status"]=="numeric_eligible"]
    for row in numeric:
        if not 2<=int(row["active_nodes"])<=APPLICATIONS[application] or any(not math.isfinite(float(row[k])) or float(row[k])<0 for k in ("global_mse","roi_mse")):
            raise ValueError("Invalid candidate metrics or active-node count")
    best=min(numeric,key=lambda r:(-float(r["roi_mse"]),float(r["global_mse"]),int(r["active_nodes"]),r["candidate_id"])) if numeric else None
    images=staged["images_all.csv"]
    if mode=="none" and images:
        raise ValueError("none mode contains image files")
    if mode=="compact":
        if best and (len(images)!=3 or {r["image_kind"] for r in images}!={"output","roi_comparison","background_comparison"} or any(r["candidate_id"]!=best["candidate_id"] for r in images)):
            raise ValueError("Compact manifest does not identify exactly three best-candidate images")
        if not best and images:
            raise ValueError("No-candidate compact report contains images")
    paths={}
    for row in images:
        relative=Path(row["path"])
        if relative.is_absolute():raise ValueError("Image paths must be relative")
        resolved=(report/relative).resolve()
        try:resolved.relative_to(report.resolve())
        except ValueError:raise ValueError("Image path escapes report") from None
        if not resolved.is_file() or not resolved.stat().st_size:raise ValueError(f"Missing image {relative}")
        row["path"]=(Path(application)/report.name/relative).as_posix()
        if best and row["candidate_id"]==best["candidate_id"]:paths[row["image_kind"]]=row["path"]
    selected={**best} if best else dict(status="no_numeric_candidate",visual_status="not_available",search_status=search_status)
    selected.update(application=application,image=image)
    for kind in ("output","roi_comparison","background_comparison"):selected[kind+"_path"]=paths.get(kind,"")
    return staged,selected,len(numeric),search_status

def main(argv=None):
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data",type=Path);parser.add_argument("output",type=Path)
    parser.add_argument("--applications",default="conv,sobel,dct")
    parser.add_argument("--first",type=int,default=1);parser.add_argument("--last",type=int,default=10)
    parser.add_argument("--beam",type=int,default=4);parser.add_argument("--refinement",type=int,default=1)
    parser.add_argument("--boundary-budget",type=int,default=24);parser.add_argument("--top-per-count",type=int,default=2)
    parser.add_argument("--restarts",type=int,default=2);parser.add_argument("--global-psnr",type=float,default=30)
    parser.add_argument("--max-active",type=int,default=0);parser.add_argument("--max-contexts",type=int,default=256)
    parser.add_argument("--max-full-evals",type=int,default=4096);parser.add_argument("--dct-full-candidates",type=int,default=16)
    parser.add_argument("--bits",default=",".join(map(str,range(12))))
    parser.add_argument("--image-mode",choices=("compact","full","none"),default="compact")
    parser.add_argument("--build-dir",type=Path,default=root/"build-bitflip");parser.add_argument("--jobs",type=int,default=4)
    parser.add_argument("--skip-build",action="store_true");parser.add_argument("--exe",type=Path)
    args=parser.parse_args(argv);owned=False;metadata=None
    try:
        apps=args.applications.split(",")
        if not apps or len(set(apps))!=len(apps) or any(a not in APPLICATIONS for a in apps):raise ValueError("Applications must be distinct conv,sobel,dct names")
        bits=[int(x) for x in args.bits.split(",")]
        if not bits or len(set(bits))!=len(bits) or any(b<0 or b>11 for b in bits):raise ValueError("Bits must be distinct integers in0..11")
        if not (1<=args.first<=args.last<=30 and 1<=args.beam<=64 and 0<=args.refinement<=10 and
                (args.boundary_budget==0 or 2<=args.boundary_budget<=8192) and 1<=args.top_per_count<=10 and
                1<=args.restarts<=8 and 1<=args.jobs<=64 and math.isfinite(args.global_psnr) and 0<=args.global_psnr<=1000 and
                (args.max_active==0 or 2<=args.max_active<=min(APPLICATIONS[a] for a in apps)) and
                1<=args.max_contexts<=1000000 and 1<=args.max_full_evals<=10000000 and 1<=args.dct_full_candidates<=1024):
            raise ValueError("Invalid search/build options or image range")
        if args.exe and not args.skip_build:raise ValueError("External --exe requires --skip-build")
        data=args.data.resolve();output=args.output.resolve();build=args.build_dir.resolve()
        exe=args.exe.resolve() if args.exe else build/"circuit_bitflip_joint"
        if not (data/"mask/roi_mask.jpg").is_file():raise ValueError("Missing ROI mask")
        output.mkdir(parents=True,exist_ok=False);owned=True;(output/"logs").mkdir()
        for application in apps:(output/application).mkdir()
        metadata=dict(completed=False,all_succeeded=False,applications=apps,first=args.first,last=args.last,
                      image_mode=args.image_mode,beam=args.beam,refinement=args.refinement,boundary_budget=args.boundary_budget,
                      top_per_count=args.top_per_count,restarts=args.restarts,global_psnr=args.global_psnr,max_active=args.max_active,
                      max_contexts=args.max_contexts,max_full_evals=args.max_full_evals,dct_full_candidates=args.dct_full_candidates,
                      bits=bits,data=str(data),executable=str(exe),ablations="disabled",failed_tasks=[],
                      visual_status="pending_user_review",selection="Independent per-application/per-image exploration; no hardware freeze")
        tasks=[(app,i) for app in apps for i in range(args.first,args.last+1)]
        statuses=[dict(application=a,image=i,status="not_run",returncode="",search_status="",numeric_candidates="",
                       report_dir=f"{a}/image_{i:02d}",log=f"logs/{a}_image_{i:02d}.log",detail="") for a,i in tasks]
        best_rows=[dict(application=a,image=i,status="not_run",visual_status="not_available") for a,i in tasks]
        tables={name:[] for name in TABLES}
        def flush():
            for name,(_,fields) in TABLES.items():atomic_csv(output/name,tables[name],fields)
            atomic_csv(output/"best_per_image.csv",best_rows,["application","image","candidate_id","active_nodes","status","visual_status","search_status"])
            atomic_csv(output/"run_status.csv",statuses,["application","image","status","returncode","search_status","numeric_candidates","report_dir","log","detail"])
            atomic_json(output/"RUN.json",metadata)
        flush()
        try:
            revision=subprocess.run(["git","rev-parse","HEAD"],cwd=root,capture_output=True,text=True)
            metadata["code_revision"]=revision.stdout.strip() if revision.returncode==0 else "unavailable"
        except OSError:metadata["code_revision"]="unavailable"
        if not args.skip_build:
            commands=(["cmake","-S",root,"-B",build,"-DCMAKE_BUILD_TYPE=Release"],
                      ["cmake","--build",build,"--target","circuit_bitflip_joint",f"-j{args.jobs}"])
            for step,command in enumerate(commands):
                code=process(command,output/"build.log",root,append=bool(step))
                if code:raise RuntimeError(f"Build failed with code {code}; see build.log")
        if not exe.is_file():raise ValueError(f"Missing executable {exe}")
        for position,(application,image) in enumerate(tasks):
            status=statuses[position];report=output/status["report_dir"]
            command=[exe,application,data,report,"--image",image,"--beam",args.beam,"--refinement",args.refinement,
                     "--boundary-budget",args.boundary_budget,"--top-per-count",args.top_per_count,"--restarts",args.restarts,
                     "--global-psnr",args.global_psnr,"--max-active",args.max_active,"--max-contexts",args.max_contexts,
                     "--max-full-evals",args.max_full_evals,"--dct-full-candidates",args.dct_full_candidates,
                     "--bits",",".join(map(str,bits)),"--image-mode",args.image_mode]
            print(f"\n=== {application} image {image} ({position+1}/{len(tasks)}) ===",flush=True)
            try:
                code=process(command,output/status["log"],root);status["returncode"]=code
                if code:raise RuntimeError(f"Executable returned {code}")
                staged,selected,count,search_status=load_report(report,application,image,args.image_mode)
                for name,rows in staged.items():tables[name].extend({**r,"application":application,"image":image} for r in rows)
                best_rows[position]=selected;status.update(status="completed",numeric_candidates=count,search_status=search_status)
            except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
                status.update(status="failed",detail=str(exc));metadata["failed_tasks"].append(dict(application=application,image=image))
                failed=dict(application=application,image=image,status="run_failed",visual_status="not_available")
                best_rows[position]=failed;tables["summary_all.csv"].append(failed)
                print(f"FAILED {application} image {image}: {exc}; continuing",file=sys.stderr,flush=True)
            flush()
        metadata["completed"]=True;metadata["all_succeeded"]=not metadata["failed_tasks"];flush()
        print(f"Attempted {len(tasks)} tasks; {len(metadata['failed_tasks'])} failed. Budget exhaustion is recorded separately.")
        print(f"Share {output/'best_per_image.csv'}, {output/'summary_all.csv'} and {output/'search_stats_all.csv'}")
        print(f"Images: {output/'images_all.csv'}; no ablations executed")
        return 0 if metadata["all_succeeded"] else 1
    except KeyboardInterrupt:
        if owned and metadata is not None:metadata["error"]="Interrupted by user";atomic_json(output/"RUN.json",metadata)
        print("Interrupted; existing reports and logs preserved",file=sys.stderr);return 130
    except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
        if owned and metadata is not None:metadata["error"]=str(exc);atomic_json(output/"RUN.json",metadata)
        print(f"Batch failed: {exc}",file=sys.stderr);return 1

if __name__=="__main__":sys.exit(main())
