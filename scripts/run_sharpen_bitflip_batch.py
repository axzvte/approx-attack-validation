#!/usr/bin/env python3
"""User-run ten-image joint trial. Build once, keep failures, merge small reports.

The authoring agent must not execute this script, tests or experiment binaries.
Search behavior is unchanged; compact/full/none only controls image exports.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import shlex
import subprocess
import sys

TABLES = {
    "summary_all.csv": ("summary.csv", ["image","candidate_id","active_nodes","status","visual_status"]),
    "configurations_all.csv": ("configurations.csv", ["image","candidate_id","node_id"]),
    "ablations_all.csv": ("ablations.csv", ["image","candidate_id","removed_node","status"]),
    "search_stats_all.csv": ("search_stats.csv", ["image","contexts_built"]),
    "images_all.csv": ("images.csv", ["image","candidate_id","image_kind","path"]),
}


def atomic_csv(path, rows, initial):
    fields = list(initial)
    for row in rows:
        for field in row:
            if field not in fields:
                fields.append(field)
    temporary = path.with_name(path.name+".tmp")
    with temporary.open("w",newline="",encoding="utf-8") as stream:
        writer=csv.DictWriter(stream,fields);writer.writeheader();writer.writerows(rows)
    temporary.replace(path)


def atomic_json(path, value):
    temporary=path.with_name(path.name+".tmp")
    temporary.write_text(json.dumps(value,indent=2)+"\n",encoding="utf-8")
    temporary.replace(path)


def read_csv(path, required):
    with path.open(newline="",encoding="utf-8-sig") as stream:
        reader=csv.DictReader(stream)
        if not reader.fieldnames or not set(required).issubset(reader.fieldnames):
            raise ValueError(f"Missing required columns in {path.name}")
        rows=list(reader)
    if any(None in row or any(v is None for v in row.values()) for row in rows):
        raise ValueError(f"Malformed row in {path.name}")
    return rows


def process(command, log, cwd, append=False):
    print("$ "+shlex.join(map(str,command)),flush=True)
    with log.open("a" if append else "w",encoding="utf-8") as stream:
        child=subprocess.Popen(list(map(str,command)),cwd=cwd,stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT,text=True,encoding="utf-8",errors="replace")
        try:
            for line in child.stdout:
                print(line,end="",flush=True);stream.write(line);stream.flush()
            return child.wait()
        finally:
            child.stdout.close()
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill();child.wait()


def load_report(report, image, mode):
    flags={}
    for line in (report/"RUN.txt").read_text(encoding="utf-8-sig").splitlines():
        if "=" in line:
            k,v=line.split("=",1);flags[k]=v
    if flags.get("completed")!="1":
        raise ValueError("Report has no final completed=1 marker")
    staged={}
    for aggregate,(source,fields) in TABLES.items():
        staged[aggregate]=read_csv(report/source,[field for field in fields if field!="image"])
    numeric=[r for r in staged["summary_all.csv"] if r["status"]=="numeric_eligible"]
    for row in numeric:
        if any(not math.isfinite(float(row[k])) or float(row[k])<0 for k in ("roi_mse","global_mse")):
            raise ValueError("Invalid numeric candidate metrics")
    best=min(numeric,key=lambda r:(-float(r["roi_mse"]),float(r["global_mse"]),int(r["active_nodes"]),r["candidate_id"])) if numeric else None
    images=staged["images_all.csv"]
    if mode=="none" and images:
        raise ValueError("none mode unexpectedly exported image files")
    if mode=="compact":
        kinds={r["image_kind"] for r in images}
        if best and (len(images)!=3 or kinds!={"output","roi_comparison","background_comparison"} or
                     any(r["candidate_id"]!=best["candidate_id"] for r in images)):
            raise ValueError("Compact image manifest does not match the best numeric candidate")
        if not best and images:
            raise ValueError("No-candidate compact report unexpectedly contains images")
    paths={}
    for row in images:
        relative=Path(row["path"])
        if relative.is_absolute():
            raise ValueError("Image manifest must contain relative paths")
        resolved=(report/relative).resolve()
        try:
            resolved.relative_to(report.resolve())
        except ValueError:
            raise ValueError("Image manifest escapes its report directory") from None
        if not resolved.is_file() or not resolved.stat().st_size:
            raise ValueError(f"Missing image: {relative}")
        row["path"]=(Path(report.name)/relative).as_posix()
        if best and row["candidate_id"]==best["candidate_id"]:
            paths[row["image_kind"]]=row["path"]
    selected=dict(image=image,**best) if best else dict(image=image,status="no_numeric_candidate",visual_status="not_available")
    for kind in ("output","roi_comparison","background_comparison"):
        selected[kind+"_path"]=paths.get(kind,"")
    return staged,selected,len(numeric)


def main(argv=None):
    root=Path(__file__).resolve().parents[1]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("data",type=Path)
    parser.add_argument("output",type=Path)
    parser.add_argument("--first",type=int,default=1)
    parser.add_argument("--last",type=int,default=10)
    parser.add_argument("--beam",type=int,default=4)
    parser.add_argument("--refinement",type=int,default=1)
    parser.add_argument("--boundary-budget",type=int,default=24)
    parser.add_argument("--top-per-count",type=int,default=2)
    parser.add_argument("--restarts",type=int,default=2)
    parser.add_argument("--global-psnr",type=float,default=30)
    parser.add_argument("--image-mode",choices=("compact","full","none"),default="compact")
    parser.add_argument("--build-dir",type=Path,default=root/"build-bitflip")
    parser.add_argument("--jobs",type=int,default=4)
    parser.add_argument("--skip-build",action="store_true")
    parser.add_argument("--exe",type=Path,help="Existing executable; requires --skip-build")
    args=parser.parse_args(argv)
    owned=False;metadata=None
    try:
        if not (1<=args.first<=args.last<=30 and 1<=args.beam<=64 and 0<=args.refinement<=10 and
                (args.boundary_budget==0 or 2<=args.boundary_budget<=8192) and
                1<=args.top_per_count<=10 and 1<=args.restarts<=8 and 1<=args.jobs<=64 and
                math.isfinite(args.global_psnr) and 0<=args.global_psnr<=1000):
            raise ValueError("Invalid image range or search/build options")
        if args.exe and not args.skip_build:
            raise ValueError("Use --skip-build when supplying an external --exe")
        data=args.data.resolve();output=args.output.resolve();build=args.build_dir.resolve()
        exe=args.exe.resolve() if args.exe else build/"sharpen_bitflip_joint"
        if not (data/"mask/roi_mask.jpg").is_file():
            raise ValueError("Missing data/mask/roi_mask.jpg")
        output.mkdir(parents=True,exist_ok=False);owned=True
        (output/"logs").mkdir()
        metadata=dict(completed=False,all_succeeded=False,first=args.first,last=args.last,
                      image_mode=args.image_mode,beam=args.beam,refinement=args.refinement,
                      boundary_budget=args.boundary_budget,top_per_count=args.top_per_count,
                      restarts=args.restarts,global_psnr=args.global_psnr,failed_images=[],
                      data=str(data),executable=str(exe),visual_status="pending_user_review",
                      selection="Independent per-image exploratory joint search; no hardware freeze")
        indices=list(range(args.first,args.last+1))
        statuses=[dict(image=i,status="not_run",returncode="",report_dir=f"image_{i:02d}",
                       log=f"logs/image_{i:02d}.log",numeric_candidates="",detail="") for i in indices]
        best_rows=[dict(image=i,status="not_run",visual_status="not_available") for i in indices]
        tables={name:[] for name in TABLES}
        def flush():
            for name,(_,fields) in TABLES.items():
                atomic_csv(output/name,tables[name],fields)
            atomic_csv(output/"best_per_image.csv",best_rows,["image","candidate_id","active_nodes","status","visual_status"])
            atomic_csv(output/"run_status.csv",statuses,["image","status","returncode","report_dir","log","numeric_candidates","detail"])
            atomic_json(output/"RUN.json",metadata)
        flush()
        try:
            revision=subprocess.run(["git","rev-parse","HEAD"],cwd=root,capture_output=True,text=True)
            metadata["code_revision"]=revision.stdout.strip() if revision.returncode==0 else "unavailable"
        except OSError:
            metadata["code_revision"]="unavailable"
        if not args.skip_build:
            for step,command in enumerate((["cmake","-S",root,"-B",build,"-DCMAKE_BUILD_TYPE=Release"],
                                           ["cmake","--build",build,"--target","sharpen_bitflip_joint",f"-j{args.jobs}"])):
                code=process(command,output/"build.log",root,append=bool(step))
                if code:
                    raise RuntimeError(f"Build step failed with code {code}; see build.log")
        if not exe.is_file():
            raise ValueError(f"Missing executable: {exe}")
        for position,image in enumerate(indices):
            status=statuses[position];report=output/status["report_dir"]
            command=[exe,data,report,str(image),str(args.beam),str(args.refinement),str(args.boundary_budget),
                     str(args.top_per_count),str(args.restarts),str(args.global_psnr),"--image-mode",args.image_mode]
            print(f"\n=== Image {image} ({position+1}/{len(indices)}) ===",flush=True)
            try:
                code=process(command,output/status["log"],root)
                status["returncode"]=code
                if code:
                    raise RuntimeError(f"Executable returned {code}")
                staged,selected,count=load_report(report,image,args.image_mode)
                for name,rows in staged.items():
                    tables[name].extend(dict(image=image,**row) for row in rows)
                best_rows[position]=selected
                status.update(status="completed",numeric_candidates=count)
            except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
                status.update(status="failed",detail=str(exc))
                metadata["failed_images"].append(image)
                failed=dict(image=image,status="run_failed",visual_status="not_available")
                best_rows[position]=failed
                tables["summary_all.csv"].append(failed)
                print(f"Image {image} FAILED: {exc}; continuing",file=sys.stderr,flush=True)
            flush()
        metadata["completed"]=True
        metadata["all_succeeded"]=not metadata["failed_images"]
        flush()
        count=sum(r["status"]=="numeric_eligible" for r in best_rows)
        print(f"Finished {len(indices)} images: {count} have numeric candidates; "
              f"{len(metadata['failed_images'])} failed. Visual review remains required.")
        print(f"Share {output/'best_per_image.csv'} and {output/'summary_all.csv'}")
        print(f"Image locations are listed in {output/'images_all.csv'}")
        return 0 if metadata["all_succeeded"] else 1
    except KeyboardInterrupt:
        if owned and metadata is not None:
            metadata["error"]="Interrupted by user";atomic_json(output/"RUN.json",metadata)
        print("Batch interrupted; existing per-image reports and logs are preserved",file=sys.stderr)
        return 130
    except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
        if owned and metadata is not None:
            metadata["error"]=str(exc);atomic_json(output/"RUN.json",metadata)
        print(f"Batch failed: {exc}",file=sys.stderr)
        return 1


if __name__=="__main__": sys.exit(main())
