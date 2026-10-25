#!/usr/bin/env python3
"""User-run Conv/Sobel/DCT fixed hardware selection and held-out interval calibration."""
import csv
import json
from pathlib import Path
import subprocess
import sys
import time

import run_sharpen_bitflip_hardware as protocol
from run_sharpen_bitflip_batch import atomic_csv,atomic_json,process,read_csv

APPLICATIONS=("conv","sobel","dct")
MERGED={"best_per_image.csv":"best_per_image.csv","summary_all.csv":"summary_all.csv",
        "configurations_all.csv":"configurations_all.csv","search_stats_all.csv":"search_stats_all.csv",
        "images_all.csv":"images_all.csv","run_status.csv":"run_status.csv",
        "screening_ranking_all.csv":"screening_ranking.csv","screening_results_all.csv":"screening_results.csv",
        "frozen_hardware_all.csv":"frozen_hardware.csv"}

def child_arguments(args,application,out,fixed,discovery):
    values=[str(args.data.resolve()),str(out),"--skip-build","--fixed-exe",str(fixed),"--discovery-exe",str(discovery)]
    for key in ("train","screen","test","max_hardware","seed_limit","beam","refinement","restarts","boundary_budget",
                "training_context_budget","context_budget","training_top","global_psnr","image_mode","jobs","screen_workers",
                "max_full_evals","full_candidates","training_max_full_evals","training_full_candidates"):
        values.extend(["--"+key.replace("_","-"),str(getattr(args,key))])
    values.extend(["--build-dir",str(args.build_dir.resolve())])
    if args.training_report:values.extend(["--training-report",str(args.training_report.resolve())])
    return values

def main(argv=None):
    root=Path(__file__).resolve().parents[1];parser=protocol.protocol_parser(root,"conv")
    parser.description=__doc__;parser.add_argument("--applications",default=",".join(APPLICATIONS))
    args=parser.parse_args(argv);owned=False;meta=None
    try:
        apps=args.applications.split(",")
        if not apps or len(set(apps))!=len(apps) or any(a not in APPLICATIONS for a in apps):raise ValueError("Choose distinct conv,sobel,dct applications")
        train,screen,test=protocol.protocol_splits(args)
        data=args.data.resolve();out=args.output.resolve();build=args.build_dir.resolve()
        source=args.training_report.resolve() if args.training_report else None
        references=[data]+([source] if source else [])
        for reference in references:
            if out==reference or reference in out.parents or out in reference.parents:raise ValueError("Output must not overlap reference data/training reports")
            if not args.skip_build and (build==reference or reference in build.parents or build in reference.parents):raise ValueError("Build directory must not overlap reference data/training reports")
        if not (data/"mask/roi_mask.jpg").is_file():raise ValueError("Missing original ROI mask")
        for image in screen+test+([] if source else train):protocol.resolve_staged_image_path(data,image)
        if source:
            for app in apps:protocol.training_pool(source,train,args.global_psnr,app,protocol.APPLICATION_NODES[app])
        fixed=args.fixed_exe.resolve() if args.fixed_exe else build/"circuit_bitflip_fixed"
        discovery=args.discovery_exe.resolve() if args.discovery_exe else build/"circuit_bitflip_joint"
        if args.skip_build and (not fixed.is_file() or (not source and not discovery.is_file())):raise ValueError("Missing circuit executables")
        out.mkdir(parents=True,exist_ok=False);owned=True
        meta=dict(completed=False,all_succeeded=False,applications=apps,splits=dict(train=train,screen=screen,test=test),
                  options={k:v for k,v in vars(args).items() if isinstance(v,(int,float,str,bool))},data=str(data),
                  source_training_report=str(source) if source else None,
                  protocol="same candidate/ranking/interval budgets; fixed node/monitor/bit per circuit; adaptive L/U per image",
                  hardware_fixed_fields=["node_id","monitor","bit"],adaptive_fields=["lower","upper"],
                  ablations="disabled",enable_parameter="absent",visual_status="pending_user_review",failed_applications=[])
        statuses=[];tables={name:[] for name in MERGED}
        def flush():
            atomic_json(out/"RUN.json",meta)
            atomic_csv(out/"applications.csv",statuses,["application","status","returncode","selection_status","selected_hardware_id","screen_wall_seconds","test_wall_seconds","numeric_test_successes","test_images","elapsed_seconds","detail"])
            for name,rows in tables.items():atomic_csv(out/name,rows,["application"])
        flush()
        try:
            rev=subprocess.run(["git","rev-parse","HEAD"],cwd=root,capture_output=True,text=True)
            meta["code_revision"]=rev.stdout.strip() if rev.returncode==0 else "unavailable"
        except OSError:meta["code_revision"]="unavailable"
        if not args.skip_build:
            for step,command in enumerate((["cmake","-S",root,"-B",build,"-DCMAKE_BUILD_TYPE=Release"],
                    ["cmake","--build",build,"--target","circuit_bitflip_fixed","circuit_bitflip_joint",f"-j{args.jobs}"])):
                if process(command,out/"build.log",root,append=bool(step)):raise RuntimeError("Build failed; see build.log")
        if not fixed.is_file() or (not source and not discovery.is_file()):raise ValueError("Circuit executable was not built")
        for app in apps:
            status=dict(application=app,status="running",returncode="",selection_status="",selected_hardware_id="",
                        screen_wall_seconds="",test_wall_seconds="",numeric_test_successes=0,test_images=len(test),elapsed_seconds="",detail="")
            statuses.append(status);begin=time.monotonic();flush()
            try:
                code=protocol.main(child_arguments(args,app,out/app,fixed,discovery),application=app);status["returncode"]=code
                if code==130:
                    status.update(status="interrupted",elapsed_seconds=time.monotonic()-begin,detail="Interrupted by user");flush()
                    raise KeyboardInterrupt
                child=json.loads((out/app/"RUN.json").read_text(encoding="utf-8-sig"))
                if child.get("application")!=app:raise ValueError("Child protocol application mismatch")
                status.update(selection_status=child.get("selection_status",""),selected_hardware_id=child.get("selected_hardware_id",""),
                              screen_wall_seconds=child.get("screen_wall_seconds",""),test_wall_seconds=child.get("test_wall_seconds",""))
                for merged,filename in MERGED.items():
                    path=out/app/filename
                    if not path.is_file():continue
                    for raw in read_csv(path,[]):
                        if "application" in raw and raw["application"]!=app:raise ValueError("Cross-application row in report")
                        row={**raw,"application":app}
                        for field in ("path","output_path","roi_comparison_path","background_comparison_path","log"):
                            if row.get(field):
                                relative=Path(row[field])
                                if relative.is_absolute() or ".." in relative.parts:raise ValueError("Report path escapes its application")
                                row[field]=(Path(app)/relative).as_posix()
                        tables[merged].append(row)
                status["numeric_test_successes"]=sum(r.get("status")=="numeric_eligible" for r in tables["best_per_image.csv"] if r["application"]==app)
                if code or child.get("completed") is not True or child.get("all_succeeded") is not True:raise RuntimeError("Application protocol incomplete or has failed tasks; inspect its RUN.json and logs")
                status["status"]="completed"
            except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
                status.update(status="failed",detail=str(exc));meta["failed_applications"].append(app)
                print(f"FAILED {app}: {exc}; preserving reports and continuing",file=sys.stderr)
            status["elapsed_seconds"]=time.monotonic()-begin;flush()
        meta.update(completed=True,all_succeeded=not meta["failed_applications"]);flush()
        print(f"Completed {len(apps)} circuit protocols. Share RUN.json, applications.csv, best_per_image.csv and screening_ranking_all.csv.")
        return 0 if meta["all_succeeded"] else 1
    except KeyboardInterrupt:
        if owned and meta is not None:meta["error"]="Interrupted by user";atomic_json(out/"RUN.json",meta)
        return 130
    except (OSError,ValueError,RuntimeError,KeyError,csv.Error) as exc:
        if owned and meta is not None:meta["error"]=str(exc);atomic_json(out/"RUN.json",meta)
        print(f"Circuit hardware pipeline failed: {exc}",file=sys.stderr);return 1

if __name__=="__main__":sys.exit(main())
